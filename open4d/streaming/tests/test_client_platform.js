'use strict';

/**
 * Tests for the ClientPlatform contract and its in-memory implementation.
 *
 * The validator matters more than it looks: a browser adapter missing one
 * method would otherwise surface as `undefined is not a function` mid-run,
 * producing a truncated metrics file that looks like a real result.
 *
 * Run: node --test tests/test_client_platform.js
 */

const assert = require('assert');
const test = require('node:test');

const {
    PLATFORM_CONTRACT, validatePlatform, platformCapabilities
} = require('../system/ClientCore/platform');
const {
    createFakePlatform, FakeClock, FakeTransport, FakeStorage
} = require('../system/ClientCore/testing/fake-platform');

// --------------------------------------------------------------------------
// Contract shape
// --------------------------------------------------------------------------

test('contract covers exactly the seven audited capabilities', () => {
    assert.deepStrictEqual(platformCapabilities(), [
        'transport', 'storage', 'viewpoints', 'clock', 'logger', 'renderer', 'lifecycle'
    ]);
});

test('contract is frozen so a caller cannot widen it at runtime', () => {
    assert.throws(() => { PLATFORM_CONTRACT.transport = {}; }, TypeError);
    assert.throws(() => { PLATFORM_CONTRACT.transport.methods.push('x'); }, TypeError);
});

// --------------------------------------------------------------------------
// validatePlatform
// --------------------------------------------------------------------------

test('the fake platform satisfies the contract', () => {
    const platform = createFakePlatform();
    assert.strictEqual(validatePlatform(platform, { requireRenderer: true }), platform,
        'returns the same object for chaining');
});

test('validate rejects a non-object', () => {
    for (const bad of [null, undefined, 42, 'platform', true]) {
        assert.throws(() => validatePlatform(bad), /must be an object/);
    }
});

test('validate reports every problem at once, not just the first', () => {
    assert.throws(() => validatePlatform({
        transport: { getJson: () => {} },        // missing 3 methods
        storage: {},                             // missing 6
        clock: { now: () => 0 }                  // missing 3
        // viewpoints, logger, lifecycle absent entirely
    }), err => {
        const message = err.message;
        assert.match(message, /transport\.postJson/);
        assert.match(message, /transport\.assetUrl/);
        assert.match(message, /transport\.fetchAsset/);
        assert.match(message, /storage\.createScratch/);
        assert.match(message, /clock\.every/);
        assert.match(message, /missing capability: viewpoints/);
        assert.match(message, /missing capability: logger/);
        assert.match(message, /missing capability: lifecycle/);
        // Enough problems that fixing them one run at a time would be miserable.
        assert.ok(message.split('\n').length > 8, 'all problems in one message');
        return true;
    });
});

test('validate rejects a method that is present but not callable', () => {
    const platform = createFakePlatform();
    platform.transport.fetchAsset = 'not a function';
    assert.throws(() => validatePlatform(platform), /transport\.fetchAsset must be a function/);
});

test('validate rejects a capability that is not an object', () => {
    const platform = createFakePlatform();
    platform.clock = 'tick';
    assert.throws(() => validatePlatform(platform), /clock must be an object, got string/);
});

test('validate checks properties, not only methods', () => {
    const platform = createFakePlatform();
    delete platform.renderer.latestCamera;
    assert.throws(() => validatePlatform(platform, { requireRenderer: true }),
        /renderer\.latestCamera must be present/);
});

test('a null renderer is valid in simulated mode and fatal in interactive mode', () => {
    const platform = createFakePlatform({ withRenderer: false });
    assert.strictEqual(platform.renderer, null);
    assert.doesNotThrow(() => validatePlatform(platform));
    assert.throws(() => validatePlatform(platform, { requireRenderer: true }),
        /renderer is required in interactive mode/);
});

// --------------------------------------------------------------------------
// FakeClock — the substrate for deterministic segment-loop tests
// --------------------------------------------------------------------------

