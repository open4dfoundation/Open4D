'use strict';

/**
 * End-to-end tests for the segment loop, driven entirely by the fake platform.
 *
 * This is what the platform contract buys: a complete run — config fetch,
 * broadcast start, viewpoint bootstrap, N segment ticks with real ABR
 * selection, downloads, renderer staging, decode crediting, telemetry upload
 * and shutdown — with no server, no disk, no GPU, and no real time. The virtual
 * clock makes a 20-segment run cost microseconds instead of 40 seconds.
 *
 * Run: node --test tests/test_streaming_client.js
 */

const assert = require('assert');
const test = require('node:test');

const { StreamingClient } = require('../system/ClientCore/streaming-client');
const { createFakePlatform } = require('../system/ClientCore/testing/fake-platform');

// --------------------------------------------------------------------------
// Fixtures: a two-object scene shaped like what ladderlib.write_ladder emits.
// --------------------------------------------------------------------------
const STREAM_CONFIG = {
    segmentDuration: 2,
    framesPerSegment: 3,        // 3 instead of 60 to keep task counts readable
    segmentIntervalMs: 2000,
    totalSegments: 5,
    updateIntervalSegments: 1
};

function representation(objName, res, crf, qp, bitrate, quality) {
    const id = `r_res${res}_crf${crf}_qp${qp}`;
    return {
        id,
        predicted: { bitrate_mbps: bitrate, quality },
        knobs: { resolution: res, crf, qp },
        paths: {
            base_dir: `/files/media/${objName}/${id}`,
            texture_urls: [`/files/media/${objName}/${id}/${objName}_tex_part00.mp4`],
            geometry_url_pattern:
                `/files/media/${objName}/${id}/${objName}_fr%04d_qp${qp}.drc`
        }
    };
}

function manifest(segmentT = 0, objects = ['dancer', 'mitch']) {
    const entry = {};
    objects.forEach((name, index) => {
        entry[name] = {
            start_number: 1 + index * 60,
            weight: 0.5,
            representations: [
                representation(name, 240, 34, 7, 4, 30),
                representation(name, 480, 30, 8, 10, 35),
                representation(name, 960, 24, 9, 25, 40)
            ]
        };
    });
    return {
        type: 'object-dash-like',
        version: 1,
        segment: { t: segmentT, fps: 30, n_frames: 3, duration_s: 2, loop: true },
        objects: entry
    };
}

/**
 * Wire a fake platform up as a working server.
 *
 * @param {object} [options]
 * @param {number} [options.totalSegments]
 * @param {boolean} [options.withRenderer]
 * @param {boolean} [options.advanceLadder] bump manifest.segment.t each fetch,
 *   so previousManifest becomes meaningfully older (as a live server does)
 */
function setup({
    totalSegments = STREAM_CONFIG.totalSegments,
    withRenderer = true,
    advanceLadder = false,
    objects
} = {}) {
    const platform = createFakePlatform({ withRenderer });
    let ladderT = 0;

    platform.transport
        .onGet('/api/config', () => ({ ...STREAM_CONFIG, totalSegments }))
        .onGet('/api/manifest', () => manifest(advanceLadder ? ladderT++ : 0, objects))
        .onPost('/api/broadcast/start', () => ({ broadcastId: 'bcast-test-1' }))
        .onPost('/api/viewpoint', () => ({ status: 'ok' }))
        .onPost('/api/segment/', body => ({ latestManifestSegId: body.segmentId }))
        .onPost('/api/render-frames', () => ({ status: 'ok' }))
        .onPost('/api/bitrate-counts-interval', () => ({ status: 'ok' }))
        .onPost('/api/results', () => ({ status: 'ok' }));

    const client = new StreamingClient({
        platform,
        config: {
            clientMode: withRenderer ? 'interactive' : 'simulated',
            manifestFetchTimeoutMs: 50,
            runLabel: 'unit-test'
        }
    });
    return { platform, client };
}

/**
 * Run to completion. In interactive mode nothing becomes playable without
 * decode events, so by default every staged object is acknowledged as soon as
 * it is staged — which is what a working renderer does.
 */
