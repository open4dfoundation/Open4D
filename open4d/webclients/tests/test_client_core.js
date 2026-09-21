'use strict';

/**
 * Characterization tests for the platform-free client core.
 *
 * These pin the behaviour that system/Client/client.js had before the core was
 * extracted, so the browser client and the Node client cannot drift and so the
 * bug fixes recorded in the module comments cannot silently regress.
 *
 * Run: node --test tests/test_client_core.js
 */

const assert = require('assert');
const test = require('node:test');

const { LinkThroughputMeter } = require('../system/ClientCore/link-throughput');
const { BandwidthEstimator, computeBudget } = require('../system/ClientCore/bandwidth');
const { resolveStreamConfig } = require('../system/ClientCore/stream-config');
const {
    planSegmentDownload,
    creditTaskResult,
    summarizeSegmentDownload,
    expectedSelectionBytes
} = require('../system/ClientCore/download-plan');
const {
    buildBroadcastStartPayload
} = require('../system/ClientCore/payloads');

// --------------------------------------------------------------------------
// A controllable clock, so timing behaviour is asserted rather than slept on.
// --------------------------------------------------------------------------
function fakeClock(start = 1000) {
    let t = start;
    return { now: () => t, advance: ms => { t += ms; } };
}

function meter(segmentIntervalMs = 2000, clock = fakeClock()) {
    const samples = [];
    const m = new LinkThroughputMeter({
        segmentIntervalMs: () => segmentIntervalMs,
        onSample: (bytes, busyMs) => samples.push({ bytes, busyMs }),
        now: clock.now
    });
    return { m, samples, clock };
}

// --------------------------------------------------------------------------
// LinkThroughputMeter
// --------------------------------------------------------------------------

test('link meter emits one sample per busy period once past the time floor', () => {
    const { m, samples, clock } = meter();
    m.started();
    m.bytesDelivered(1_000_000);
    clock.advance(200);              // >= LINK_MIN_SAMPLE_MS
    m.finished();
    assert.deepStrictEqual(samples, [{ bytes: 1_000_000, busyMs: 200 }]);
});

test('link meter accumulates short bursts instead of discarding them', () => {
    // A fast link finishes a whole download in tens of ms: below the 100 ms
    // floor, so the sample must survive on the byte floor instead.
    const { m, samples, clock } = meter();
    m.started();
    m.bytesDelivered(3e6);           // >= LINK_MIN_SAMPLE_BYTES
    clock.advance(20);               // >= 10 ms, < 100 ms
    m.finished();
    assert.strictEqual(samples.length, 1);
    assert.strictEqual(samples[0].busyMs, 20);
});

test('link meter holds a sub-threshold burst back for the next busy period', () => {
    const { m, samples, clock } = meter();
    m.started();
    m.bytesDelivered(1000);          // too few bytes...
    clock.advance(5);                // ...and too little time
    m.finished();
    assert.deepStrictEqual(samples, []);

    // The held bytes and busy time carry into the next period rather than
    // being thrown away.
    m.started();
    m.bytesDelivered(1000);
    clock.advance(200);
    m.finished();
    assert.strictEqual(samples.length, 1);
    assert.strictEqual(samples[0].bytes, 2000);
    assert.strictEqual(samples[0].busyMs, 205);
});

test('link meter credits overlapping downloads into one window', () => {
    // The regression this guards: crediting bytes only at segment completion
    // divided ONE segment's bytes by a window in which the link had also served
    // a concurrent download, a measured ~30% under-read.
    const { m, samples, clock } = meter();
    m.started();                     // download A
    clock.advance(50);
    m.started();                     // download B overlaps
    m.bytesDelivered(4e6);           // A's bytes land
    clock.advance(50);
    m.finished();                    // A done, B still running, window not rolled
    assert.deepStrictEqual(samples, [], 'no sample while the link is still busy');

    m.bytesDelivered(4e6);           // B's bytes land
    clock.advance(100);
    m.finished();                    // B done, busy period closes
    assert.strictEqual(samples.length, 1);
    assert.strictEqual(samples[0].bytes, 8e6, 'both downloads bytes in one sample');
    assert.strictEqual(samples[0].busyMs, 200, 'one continuous busy window');
});

test('link meter rolls the window under sustained load', () => {
    // Under sustained overlap the busy period never closes, so without the
    // rolling branch the estimator would never sample at all.
    const { m, samples, clock } = meter(2000);
    m.started();
    m.started();
    m.bytesDelivered(5e6);
    clock.advance(2500);             // exceeds the segment interval
    m.finished();                    // activeCount still 1 -> roll the window
    assert.strictEqual(samples.length, 1);
    assert.strictEqual(samples[0].busyMs, 2500);
});