test('clock advances virtual time without sleeping', async () => {
    const clock = new FakeClock(1000);
    assert.strictEqual(clock.now(), 1000);
    await clock.advance(5000);
    assert.strictEqual(clock.now(), 6000);
});

test('clock fires a repeating timer once per interval', async () => {
    const clock = new FakeClock();
    const fired = [];
    clock.every(2000, () => fired.push(clock.now()));
    await clock.advance(7000);
    assert.strictEqual(fired.length, 3, '2000, 4000, 6000');
    assert.deepStrictEqual(
        fired.map(t => t - fired[0]), [0, 2000, 4000]);
});

test('clock stops a cancelled timer', async () => {
    const clock = new FakeClock();
    let count = 0;
    const handle = clock.every(1000, () => { count++; });
    await clock.advance(2500);
    assert.strictEqual(count, 2);
    clock.cancel(handle);
    await clock.advance(10_000);
    assert.strictEqual(count, 2, 'no further firings');
    assert.strictEqual(clock.pending, 0, 'nothing left armed');
});

test('clock cancel is idempotent and tolerates unknown handles', () => {
    const clock = new FakeClock();
    const handle = clock.every(1000, () => {});
    clock.cancel(handle);
    assert.doesNotThrow(() => clock.cancel(handle));
    assert.doesNotThrow(() => clock.cancel(9999));
});

test('clock resolves delay() when time passes it', async () => {
    const clock = new FakeClock();
    let resolved = false;
    const pending = clock.delay(500).then(() => { resolved = true; });
    await clock.advance(499);
    assert.strictEqual(resolved, false);
    await clock.advance(1);
    await pending;
    assert.strictEqual(resolved, true);
});

test('clock fires timers in time order across mixed intervals', async () => {
    const clock = new FakeClock();
    const order = [];
    clock.every(300, () => order.push(`a@${clock.now()}`));
    clock.every(500, () => order.push(`b@${clock.now()}`));
    await clock.advance(1000);
    const times = order.map(entry => Number(entry.split('@')[1]));
    assert.deepStrictEqual(times, [...times].sort((x, y) => x - y),
        'callbacks fire in chronological order');
});

test('clock drains microtasks between timer callbacks', async () => {
    // A segment tick awaits a manifest fetch; the next tick must not fire until
    // the previous one has settled, or the test would see interleaved state.
    const clock = new FakeClock();
    const order = [];
    clock.every(1000, async () => {
        order.push('tick-start');
        await Promise.resolve();
        order.push('tick-end');
    });
    await clock.advance(2000);
    assert.deepStrictEqual(order,
        ['tick-start', 'tick-end', 'tick-start', 'tick-end']);
});

test('clock rejects a non-positive interval', () => {
    const clock = new FakeClock();
    assert.throws(() => clock.every(0, () => {}), RangeError);
    assert.throws(() => clock.every(-5, () => {}), RangeError);
});

// --------------------------------------------------------------------------
// FakeTransport
// --------------------------------------------------------------------------

test('transport routes exact paths and records requests', async () => {
    const transport = new FakeTransport();
    transport.onGet('/api/config', () => ({ totalSegments: 20 }));
    const result = await transport.getJson('/api/config');
    assert.deepStrictEqual(result, { ok: true, status: 200, body: { totalSegments: 20 } });
    assert.deepStrictEqual(transport.pathsFor('GET'), ['/api/config']);
});

test('transport falls back to prefix matching for parameterized paths', async () => {
    const transport = new FakeTransport();
    transport.onPost('/api/segment/', body => ({ latestManifestSegId: body.segmentId }));
    const result = await transport.postJson('/api/segment/17', { segmentId: 17 });
    assert.strictEqual(result.body.latestManifestSegId, 17);
});