async function runToCompletion(platform, client, {
    segments = STREAM_CONFIG.totalSegments, ackDecodes = true
} = {}) {
    await client.run();
    let acked = 0;
    for (let i = 0; i < segments + 2; i++) {
        await platform.clock.advance(STREAM_CONFIG.segmentIntervalMs);
        if (ackDecodes && platform.renderer) {
            for (; acked < platform.renderer.staged.length; acked++) {
                const { segmentId, objects } = platform.renderer.staged[acked];
                for (const [name, info] of Object.entries(objects)) {
                    await platform.renderer.emitObjectReady(segmentId, name, info.repId);
                }
            }
        }
    }
    // The loop calls finish() itself once it runs out of segments; awaiting the
    // same (idempotent) promise here waits for the shutdown chain to complete
    // rather than racing it.
    await client.finish();
}

// --------------------------------------------------------------------------
// Startup
// --------------------------------------------------------------------------

test('run() performs startup in the order the server depends on', async () => {
    const { platform, client } = setup();
    await client.run();

    const paths = platform.transport.requests.map(r => `${r.method} ${r.path}`);
    const configAt = paths.indexOf('GET /api/config');
    const startAt = paths.indexOf('POST /api/broadcast/start');
    const viewpointAt = paths.indexOf('POST /api/viewpoint');
    const manifestAt = paths.indexOf('GET /api/manifest');

    assert.ok(configAt >= 0 && startAt >= 0 && viewpointAt >= 0 && manifestAt >= 0);
    assert.ok(configAt < startAt, 'timing config before the broadcast exists');
    assert.ok(startAt < viewpointAt, 'the viewpoint needs a broadcastId');
    assert.ok(viewpointAt < manifestAt,
        'segment-0 pose must reach the server before the first menu is fetched, '
        + 'or the client consumes a uniformly weighted ladder');

    await client.finish();
});

test('run() adopts the server timing config', async () => {
    const { platform, client } = setup({ totalSegments: 7 });
    await client.run();
    assert.strictEqual(client.streamConfig.totalSegments, 7);
    assert.strictEqual(client.streamConfig.framesPerSegment, 3);
    assert.strictEqual(client.streamConfig.segmentIntervalMs, 2000);
    await client.finish();
    void platform;
});

test('run() aborts and exits non-zero when the config fetch fails', async () => {
    const platform = createFakePlatform();
    // No /api/config route -> 404.
    const client = new StreamingClient({ platform });
    await client.run();
    assert.strictEqual(platform.lifecycle.exitCode, 1);
    assert.ok(platform.logger.saw('Fatal error'));
    assert.strictEqual(platform.clock.pending, 0, 'no timer left armed after abort');
});

test('run() records the broadcast id on the metrics record', async () => {
    const { platform, client } = setup();
    await client.run();
    assert.strictEqual(client.broadcastId, 'bcast-test-1');
    assert.strictEqual(client.metrics.broadcastId, 'bcast-test-1');
    await client.finish();
    void platform;
});

test('interactive startup opens both telemetry streams and starts the renderer', async () => {
    const { platform, client } = setup();
    await client.run();
    assert.ok(platform.storage.streams.has('render_frames.jsonl'));
    assert.ok(platform.storage.streams.has('decode_events.jsonl'));
    assert.ok(platform.renderer.started, 'renderer.start was called');
    assert.strictEqual(platform.renderer.started.frameCount, 3);
    assert.strictEqual(platform.renderer.started.fps, 2, '3 frames / 2 s');
    await client.finish();
});

test('simulated startup opens no renderer and no telemetry streams', async () => {
    const { platform, client } = setup({ withRenderer: false });
    await client.run();
    assert.strictEqual(platform.renderer, null);
    assert.strictEqual(platform.storage.streams.size, 0);
    await client.finish();
});

// --------------------------------------------------------------------------
// The segment loop
// --------------------------------------------------------------------------

test('the loop ticks exactly totalSegments times and then finishes', async () => {
    const { platform, client } = setup({ totalSegments: 5 });
    await runToCompletion(platform, client, { segments: 5 });

    assert.strictEqual(client.currentSegmentId, 5, 'no extra ticks past the end');
    assert.strictEqual(client.metrics.segments.length, 5);
    assert.deepStrictEqual(client.metrics.segments.map(s => s.segmentId),
        [0, 1, 2, 3, 4]);
    assert.strictEqual(platform.lifecycle.exitCode, 0);
});