test('link meter ignores non-positive byte credits', () => {
    const { m, samples, clock } = meter();
    m.started();
    m.bytesDelivered(0);
    m.bytesDelivered(-5);
    clock.advance(500);
    m.finished();
    assert.deepStrictEqual(samples, [], 'no bytes means no sample');
});

test('link meter never drives the active count negative', () => {
    const { m } = meter();
    m.finished();
    m.finished();
    assert.strictEqual(m.activeCount, 0);
});

// --------------------------------------------------------------------------
// computeBudget
// --------------------------------------------------------------------------

const healthy = {
    missingCount: 0, inFlightCount: 0, maxInFlight: 2, lastDownloadLate: false
};

test('budget uses the normal multiplier when delivery is healthy', () => {
    const r = computeBudget(100, healthy, 0.9, 0.5);
    assert.strictEqual(r.budget, 90);
    assert.strictEqual(r.reason, 'normal');
});

test('budget drops to the struggling multiplier on each distinct signal', () => {
    const cases = [
        [{ ...healthy, missingCount: 1 }, 'missing-objects'],
        [{ ...healthy, inFlightCount: 2 }, 'download-backlog'],
        [{ ...healthy, lastDownloadLate: true }, 'late-download']
    ];
    for (const [signals, reason] of cases) {
        const r = computeBudget(100, signals, 0.9, 0.5);
        assert.strictEqual(r.budget, 50, `${reason} should reduce the budget`);
        assert.strictEqual(r.reason, reason);
    }
});

test('budget concatenates concurrent struggle reasons', () => {
    const r = computeBudget(100, {
        missingCount: 3, inFlightCount: 5, maxInFlight: 2, lastDownloadLate: true
    }, 0.9, 0.5);
    assert.strictEqual(r.reason, 'missing-objects+download-backlog+late-download');
    assert.strictEqual(r.budget, 50);
});

test('budget treats a full in-flight queue as a backlog, not just an over-full one', () => {
    // The guard is `>=`: at exactly the cap the link is already saturated.
    const r = computeBudget(100, { ...healthy, inFlightCount: 2, maxInFlight: 2 }, 0.9, 0.5);
    assert.strictEqual(r.reason, 'download-backlog');
});

// --------------------------------------------------------------------------
// BandwidthEstimator
// --------------------------------------------------------------------------

function estimator(overrides = {}) {
    let elapsed = 0;
    return new BandwidthEstimator({
        elapsedMs: () => (elapsed += 10),
        inFlightEntries: () => [],
        signals: () => healthy,
        // Always a controlled clock: the core requires `now` precisely so a
        // test cannot fall back to wall time without noticing.
        now: fakeClock().now,
        initialEstimate: 5,
        multiplier: 1,
        multiplierStruggling: 1,
        ...overrides
    });
}

test('estimator refuses to construct without a clock', () => {
    assert.throws(() => new BandwidthEstimator({
        elapsedMs: () => 0, inFlightEntries: () => [], signals: () => healthy
    }), /now must be a function/);
});

test('link meter refuses to construct without a clock', () => {
    assert.throws(() => new LinkThroughputMeter({
        segmentIntervalMs: () => 2000, onSample: () => {}
    }), /now must be a function/);
});

test('link meter validates its other dependencies too', () => {
    assert.throws(() => new LinkThroughputMeter({
        onSample: () => {}, now: Date.now
    }), /segmentIntervalMs must be a function/);
    assert.throws(() => new LinkThroughputMeter({
        segmentIntervalMs: () => 2000, now: Date.now
    }), /onSample must be a function/);
});

test('estimator ignores degenerate samples', () => {
    const e = estimator();
    for (const [bytes, ms] of [[0, 100], [1000, 0], [-1, 100], [1000, -1]]) {
        e.update(bytes, ms);
    }
    assert.strictEqual(e.estimate, 5, 'estimate untouched');
    assert.deepStrictEqual(e.history, []);
});

test('estimator uses a two-sample harmonic mean', () => {
    const e = estimator();
    // 1 MB in 80 ms = 100 Mbps; 2 MB in 80 ms = 200 Mbps
    e.update(1e6, 80);
    e.update(2e6, 80);
    assert.ok(Math.abs(e.estimate - 133.3333333333) < 1e-6);
});