test('transport returns 404 rather than throwing for an unrouted path', async () => {
    const transport = new FakeTransport();
    const result = await transport.postJson('/api/nope', {});
    assert.deepStrictEqual(result, { ok: false, status: 404, body: null });
});

test('transport lets a handler return an explicit non-ok result', async () => {
    const transport = new FakeTransport();
    transport.onGet('/api/manifest', () => ({ ok: false, status: 503, body: null }));
    const result = await transport.getJson('/api/manifest');
    assert.strictEqual(result.status, 503);
    assert.strictEqual(result.ok, false);
});

test('transport assetUrl matches the real client rewriting rules', () => {
    const transport = new FakeTransport({ baseUrl: 'http://h:3000' });
    assert.strictEqual(transport.assetUrl(null), null);
    assert.strictEqual(transport.assetUrl('https://cdn/x.drc'), 'https://cdn/x.drc');
    assert.strictEqual(transport.assetUrl('/files/media/a/b.mp4'),
        'http://h:3000/files/media/a/b.mp4');
    assert.strictEqual(transport.assetUrl('/data/DataDrive/files/media/a/b.drc'),
        'http://h:3000/files/media/a/b.drc');
    assert.strictEqual(transport.assetUrl('nonsense'), null);
});

test('transport fetchAsset succeeds by default and never rejects on failure', async () => {
    const transport = new FakeTransport();
    const ok = await transport.fetchAsset('http://h/files/a.drc', '/scratch/a.drc');
    assert.strictEqual(ok.success, true);
    assert.ok(ok.size > 0);

    transport.failAsset('bad');
    const bad = await transport.fetchAsset('http://h/files/bad.drc', '/scratch/bad.drc');
    assert.strictEqual(bad.success, false);
    assert.strictEqual(bad.size, 0, 'a failed asset contributes no bytes');
});

test('transport records the handle each asset was fetched into', async () => {
    const transport = new FakeTransport();
    await transport.fetchAsset('http://h/files/a.drc', '/scratch/seg0/a.drc');
    assert.deepStrictEqual(transport.assetRequests, [
        { url: 'http://h/files/a.drc', handle: '/scratch/seg0/a.drc' }
    ]);
});

test('transport accepts a null handle for count-only mode', async () => {
    const transport = new FakeTransport();
    const result = await transport.fetchAsset('http://h/files/a.drc', null);
    assert.strictEqual(result.success, true, 'simulated mode still measures bytes');
    assert.strictEqual(transport.assetRequests[0].handle, null);
});

test('transport bodyFor returns the most recent body for a path', async () => {
    const transport = new FakeTransport();
    transport.onPost('/api/viewpoint', () => ({}));
    await transport.postJson('/api/viewpoint', { segId: 0 });
    await transport.postJson('/api/viewpoint', { segId: 1 });
    assert.deepStrictEqual(transport.bodyFor('POST', '/api/viewpoint'), { segId: 1 });
});

// --------------------------------------------------------------------------
// FakeStorage
// --------------------------------------------------------------------------

test('storage composes handles and skips null parts', () => {
    const storage = new FakeStorage();
    assert.strictEqual(
        storage.handle('/scratch', 'segment_0007', 'dancer', 'r_res960'),
        '/scratch/segment_0007/dancer/r_res960');
    assert.strictEqual(storage.handle('/scratch', null, 'dancer'), '/scratch/dancer');
});

test('storage mints a distinct scratch root per run', async () => {
    const storage = new FakeStorage();
    const first = await storage.createScratch();
    const second = await storage.createScratch();
    assert.notStrictEqual(first, second);
    assert.strictEqual(storage.scratch, second);
});

test('storage release tolerates a handle that was never created', async () => {
    // Contract requirement: an object whose download failed may never have had
    // a directory, and the cleanup path must not throw on it.
    const storage = new FakeStorage();
    await assert.doesNotReject(() => storage.release('/scratch/never-existed'));
    assert.deepStrictEqual(storage.released, ['/scratch/never-existed']);
});