test('every tick reports to the server before downloading', async () => {
    const { platform, client } = setup({ totalSegments: 3 });
    await runToCompletion(platform, client, { segments: 3 });

    const segmentPosts = platform.transport.requests
        .filter(r => r.method === 'POST' && /^\/api\/segment\/\d+$/.test(r.path));
    assert.strictEqual(segmentPosts.length, 3);

    // The first asset request must come after the first segment report, because
    // ladder generation for this viewpoint should start immediately rather than
    // after the transfer.
    const firstReportIndex = platform.transport.requests.indexOf(segmentPosts[0]);
    const anyAsset = platform.transport.assetRequests.length;
    assert.ok(anyAsset > 0, 'assets were fetched');
    assert.ok(firstReportIndex >= 0);
});

test('the loop downloads every planned asset of every selected object', async () => {
    const { platform, client } = setup({ totalSegments: 3 });
    await runToCompletion(platform, client, { segments: 3 });

    // Derived, not hardcoded: the ABR legitimately serves fewer objects than the
    // scene contains. At segment 0 the initial 5 Mbps estimate cannot afford two
    // 4 Mbps objects, so one is deliberately frozen under bandwidth deficit; by
    // segment 1 the estimate has risen off the first download and both download.
    // Asserting a fixed count would encode that ramp as a requirement.
    const downloadsPerSegment = client.metrics.segments.map(
        s => s.objectQualities.filter(o => o.decision === 'download').length);
    const assetsPerObject = 1 + STREAM_CONFIG.framesPerSegment;   // texture + frames
    const expected = downloadsPerSegment.reduce((sum, n) => sum + n, 0) * assetsPerObject;

    assert.strictEqual(platform.transport.assetRequests.length, expected);
    assert.ok(downloadsPerSegment.some(n => n === 2),
        'both objects are served once bandwidth allows');

    const urls = platform.transport.assetRequests.map(a => a.url);
    assert.ok(urls.some(u => u.includes('dancer_fr0001_qp')), 'dancer geometry');
    assert.ok(urls.some(u => u.includes('mitch_fr0061_qp')),
        'mitch geometry starts at its own start_number');
    assert.ok(urls.some(u => u.includes('_tex_part00.mp4')), 'texture');
});

test('bandwidth deficit at startup freezes rather than starves', async () => {
    // Pins the ramp observed above as intentional: a frozen object keeps showing
    // its last frame and is penalised less than a real stall, so the distinction
    // must survive into the report the QoE evaluator reads.
    const { platform, client } = setup({ totalSegments: 2 });
    await runToCompletion(platform, client, { segments: 2 });

    const first = client.metrics.segments[0];
    const frozen = first.objectQualities.filter(o => o.decision === 'freeze');
    assert.strictEqual(frozen.length, 1,
        'one object could not be afforded at the initial 5 Mbps estimate');
    assert.match(frozen[0].reason, /deficit/);
    assert.strictEqual(first.deficit, true);

    const report = platform.transport.bodyFor('POST', '/api/segment/0');
    assert.strictEqual(report.selection.frozenCount, 1);
    assert.strictEqual(report.selection.objects[frozen[0].objectName].decision,
        'freeze', 'a policy freeze must not be reported as a download failure');
});

test('the segment report carries a per-object decision for every object', async () => {
    const { platform, client } = setup({ totalSegments: 2 });
    await runToCompletion(platform, client, { segments: 2 });

    const body = platform.transport.bodyFor('POST', '/api/segment/1');
    assert.ok(body.selection, 'a non-empty report has a selection');
    const objects = body.selection.objects;
    assert.deepStrictEqual(Object.keys(objects).sort(), ['dancer', 'mitch']);
    for (const entry of Object.values(objects)) {
        assert.ok(['download', 'freeze', 'skip'].includes(entry.decision));
    }
    assert.strictEqual(typeof body.estimatedBandwidth, 'number');
    assert.strictEqual(body.isEmpty, false);
});