test('estimator keeps at most five samples', () => {
    const e = estimator();
    for (let i = 1; i <= 8; i++) e.update(1e6 * i, 80);
    assert.strictEqual(e.samples.length, 5);
});

test('estimator history is the array the metrics record shares', () => {
    const e = estimator();
    const metrics = { bandwidthHistory: e.history };
    e.update(1e6, 80);
    assert.strictEqual(metrics.bandwidthHistory.length, 1,
        'metrics must observe appends without re-assignment');
    assert.strictEqual(metrics.bandwidthHistory[0].measured, 100);
});

test('estimator degrades on a slow in-flight download before any sample lands', () => {
    const clock = fakeClock();
    const e = estimator({
        now: clock.now,
        // 1 MB expected, already 10 s in -> 0.8 Mbps, far below the initial 5
        inFlightEntries: () => [[7, { startTime: clock.now() - 10_000, bytesExpected: 1e6 }]]
    });
    assert.ok(e.current() < 5);
    assert.ok(e.current() >= 1, 'floored at 1 Mbps');
});

test('estimator ignores in-flight downloads once real samples exist', () => {
    const clock = fakeClock();
    const e = estimator({
        now: clock.now,
        inFlightEntries: () => [[7, { startTime: clock.now() - 10_000, bytesExpected: 1e6 }]]
    });
    e.update(1e6, 80);               // 100 Mbps
    assert.strictEqual(e.current(), 100, 'samples win over in-flight guessing');
});

test('estimator ignores a young in-flight download', () => {
    const clock = fakeClock();
    const e = estimator({
        now: clock.now,
        inFlightEntries: () => [[7, { startTime: clock.now() - 1000, bytesExpected: 1e6 }]]
    });
    assert.strictEqual(e.current(), 5, 'under 5 s is too early to judge');
});

// --------------------------------------------------------------------------
// resolveStreamConfig
// --------------------------------------------------------------------------

const fullConfig = {
    segmentDuration: 2,
    framesPerSegment: 60,
    segmentIntervalMs: 2000,
    totalSegments: 20,
    updateIntervalSegments: 1
};

test('stream config accepts a complete server config', () => {
    assert.deepStrictEqual({ ...resolveStreamConfig(fullConfig) }, fullConfig);
});

test('stream config derives the segment interval from the duration', () => {
    const { segmentIntervalMs } = resolveStreamConfig(
        { ...fullConfig, segmentIntervalMs: undefined });
    assert.strictEqual(segmentIntervalMs, 2000);
});

test('stream config names every missing field', () => {
    assert.throws(() => resolveStreamConfig({ segmentDuration: 2 }), err => {
        assert.match(err.message, /framesPerSegment/);
        assert.match(err.message, /totalSegments/);
        assert.match(err.message, /updateIntervalSegments/);
        return true;
    });
});

test('stream config rejects non-positive and non-finite values', () => {
    for (const bad of [0, -1, Infinity, NaN, '60', null]) {
        assert.throws(
            () => resolveStreamConfig({ ...fullConfig, framesPerSegment: bad }),
            /framesPerSegment/,
            `framesPerSegment=${String(bad)} must be rejected`);
    }
});

test('stream config falls back to previously resolved values', () => {
    const first = resolveStreamConfig(fullConfig);
    const second = resolveStreamConfig({ totalSegments: 40 }, first);
    assert.strictEqual(second.totalSegments, 40);
    assert.strictEqual(second.framesPerSegment, 60, 'carried over');
});

test('stream config result is frozen', () => {
    const cfg = resolveStreamConfig(fullConfig);
    assert.throws(() => { cfg.totalSegments = 99; }, TypeError);
});

// --------------------------------------------------------------------------
// planSegmentDownload / summarizeSegmentDownload
// --------------------------------------------------------------------------

function rep(id, { textureUrls = null, textureUrl = null, qp = 7 } = {}) {
    const paths = {
        base_dir: `/files/media/obj/${id}`,
        geometry_url_pattern: `/files/media/obj/${id}/obj_fr%04d_qp${qp}.drc`
    };
    if (textureUrls) paths.texture_urls = textureUrls;
    if (textureUrl) paths.texture_url = textureUrl;
    return { id, paths, predicted: { bitrate_mbps: 8, quality: 40 } };
}