test('storage keeps named text artifacts and the run result apart', async () => {
    const storage = new FakeStorage();
    await storage.writeText('menu.json', '{"a":1}');
    await storage.writeResult('{"summary":{}}');
    assert.strictEqual(storage.files.get('menu.json'), '{"a":1}');
    assert.strictEqual(storage.result, '{"summary":{}}');
});

test('storage append streams collect lines and reject writes after close', async () => {
    const storage = new FakeStorage();
    const stream = await storage.openAppendStream('render_frames.jsonl');
    stream.write(JSON.stringify({ frame: 1 }));
    stream.write(JSON.stringify({ frame: 2 }));
    assert.deepStrictEqual(storage.linesOf('render_frames.jsonl'),
        [{ frame: 1 }, { frame: 2 }]);
    await stream.close();
    assert.throws(() => stream.write('{}'), /write after close/);
});

// --------------------------------------------------------------------------
// Renderer + lifecycle
// --------------------------------------------------------------------------

test('renderer records staging and playback state separately', async () => {
    const { renderer } = createFakePlatform();
    await renderer.start({ width: 1280, height: 720 });
    renderer.stageSegment(3, { dancer: { state: 'download' } });
    renderer.setPlaybackState(2, { dancer: { state: 'OK', bufferLevel: 1.5 } });
    assert.strictEqual(renderer.started.width, 1280);
    assert.deepStrictEqual(renderer.staged, [
        { segmentId: 3, objects: { dancer: { state: 'download' } } }
    ]);
    assert.strictEqual(renderer.playbackStates[0].segmentId, 2);
});

test('renderer drives the decode-completion callbacks the core credits from', () => {
    const { renderer } = createFakePlatform();
    const events = [];
    renderer.setCallbacks({ onObjectReady: e => events.push(e) });
    renderer.emitObjectReady(4, 'dancer', 'r_res960_crf24_qp7');
    renderer.emitObjectError(4, 'mitch', 'r_res480_crf30_qp7');
    renderer.emitObjectError(5, 'thomas', 'r1', 'superseded', { superseded: true });

    assert.deepStrictEqual(events.map(e => e.type),
        ['object_ready', 'object_error', 'object_error']);
    assert.strictEqual(events[0].objectName, 'dancer');
    assert.strictEqual(events[2].superseded, true,
        'superseded is not a failure and the core must be able to tell');
});

test('renderer camera movement is observable through latestCamera', () => {
    const { renderer } = createFakePlatform();
    renderer.moveCamera({ position: [1, 2, 3] });
    assert.deepStrictEqual(renderer.latestCamera, { position: [1, 2, 3] });
});

test('lifecycle records the exit code and fires shutdown handlers', () => {
    const { lifecycle } = createFakePlatform();
    let finalized = 0;
    lifecycle.onShutdownRequest(() => { finalized++; });
    lifecycle.requestShutdown();
    assert.strictEqual(finalized, 1, 'an interrupted run must still finalize');
    lifecycle.exit(0);
    assert.strictEqual(lifecycle.exitCode, 0);
    assert.strictEqual(lifecycle.exitCalls, 1);
});

test('viewpoints rejects when no pose is available', async () => {
    // A run with no initial pose would solve the first ladder against a default
    // camera, silently invalidating the viewpoint-aware comparison.
    const platform = createFakePlatform({ viewpoints: [] });
    await assert.rejects(() => platform.viewpoints.list(), /no viewpoints/);
});

test('logger captures level, line and structured data', () => {
    const { logger } = createFakePlatform();
    logger.emit('WARN', '[seg:3][WARN][DOWNLOAD] Segment 3 download LATE',
        { downloadTimeMs: 2400 });
    assert.strictEqual(logger.at('WARN').length, 1);
    assert.ok(logger.saw('download LATE'));
    assert.strictEqual(logger.at('WARN')[0].data.downloadTimeMs, 2400);
});