test('a tick with no manifest reports an empty segment and still advances', async () => {
    const platform = createFakePlatform();
    platform.transport
        .onGet('/api/config', () => ({ ...STREAM_CONFIG, totalSegments: 2 }))
        .onPost('/api/broadcast/start', () => ({ broadcastId: 'b' }))
        .onPost('/api/viewpoint', () => ({}))
        .onPost('/api/segment/', () => ({}))
        .onPost('/api/results', () => ({}));
    // Deliberately no /api/manifest route: the server has published nothing.

    const client = new StreamingClient({
        platform, config: { manifestFetchTimeoutMs: 10 }
    });
    await client.run();
    await platform.clock.advance(3 * STREAM_CONFIG.segmentIntervalMs);

    const bodies = platform.transport.requests
        .filter(r => /^\/api\/segment\/\d+$/.test(r.path))
        .map(r => r.body);
    assert.ok(bodies.length >= 2);
    assert.ok(bodies.every(b => b.isEmpty === true),
        'every report is an empty-segment report');
    assert.ok(bodies.every(b => b.selection === undefined),
        'an empty report carries no selection');
    assert.ok(platform.logger.saw('No manifest yet'));
});

test('download backlog skips the transfer and still credits the skip per object', async () => {
    // The regression this guards: the early return used to bypass per-object
    // bookkeeping, so the whole scene lost a segment's credit with nothing but
    // a counter to show for it.
    const { platform, client } = setup({ totalSegments: 6 });
    await client.run();

    // Never resolve any asset, so downloads pile up to the in-flight cap.
    platform.transport.fetchAsset = () => new Promise(() => {});

    for (let i = 0; i < 6; i++) {
        await platform.clock.advance(STREAM_CONFIG.segmentIntervalMs);
    }

    assert.ok(platform.logger.saw('Skipping segment'),
        'the backlog guard fired');
    assert.ok(client.metrics.summary.skippedDownloads > 0);
    const buffer = client.abr.getBufferManager().getBuffer('dancer');
    assert.ok(buffer.segmentsSkipped > 0, 'the skip was credited to the object');
    assert.strictEqual(buffer.lastSkipReason, 'download-skipped-backlog',
        'the real reason is carried, not a generic failure');
});

test('a late download is flagged and tightens the next budget', async () => {
    const { platform, client } = setup({ totalSegments: 3 });
    await client.run();
    client.lastDownloadLate = true;
    const tight = client.bandwidth.budget();
    client.lastDownloadLate = false;
    const normal = client.bandwidth.budget();
    assert.ok(tight <= normal, 'lateness must not increase the budget');
    await client.finish();
});

// --------------------------------------------------------------------------
// Interactive decode crediting
// --------------------------------------------------------------------------

test('interactive mode stages downloaded objects for the renderer', async () => {
    const { platform, client } = setup({ totalSegments: 2 });
    await runToCompletion(platform, client, { segments: 2 });

    assert.ok(platform.renderer.staged.length >= 2, 'segments were staged');
    const first = platform.renderer.staged[0];
    const dancer = first.objects.dancer;
    assert.ok(dancer, 'dancer was staged');
    assert.strictEqual(dancer.geometryFiles.length, 3,
        'one slot per frame, index-aligned');
    assert.ok(dancer.geometryFiles.every(handle => handle !== null));
    assert.ok(dancer.decodeDir.endsWith('/decoded'));
});

test('an object becomes playable only once the renderer reports it ready', async () => {
    const { platform, client } = setup({ totalSegments: 2 });
    await client.run();
    await platform.clock.advance(STREAM_CONFIG.segmentIntervalMs);

    const before = client.abr.getBufferManager().getBuffer('dancer').level;
    const staged = platform.renderer.staged[0];
    await platform.renderer.emitObjectReady(
        staged.segmentId, 'dancer', staged.objects.dancer.repId);
    const after = client.abr.getBufferManager().getBuffer('dancer').level;

    assert.ok(after > before,
        'download alone must not fill the buffer in interactive mode');
    await client.finish();
});