function plan(selection, manifest, framesPerSegment = 3, interactive = true) {
    return planSegmentDownload({
        selection,
        manifest,
        framesPerSegment,
        interactive,
        objectDirFor: (objName, repId) => `/cache/${objName}/${repId}`,
        destinationFor: (objName, dir, file) => (file.kind === 'texture'
            ? `${dir}/texture_${String(file.textureIndex).padStart(4, '0')}.mp4`
            : `${dir}/geometry_${String(file.frameIndex).padStart(4, '0')}.drc`),
        pathToUrl: assetPath => `http://server${assetPath}`
    });
}

const oneObject = {
    selection: {
        combo: {
            dancer: rep('r_res960_crf24_qp7', {
                textureUrls: ['/files/media/obj/r/tex_part00.mp4',
                              '/files/media/obj/r/tex_part01.mp4']
            })
        }
    },
    manifest: { objects: { dancer: { start_number: 10 } } }
};

test('plan emits every texture group and every geometry frame', () => {
    const { tasks, perObj } = plan(oneObject.selection, oneObject.manifest, 3);
    assert.strictEqual(tasks.length, 5, '2 textures + 3 geometry frames');
    const acc = perObj.get('dancer');
    assert.strictEqual(acc.total, 5);
    assert.strictEqual(acc.geometryTotal, 3);
    assert.strictEqual(acc.textureFiles.length, 2);
});

test('plan resolves geometry frame numbers from the manifest start_number', () => {
    const { tasks } = plan(oneObject.selection, oneObject.manifest, 3);
    const geometry = tasks.filter(t => t.kind === 'geometry');
    assert.deepStrictEqual(geometry.map(t => t.frameNum), [10, 11, 12]);
    assert.match(geometry[0].url, /obj_fr0010_qp7\.drc$/);
    assert.deepStrictEqual(geometry.map(t => t.frameIndex), [0, 1, 2],
        'frameIndex is segment-relative, frameNum is absolute');
});

test('plan preallocates a null slot per frame so a failure cannot shift the clip', () => {
    // The regression this guards: collecting only successful frames shifted
    // every later frame by one and silently desynchronised the clip.
    const { tasks, perObj } = plan(oneObject.selection, oneObject.manifest, 3);
    const acc = perObj.get('dancer');
    assert.deepStrictEqual(acc.geometryFiles, [null, null, null]);

    const geometry = tasks.filter(t => t.kind === 'geometry');
    creditTaskResult(acc, geometry[0], { success: true, size: 100 }, true);
    creditTaskResult(acc, geometry[1], { success: false, size: 0 }, true);
    creditTaskResult(acc, geometry[2], { success: true, size: 100 }, true);

    assert.strictEqual(acc.geometryFiles[1], null, 'the failed frame keeps its slot');
    assert.match(acc.geometryFiles[2], /geometry_0002\.drc$/, 'frame 2 stays at index 2');
});

test('plan falls back through texture_urls, texture_mp4s, then the single url', () => {
    const single = {
        combo: { dancer: rep('r1', { textureUrl: '/files/media/obj/r1/tex.mp4' }) }
    };
    const { perObj } = plan(single, oneObject.manifest, 1);
    const acc = perObj.get('dancer');
    assert.strictEqual(acc.textureFiles.length, 1);

    const legacy = { combo: { dancer: rep('r2') } };
    legacy.combo.dancer.paths.texture_mp4s = ['/files/a.mp4', '/files/b.mp4'];
    const { perObj: p2 } = plan(legacy, oneObject.manifest, 1);
    assert.strictEqual(p2.get('dancer').textureFiles.length, 2);
});

test('plan sets the legacy textureFile only for a single-group texture', () => {
    const single = {
        combo: { dancer: rep('r1', { textureUrl: '/files/media/obj/r1/tex.mp4' }) }
    };
    const { tasks, perObj } = plan(single, oneObject.manifest, 1);
    const acc = perObj.get('dancer');
    creditTaskResult(acc, tasks.find(t => t.kind === 'texture'),
        { success: true, size: 10 }, true);
    assert.ok(acc.textureFile, 'one group -> legacy field populated');

    const { tasks: multi, perObj: pm } = plan(
        oneObject.selection, oneObject.manifest, 1);
    const accMulti = pm.get('dancer');
    for (const t of multi.filter(t => t.kind === 'texture')) {
        creditTaskResult(accMulti, t, { success: true, size: 10 }, true);
    }
    assert.strictEqual(accMulti.textureFile, null,
        'two groups -> consumers must use the list');
});