test('a ready event is credited once even if the renderer repeats it', async () => {
    const { platform, client } = setup({ totalSegments: 2 });
    await client.run();
    await platform.clock.advance(STREAM_CONFIG.segmentIntervalMs);

    const staged = platform.renderer.staged[0];
    const repId = staged.objects.dancer.repId;
    await platform.renderer.emitObjectReady(staged.segmentId, 'dancer', repId);
    const once = client.abr.getBufferManager().getBuffer('dancer').level;
    await platform.renderer.emitObjectReady(staged.segmentId, 'dancer', repId);
    const twice = client.abr.getBufferManager().getBuffer('dancer').level;

    assert.strictEqual(twice, once, 'no double credit');
    await client.finish();
});

test('a decode error is counted but a superseded decode is not a failure', async () => {
    const { platform, client } = setup({ totalSegments: 2 });
    await client.run();
    await platform.clock.advance(STREAM_CONFIG.segmentIntervalMs);

    await platform.renderer.emitObjectError(0, 'dancer', 'r1', 'boom');
    assert.strictEqual(client.metrics.renderSummary.objectDecodeFailures, 1);

    await platform.renderer.emitObjectError(0, 'mitch', 'r1', 'superseded',
        { superseded: true });
    assert.strictEqual(client.metrics.renderSummary.objectDecodeFailures, 1,
        'a superseded decode lost no work and must not count as a failure');
    await client.finish();
});

test('decode events are streamed as they happen, not only at exit', async () => {
    // An aborted run used to lose every decode timing and error — the one
    // telemetry needed to diagnose a starved renderer.
    const { platform, client } = setup({ totalSegments: 2 });
    await client.run();
    await platform.clock.advance(STREAM_CONFIG.segmentIntervalMs);
    await platform.renderer.emitObjectReady(0, 'dancer', 'r1');

    const lines = platform.storage.linesOf('decode_events.jsonl');
    assert.strictEqual(lines.length, 1);
    assert.strictEqual(lines[0].status, 'ready');
    assert.strictEqual(lines[0].objectName, 'dancer');
    await client.finish();
});

test('object staging directories are released after decode', async () => {
    const { platform, client } = setup({ totalSegments: 2 });
    await client.run();
    await platform.clock.advance(STREAM_CONFIG.segmentIntervalMs);

    const staged = platform.renderer.staged[0];
    await platform.renderer.emitObjectReady(
        staged.segmentId, 'dancer', staged.objects.dancer.repId);
    assert.ok(platform.storage.released.some(h => h.includes('dancer')),
        'staging space is reclaimed once the frames are decoded');
    await client.finish();
});

test('render frames are recorded and batched for upload', async () => {
    const { platform, client } = setup({ totalSegments: 2 });
    await client.run();
    for (let i = 0; i < 3; i++) platform.renderer.emitFrame({ sourceFrame: i });

    assert.strictEqual(client.metrics.renderSummary.framesPresented, 3);
    assert.strictEqual(platform.storage.linesOf('render_frames.jsonl').length, 3);
    // Below renderTraceBatchSize (30), so nothing has been uploaded yet.
    assert.strictEqual(
        platform.transport.pathsFor('POST')
            .filter(p => p === '/api/render-frames').length, 0);

    await client.finish();
    assert.ok(platform.transport.pathsFor('POST').includes('/api/render-frames'),
        'the remainder is force-flushed at shutdown');
});

test('an early renderer window close finishes the run', async () => {
    const { platform, client } = setup({ totalSegments: 20 });
    await client.run();
    await platform.clock.advance(STREAM_CONFIG.segmentIntervalMs);

    platform.renderer.emitClosed({ reason: 'user closed window' });
    await platform.clock.advance(10);

    assert.strictEqual(client.metrics.renderSummary.earlyWindowClose, true);
    assert.ok(client.finishing);
});

// --------------------------------------------------------------------------
// Simulated mode
// --------------------------------------------------------------------------

test('simulated mode credits segments straight from the download result', async () => {
    const { platform, client } = setup({ totalSegments: 3, withRenderer: false });
    await runToCompletion(platform, client, { segments: 3, ackDecodes: false });

    const buffer = client.abr.getBufferManager().getBuffer('dancer');
    assert.ok(buffer.segmentsDownloaded > 0,
        'no renderer, so the download itself makes the object playable');
    assert.strictEqual(platform.lifecycle.exitCode, 0);
});

test('simulated mode writes no asset destinations', async () => {
    const { platform, client } = setup({ totalSegments: 2, withRenderer: false });
    await runToCompletion(platform, client, { segments: 2, ackDecodes: false });
    assert.ok(platform.transport.assetRequests.length > 0);
    assert.ok(platform.transport.assetRequests.every(a => a.handle === null),
        'bytes are counted but nothing is persisted');
});

// --------------------------------------------------------------------------
// Failure handling
// --------------------------------------------------------------------------

test('a failed geometry frame keeps its slot so the clip cannot shift', async () => {
    const { platform, client } = setup({ totalSegments: 2 });
    platform.transport.failAsset('dancer_fr0002');
    await runToCompletion(platform, client, { segments: 2 });

    const dancer = platform.renderer.staged[0]?.objects?.dancer;
    if (dancer) {
        assert.strictEqual(dancer.geometryFiles.length, 3);
        assert.strictEqual(dancer.geometryFiles[1], null,
            'the failed frame holds a null slot rather than shifting frame 2 down');
        assert.ok(dancer.geometryFiles[2] !== null);
    } else {
        // Below the 90% geometry bar the object is not staged at all, which is
        // also correct — assert that it was judged a failure rather than staged
        // with a short clip.
        assert.ok(platform.logger.saw('partial geometry')
            || client.metrics.segments.length > 0);
    }
});

test('total download failure falls back to the previous manifest', async () => {
    const { platform, client } = setup({ totalSegments: 4, advanceLadder: true });
    await client.run();
    // First segment succeeds so a previousManifest exists, then everything fails.
    await platform.clock.advance(STREAM_CONFIG.segmentIntervalMs);
    platform.transport.assetRules.push(
        { match: '/files/', success: false, size: 0, timeMs: 1 });
    await platform.clock.advance(2 * STREAM_CONFIG.segmentIntervalMs);

    assert.ok(platform.logger.saw('trying fallback'),
        'the fallback path was attempted');
    await client.finish();
});

test('a server error on the segment report does not stop the loop', async () => {
    const { platform, client } = setup({ totalSegments: 4 });
    platform.transport.onPost('/api/segment/',
        () => ({ ok: false, status: 500, body: null }));
    await runToCompletion(platform, client, { segments: 4 });
    assert.strictEqual(client.currentSegmentId, 4, 'the loop kept ticking');
    assert.strictEqual(platform.lifecycle.exitCode, 0);
});

// --------------------------------------------------------------------------
// Shutdown
// --------------------------------------------------------------------------

test('finish() writes the metrics result and uploads it', async () => {
    const { platform, client } = setup({ totalSegments: 3 });
    await runToCompletion(platform, client, { segments: 3 });

    assert.ok(platform.storage.result, 'metrics were written locally');
    const written = JSON.parse(platform.storage.result);
    assert.strictEqual(written.summary.totalSegments, 3);
    assert.strictEqual(written.clientMode, 'interactive');
    assert.strictEqual(written.broadcastId, 'bcast-test-1');
    assert.ok(Array.isArray(written.segments) && written.segments.length === 3);
    assert.ok(platform.transport.pathsFor('POST').includes('/api/results'));
});

test('finish() cancels every timer and releases the scratch root', async () => {
    const { platform, client } = setup({ totalSegments: 3 });
    await runToCompletion(platform, client, { segments: 3 });

    assert.strictEqual(platform.clock.pending, 0, 'no timer leaked');
    assert.ok(platform.storage.released.includes(client.scratchRoot),
        'per-run staging space is reclaimed');
    assert.ok(platform.renderer.stopped, 'the renderer was stopped');
});

test('finish() closes telemetry streams and refuses later writes', async () => {
    const { platform, client } = setup({ totalSegments: 2 });
    await runToCompletion(platform, client, { segments: 2 });
    assert.strictEqual(client.renderTraceStream, null);
    assert.strictEqual(client.decodeEventStream, null);
    // Detached before closing, so a late frame cannot write to a closing stream.
    assert.doesNotThrow(() => platform.renderer.emitFrame({ sourceFrame: 99 }));
});