test('plan writes no destinations outside interactive mode', () => {
    const { tasks } = plan(oneObject.selection, oneObject.manifest, 2, false);
    assert.ok(tasks.every(t => t.destination === null));
    assert.ok(tasks.every(t => t.url.startsWith('http')));
});

test('plan preserves combo order across objects', () => {
    const selection = {
        combo: { zebra: rep('rz'), alpha: rep('ra') }
    };
    const manifest = { objects: { zebra: { start_number: 1 }, alpha: { start_number: 1 } } };
    const { perObj } = plan(selection, manifest, 1);
    assert.deepStrictEqual([...perObj.keys()], ['zebra', 'alpha'],
        'insertion order, not sorted');
});

test('summary judges geometry on its own and requires 90%', () => {
    // The regression this guards: `|| acc.size > 0` let a 1-of-61 download
    // report success, which then failed hard in the renderer frame-count check.
    const build = (geometrySuccess, geometryTotal, success, total) => {
        const perObj = new Map([['dancer', {
            repId: 'r', size: 1000, success, total,
            geometryTotal, geometrySuccess, objectDir: null,
            textureFile: null, textureFiles: [], geometryFiles: []
        }]]);
        return summarizeSegmentDownload(perObj).objects[0];
    };

    assert.strictEqual(build(60, 60, 61, 61).success, true, 'complete');
    assert.strictEqual(build(1, 60, 2, 61).success, false, '1-of-61 is not success');
    assert.strictEqual(build(54, 60, 55, 61).success, true, 'exactly 90% geometry passes');
    assert.strictEqual(build(53, 60, 54, 61).success, false, 'under 90% geometry fails');
    // Texture missing but all geometry present: an untextured mesh still plays,
    // yet the overall file ratio must still clear the bar.
    assert.strictEqual(build(60, 60, 60, 61).success, true,
        'a missing texture alone does not fail the object');
});

test('summary totals bytes across objects', () => {
    const perObj = new Map([
        ['a', { repId: 'r', size: 100, success: 1, total: 1, geometryTotal: 1,
                geometrySuccess: 1, objectDir: null, textureFile: null,
                textureFiles: [], geometryFiles: [] }],
        ['b', { repId: 'r', size: 250, success: 1, total: 1, geometryTotal: 1,
                geometrySuccess: 1, objectDir: null, textureFile: null,
                textureFiles: [], geometryFiles: [] }]
    ]);
    assert.strictEqual(summarizeSegmentDownload(perObj).totalSize, 350);
});

test('summary reports zero ratios rather than NaN for an empty object', () => {
    const perObj = new Map([['a', {
        repId: 'r', size: 0, success: 0, total: 0, geometryTotal: 0,
        geometrySuccess: 0, objectDir: null, textureFile: null,
        textureFiles: [], geometryFiles: []
    }]]);
    const obj = summarizeSegmentDownload(perObj).objects[0];
    assert.strictEqual(obj.successRatio, 0);
    assert.strictEqual(obj.geometryRatio, 0);
    assert.strictEqual(obj.success, false);
});

test('expected selection bytes converts Mbps to bytes per segment', () => {
    const selection = { combo: { a: rep('r1'), b: rep('r2') } };
    assert.strictEqual(expectedSelectionBytes(selection), 2 * (8 * 1e6 / 8));
});

// --------------------------------------------------------------------------
// buildBroadcastStartPayload — sceneObjects is what sets the ladder floor
// --------------------------------------------------------------------------
test('broadcast start omits sceneObjects when no subset is requested', () => {
    for (const sceneObjects of [undefined, [], ['', '  ']]) {
        const body = buildBroadcastStartPayload({
            clientMode: 'interactive', label: 'web-client', sceneObjects
        });
        // An absent list resets the server to its full object catalog, so
        // a run never silently inherits the previous run's scene.
        assert.ok(!('sceneObjects' in body) || body.sceneObjects === undefined,
            `expected no sceneObjects for ${JSON.stringify(sceneObjects)}`);
        assert.strictEqual(body.algorithm, 'mckp-abr-realtime-interactive');
    }
});

test('broadcast start forwards a trimmed object subset', () => {
    const body = buildBroadcastStartPayload({
        clientMode: 'simulated',
        sceneObjects: [' dancer ', 'thomas', '', 'mitch']
    });
    assert.deepStrictEqual(body.sceneObjects, ['dancer', 'thomas', 'mitch']);
    assert.strictEqual(body.algorithm, 'mckp-abr-realtime-simulated');
    assert.strictEqual(body.label, undefined);
});