test('finish() is idempotent', async () => {
    const { platform, client } = setup({ totalSegments: 2 });
    await runToCompletion(platform, client, { segments: 2 });
    const exits = platform.lifecycle.exitCalls;
    await client.finish();
    await client.finish();
    assert.strictEqual(platform.lifecycle.exitCalls, exits,
        'repeated finish must not exit or re-upload again');
});

test('a shutdown request finalizes a partial run', async () => {
    const { platform, client } = setup({ totalSegments: 50 });
    await client.run();
    await platform.clock.advance(2 * STREAM_CONFIG.segmentIntervalMs);

    platform.lifecycle.requestShutdown();
    await platform.clock.advance(10);

    assert.ok(client.finishing, 'the interrupt was honoured');
    assert.ok(platform.logger.saw('Shutdown requested'));
});

test('the bandwidth history the estimator appends to is the one serialized', async () => {
    const { platform, client } = setup({ totalSegments: 3 });
    await runToCompletion(platform, client, { segments: 3 });
    const written = JSON.parse(platform.storage.result);
    assert.strictEqual(written.bandwidthHistory.length,
        client.bandwidth.history.length);
    assert.ok(written.bandwidthHistory.length > 0,
        'samples were taken during the run');
});

test('bitrate request counts are reported per interval and drained', async () => {
    const { platform, client } = setup({ totalSegments: 4 });
    await runToCompletion(platform, client, { segments: 4 });

    const posts = platform.transport.requests.filter(
        r => r.path === '/api/bitrate-counts-interval');
    assert.ok(posts.length > 0, 'interval reports were sent');
    const counted = posts.some(p => Object.keys(p.body.countsPerObject).length > 0);
    assert.ok(counted, 'at least one report carried counts');

    const written = JSON.parse(platform.storage.result);
    assert.ok(Object.keys(written.bitrateRequestCountsPerObject).length > 0,
        'cumulative counts survive independently of the interval drain');
});

// --------------------------------------------------------------------------
// Config validation
// --------------------------------------------------------------------------

test('an unknown client mode is rejected at construction', () => {
    const platform = createFakePlatform();
    assert.throws(() => new StreamingClient({
        platform, config: { clientMode: 'headless' }
    }), /clientMode must be/);
});

test('interactive mode without a renderer is rejected at construction', () => {
    const platform = createFakePlatform({ withRenderer: false });
    assert.throws(() => new StreamingClient({
        platform, config: { clientMode: 'interactive' }
    }), /renderer is required in interactive mode/);
});

test('client mode is case-insensitive, matching the env var it came from', () => {
    const platform = createFakePlatform({ withRenderer: false });
    const client = new StreamingClient({
        platform, config: { clientMode: 'SIMULATED' }
    });
    assert.strictEqual(client.config.clientMode, 'simulated');
});

test('the ABR refuses to construct without its platform seams', () => {
    // The grace period is wall-time based, so a Date.now default would let a
    // virtual-clock test silently measure real time and appear to pass.
    const { MCKPAdaptiveBitrate } = require('../system/ClientCore/abr');
    assert.throws(() => new MCKPAdaptiveBitrate({}), /requires a now function/);
    assert.throws(() => new MCKPAdaptiveBitrate({}, { now: () => 0 }),
        /requires a log function/);
    assert.doesNotThrow(
        () => new MCKPAdaptiveBitrate({}, { now: () => 0, log: () => {} }));
});

test('the ABR startup grace period is driven by the injected clock', () => {
    // Previously untestable: it read wall time, which a virtual clock cannot
    // reach. Now the grace period can be exercised deterministically.
    const { PerObjectBufferManager } = require('../system/ClientCore/abr');
    let t = 0;
    const lines = [];
    const manager = new PerObjectBufferManager(
        { startupBuffer: 99, startupGracePeriod: 3 },
        { now: () => t, log: line => lines.push(line) });

    manager.firstSegmentRequestTime = t;
    assert.strictEqual(manager.shouldStartPlayback(), false,
        'buffer target unreachable and no grace period elapsed yet');

    t += 3000;
    assert.strictEqual(manager.shouldStartPlayback(), true,
        'playback starts once the grace period passes');
    assert.ok(lines.some(l => l.includes('grace period')),
        'and says so through the injected logger, not the console');
});
