'use strict';

/**
 * Tests for the browser platform adapter, run in Node against stubbed browser
 * globals.
 *
 * This is why browser-platform.js takes an injected `env` instead of capturing
 * globals: the cache policy, the OPFS paths and the non-blocking telemetry
 * buffer are all verifiable here, without a browser or a GPU. What is NOT
 * covered is the WebGL/WebCodecs renderer, which needs a real GPU.
 *
 * Run: node --test tests/test_browser_platform.js
 */

const assert = require('assert');
const test = require('node:test');

const {
    createBrowserPlatform, createTransport, createStorage, createViewpoints,
    createClock, createLogger, createLifecycle,
    MemoryAssetStore, OpfsAssetStore, BufferedLineSink
} = require('../system/WebClient/src/browser-platform');
const { validatePlatform } = require('../system/ClientCore/platform');

// --------------------------------------------------------------------------
// A stub browser environment.
// --------------------------------------------------------------------------
function stubEnv({ routes = {}, assets = {}, failAssets = [] } = {}) {
    const calls = [];
    let clockValue = 1_000_000;
    const timers = new Map();
    let timerSeq = 0;

    const env = {
        calls,
        logged: [],
        now: () => clockValue,
        advance: ms => { clockValue += ms; },
        console: { log: (...args) => env.logged.push(args) },
        setInterval: (fn, ms) => {
            const id = ++timerSeq;
            timers.set(id, { fn, ms });
            return id;
        },
        clearInterval: id => timers.delete(id),
        setTimeout: fn => { fn(); return ++timerSeq; },
        timers,
        /** Fire every registered interval once. */
        tickIntervals: async () => {
            for (const { fn } of [...timers.values()]) await fn();
        },
        async fetch(url, options = {}) {
            calls.push({ url, options });
            const asset = Object.keys(assets).find(key => url.includes(key));
            if (failAssets.some(key => url.includes(key))) {
                return { ok: false, status: 503, json: async () => null };
            }
            if (asset !== undefined) {
                const bytes = assets[asset];
                return {
                    ok: true,
                    status: 200,
                    arrayBuffer: async () => new Uint8Array(bytes).buffer,
                    json: async () => { throw new Error('not json'); }
                };
            }
            const path = url.replace(/^https?:\/\/[^/]+/, '');
            const route = routes[path]
                || Object.entries(routes).find(([p]) => path.startsWith(p))?.[1];
            if (!route) return { ok: false, status: 404, json: async () => null };
            const body = typeof route === 'function' ? route(options) : route;
            return { ok: true, status: 200, json: async () => body };
        }
    };
    return env;
}

const SERVER = 'http://server:3000';

// --------------------------------------------------------------------------
// Contract compliance
// --------------------------------------------------------------------------

test('the browser platform satisfies the ClientPlatform contract', async () => {
    const platform = await createBrowserPlatform({
        serverUrl: SERVER, initialPose: { objects: {} }, env: stubEnv()
    });
    assert.doesNotThrow(() => validatePlatform(platform));
});

test('a renderer passed in is used, and interactive mode requires one', async () => {
    const env = stubEnv();
    const bare = await createBrowserPlatform({
        serverUrl: SERVER, initialPose: {}, env
    });
    assert.strictEqual(bare.renderer, null, 'simulated mode by default');
    assert.throws(() => validatePlatform(bare, { requireRenderer: true }),
        /renderer is required/);

    const fakeRenderer = {
        start: async () => {}, stop: async () => {},
        stageSegment: () => {}, setPlaybackState: () => {},
        setCallbacks: () => {}, latestCamera: null
    };
    const withRenderer = await createBrowserPlatform({
        serverUrl: SERVER, initialPose: {}, renderer: fakeRenderer, env
    });
    assert.doesNotThrow(
        () => validatePlatform(withRenderer, { requireRenderer: true }));
});

test('createBrowserPlatform rejects a missing serverUrl or fetch', async () => {
    await assert.rejects(() => createBrowserPlatform({ env: stubEnv() }),
        /serverUrl is required/);
    await assert.rejects(
        () => createBrowserPlatform({ serverUrl: SERVER, env: { fetch: null } }),
        /no fetch/);
});

// --------------------------------------------------------------------------
// Transport — the cache policy is a research-validity requirement
// --------------------------------------------------------------------------

test('every media fetch is uncacheable', async () => {
    // A browser silently serving a segment from its HTTP cache turns a
    // shaped-bandwidth measurement into fiction. This is the same reason
    // decode_cache.py caches decodes but never caches downloads.
    const env = stubEnv({ assets: { 'frame.drc': [1, 2, 3, 4] } });
    const store = new MemoryAssetStore();
    const transport = createTransport({ serverUrl: SERVER, assetStore: store, env });

    await transport.fetchAsset(`${SERVER}/files/frame.drc`, 'run-1/a.drc');
    assert.strictEqual(env.calls.length, 1);
    assert.strictEqual(env.calls[0].options.cache, 'no-store',
        'media MUST NOT be served from the HTTP cache');
});

test('every API call is uncacheable too', async () => {
    // The manifest changes every segment; a cached menu would pin the client to
    // a stale ladder without any error.
    const env = stubEnv({ routes: { '/api/manifest': { segment: { t: 3 } } } });
    const transport = createTransport({
        serverUrl: SERVER, assetStore: new MemoryAssetStore(), env
    });
    await transport.getJson('/api/manifest');
    await transport.postJson('/api/viewpoint', { segId: 0 });
    assert.ok(env.calls.every(c => c.options.cache === 'no-store'));
});

test('getJson and postJson return the contract HttpResult shape', async () => {
    const env = stubEnv({
        routes: {
            '/api/config': { totalSegments: 20 },
            '/api/segment/': () => ({ latestManifestSegId: 4 })
        }
    });
    const transport = createTransport({
        serverUrl: SERVER, assetStore: new MemoryAssetStore(), env
    });

    assert.deepStrictEqual(await transport.getJson('/api/config'),
        { ok: true, status: 200, body: { totalSegments: 20 } });
    const posted = await transport.postJson('/api/segment/4', { segmentId: 4 });
    assert.strictEqual(posted.body.latestManifestSegId, 4);
    assert.deepStrictEqual(await transport.getJson('/api/nope'),
        { ok: false, status: 404, body: null });
});

test('postJson sends JSON with a content type, and omits a body when absent', async () => {
    const env = stubEnv({ routes: { '/api/x': {} } });
    const transport = createTransport({
        serverUrl: SERVER, assetStore: new MemoryAssetStore(), env
    });
    await transport.postJson('/api/x', { a: 1 });
    assert.strictEqual(env.calls[0].options.body, '{"a":1}');
    assert.strictEqual(env.calls[0].options.headers['Content-Type'], 'application/json');

    await transport.getJson('/api/x');
    assert.strictEqual(env.calls[1].options.body, undefined);
});

test('a non-JSON response body is not an error', async () => {
    // Telemetry endpoints answer with an empty body; treating that as a failure
    // would spam the log on every render-frame upload.
    const env = stubEnv({ assets: { 'anything': [1] } });
    const transport = createTransport({
        serverUrl: SERVER, assetStore: new MemoryAssetStore(), env
    });
    const res = await transport.postJson('/anything', {});
    assert.strictEqual(res.ok, true);
    assert.strictEqual(res.body, null);
});

test('assetUrl matches the Node adapter rewriting rules exactly', () => {
    const transport = createTransport({
        serverUrl: SERVER, assetStore: new MemoryAssetStore(), env: stubEnv()
    });
    assert.strictEqual(transport.assetUrl(null), null);
    assert.strictEqual(transport.assetUrl('https://cdn/x.drc'), 'https://cdn/x.drc');
    assert.strictEqual(transport.assetUrl('/files/media/a/b.mp4'),
        `${SERVER}/files/media/a/b.mp4`);
    assert.strictEqual(transport.assetUrl('/mnt/DataDrive/files/media/a/b.drc'),
        `${SERVER}/files/media/a/b.drc`);
    assert.strictEqual(transport.assetUrl('nonsense'), null);
});

test('fetchAsset stores bytes under its handle and reports the size', async () => {
    const env = stubEnv({ assets: { 'a.drc': [1, 2, 3, 4, 5] } });
    const store = new MemoryAssetStore();
    const transport = createTransport({ serverUrl: SERVER, assetStore: store, env });

    const result = await transport.fetchAsset(`${SERVER}/files/a.drc`, 'run-1/a.drc');
    assert.strictEqual(result.success, true);
    assert.strictEqual(result.size, 5);
    assert.strictEqual(store.get('run-1/a.drc').byteLength, 5,
        'the renderer reads bytes back out by handle');
});

test('fetchAsset with a null handle counts bytes but keeps nothing', async () => {
    const env = stubEnv({ assets: { 'a.drc': [1, 2, 3] } });
    const store = new MemoryAssetStore();
    const transport = createTransport({ serverUrl: SERVER, assetStore: store, env });

    const result = await transport.fetchAsset(`${SERVER}/files/a.drc`, null);
    assert.strictEqual(result.size, 3, 'simulated mode still measures the link');
    assert.strictEqual(store.buffers.size, 0);
});

test('fetchAsset resolves rather than rejecting on failure', async () => {
    // A failed asset is normal and is judged by download-plan's 90% rules, not
    // by an exception unwinding the segment.
    const env = stubEnv({ assets: { 'bad.drc': [1] }, failAssets: ['bad.drc'] });
    const transport = createTransport({
        serverUrl: SERVER, assetStore: new MemoryAssetStore(), env
    });
    const result = await transport.fetchAsset(`${SERVER}/files/bad.drc`, 'h');
    assert.strictEqual(result.success, false);
    assert.strictEqual(result.size, 0);
    assert.match(result.error, /HTTP 503/);
});

test('fetchAsset resolves when the network throws outright', async () => {
    const env = stubEnv();
    env.fetch = async () => { throw new Error('connection reset'); };
    const transport = createTransport({
        serverUrl: SERVER, assetStore: new MemoryAssetStore(), env
    });
    const result = await transport.fetchAsset(`${SERVER}/files/a.drc`, 'h');
    assert.strictEqual(result.success, false);
    assert.match(result.error, /connection reset/);
});

// --------------------------------------------------------------------------
// Asset stores
// --------------------------------------------------------------------------

test('memory store releases a whole object-segment prefix', async () => {
    const store = new MemoryAssetStore();
    await store.put('run-1/segment_0000/dancer/rep/geometry_0000.drc', new ArrayBuffer(10));
    await store.put('run-1/segment_0000/dancer/rep/geometry_0001.drc', new ArrayBuffer(10));
    await store.put('run-1/segment_0000/mitch/rep/geometry_0000.drc', new ArrayBuffer(10));

    await store.releasePrefix('run-1/segment_0000/dancer');
    assert.strictEqual(store.buffers.size, 1, 'only dancer was dropped');
    assert.ok(store.has('run-1/segment_0000/mitch/rep/geometry_0000.drc'));
});

test('memory store prefix release does not match a sibling by name prefix', async () => {
    const store = new MemoryAssetStore();
    await store.put('run-1/dancer2/a.drc', new ArrayBuffer(4));
    await store.releasePrefix('run-1/dancer');
    assert.strictEqual(store.buffers.size, 1,
        'dancer2 must survive a release of dancer');
});

test('memory store reports its footprint', async () => {
    const store = new MemoryAssetStore();
    await store.put('a', new ArrayBuffer(1000));
    await store.put('b', new ArrayBuffer(2000));
    assert.strictEqual(store.byteLength, 3000);
});

test('opfs store round-trips a nested handle and tolerates a missing release', async () => {
    // Minimal OPFS stub: nested directory handles over Maps.
    function directory() {
        const dirs = new Map();
        const files = new Map();
        return {
            dirs, files,
            async getDirectoryHandle(name, opts = {}) {
                if (!dirs.has(name)) {
                    if (!opts.create) throw new Error('NotFoundError');
                    dirs.set(name, directory());
                }
                return dirs.get(name);
            },
            async getFileHandle(name, opts = {}) {
                if (!files.has(name)) {
                    if (!opts.create) throw new Error('NotFoundError');
                    files.set(name, { data: null });
                }
                const entry = files.get(name);
                return {
                    async createWritable() {
                        return {
                            write: async buffer => { entry.data = buffer; },
                            close: async () => {}
                        };
                    },
                    async getFile() {
                        return { arrayBuffer: async () => entry.data };
                    }
                };
            },
            async removeEntry(name) {
                if (!dirs.delete(name) && !files.delete(name)) {
                    throw new Error('NotFoundError');
                }
            }
        };
    }

    const root = directory();
    const env = { navigator: { storage: { getDirectory: async () => root } } };
    const store = await OpfsAssetStore.create(env, 'vs4d-client');

    const payload = new Uint8Array([9, 8, 7]).buffer;
    await store.put('run-1/segment_0000/dancer/geometry_0000.drc', payload);
    const read = await store.get('run-1/segment_0000/dancer/geometry_0000.drc');
    assert.strictEqual(new Uint8Array(read).length, 3);

    assert.strictEqual(await store.get('run-1/missing.drc'), null,
        'a missing asset reads back as null, not a throw');
    await assert.doesNotReject(() => store.releasePrefix('run-1/never-existed'),
        'release must tolerate a handle that was never created');
});

// --------------------------------------------------------------------------
// Storage capability
// --------------------------------------------------------------------------

test('storage composes handles and skips nulls', () => {
    const storage = createStorage({
        assetStore: new MemoryAssetStore(), env: stubEnv()
    });
    assert.strictEqual(
        storage.handle('run-1', 'segment_0007', 'dancer', 'rep'),
        'run-1/segment_0007/dancer/rep');
    assert.strictEqual(storage.handle('run-1', null, 'dancer'), 'run-1/dancer');
});

test('storage surfaces the result and the manifest as page artifacts', async () => {
    const offered = [];
    const storage = createStorage({
        assetStore: new MemoryAssetStore(),
        env: stubEnv(),
        onArtifact: (name, text) => offered.push(name)
    });
    await storage.writeText('menu.json', '{"a":1}');
    await storage.writeResult('{"summary":{}}');

    assert.strictEqual(storage.artifacts.get('menu.json'), '{"a":1}');
    assert.strictEqual(storage.artifacts.get('metrics.json'), '{"summary":{}}');
    assert.deepStrictEqual(offered, ['menu.json', 'metrics.json'],
        'the page is told, since a browser cannot write a file unprompted');
});

test('storage release drops the asset bytes for that handle', async () => {
    const store = new MemoryAssetStore();
    await store.put('run-1/dancer/a.drc', new ArrayBuffer(8));
    const storage = createStorage({ assetStore: store, env: stubEnv() });
    await storage.release('run-1/dancer');
    assert.strictEqual(store.buffers.size, 0);
});

// --------------------------------------------------------------------------
// Telemetry buffering — write() must not block
// --------------------------------------------------------------------------

test('append stream writes without touching storage on the hot path', async () => {
    // 30 Hz render telemetry: write() must be one array push. In Node a
    // synchronous write per frame stalled the event loop, filled the renderer's
    // stdout pipe, and blocked the loop calling poll_events().
    const env = stubEnv();
    let flushes = 0;
    const sink = new BufferedLineSink({
        name: 't.jsonl', env, flush: () => { flushes++; }
    });
    for (let i = 0; i < 100; i++) sink.write(`{"frame":${i}}`);
    assert.strictEqual(flushes, 0, 'no flush during the burst');
    assert.strictEqual(sink.lines.length, 100);

    await env.tickIntervals();
    assert.strictEqual(flushes, 1, 'one batched flush on the interval');
    await sink.close();
});

test('append stream flushes the remainder on close and then refuses writes', async () => {
    const env = stubEnv();
    const batches = [];
    const sink = new BufferedLineSink({
        name: 't.jsonl', env, flush: batch => batches.push(batch.length)
    });
    sink.write('a');
    sink.write('b');
    await sink.close();
    assert.deepStrictEqual(batches, [2]);
    assert.throws(() => sink.write('c'), /write after close/);
});

test('append stream close cancels its flush timer', async () => {
    const env = stubEnv();
    const sink = new BufferedLineSink({ name: 't.jsonl', env, flush: () => {} });
    assert.strictEqual(env.timers.size, 1);
    await sink.close();
    assert.strictEqual(env.timers.size, 0, 'no timer leaked');
});

test('a flush failure never takes the run down', async () => {
    const env = stubEnv();
    const sink = new BufferedLineSink({
        name: 't.jsonl', env, flush: () => { throw new Error('quota exceeded'); }
    });
    sink.write('a');
    await assert.doesNotReject(() => sink.close());
});

test('storage append streams accumulate downloadable text', async () => {
    const env = stubEnv();
    const storage = createStorage({
        assetStore: new MemoryAssetStore(), env
    });
    const stream = await storage.openAppendStream('render_frames.jsonl');
    stream.write('{"frame":1}');
    stream.write('{"frame":2}');
    await stream.close();
    assert.strictEqual(storage.artifacts.get('render_frames.jsonl'),
        '{"frame":1}\n{"frame":2}\n');
});

// --------------------------------------------------------------------------
// Viewpoints
// --------------------------------------------------------------------------

test('an injected initial pose short-circuits the fetch', async () => {
    const env = stubEnv();
    const viewpoints = createViewpoints({
        serverUrl: SERVER, initialPose: { objects: { dancer: {} } }, env
    });
    const list = await viewpoints.list();
    assert.strictEqual(list.length, 1);
    assert.strictEqual(list[0].filename, 'browser-initial-pose');
    assert.strictEqual(env.calls.length, 0, 'no network needed for a live camera');
});

test('a viewpoint index is fetched and normalized', async () => {
    const env = stubEnv({
        routes: {
            '/viewpoints/index.json': {
                viewpoints: [
                    { filename: 'view_00.json', data: { objects: {} } },
                    { data: { objects: {} } }
                ]
            }
        }
    });
    const viewpoints = createViewpoints({
        serverUrl: SERVER, indexPath: '/viewpoints/index.json', env
    });
    const list = await viewpoints.list();
    assert.strictEqual(list.length, 2);
    assert.strictEqual(list[1].filename, 'view_01.json', 'a name is synthesized');
});

test('viewpoints reject when nothing is available', async () => {
    // Per the contract: a run with no initial pose would solve the first ladder
    // against a default camera and silently invalidate the comparison.
    const env = stubEnv();
    await assert.rejects(
        () => createViewpoints({ serverUrl: SERVER, env }).list(),
        /no viewpoints available/);
    await assert.rejects(
        () => createViewpoints({
            serverUrl: SERVER, indexPath: '/missing.json', env
        }).list(), /HTTP 404/);
    const empty = stubEnv({ routes: { '/v.json': { viewpoints: [] } } });
    await assert.rejects(
        () => createViewpoints({
            serverUrl: SERVER, indexPath: '/v.json', env: empty
        }).list(), /no poses/);
});

// --------------------------------------------------------------------------
// Clock, logger, lifecycle
// --------------------------------------------------------------------------

test('the clock delegates to the injected timer functions', async () => {
    const env = stubEnv();
    const clock = createClock(env);
    assert.strictEqual(clock.now(), env.now());

    let fired = 0;
    const handle = clock.every(1000, () => { fired++; });
    await env.tickIntervals();
    assert.strictEqual(fired, 1);
    clock.cancel(handle);
    await env.tickIntervals();
    assert.strictEqual(fired, 1);

    await assert.doesNotReject(() => clock.delay(5));
});

test('the logger keeps a bounded ring buffer and feeds the page', () => {
    const env = stubEnv();
    const seen = [];
    const logger = createLogger({ env, onLine: e => seen.push(e), keep: 3 });
    for (let i = 0; i < 5; i++) logger.emit('INFO', `line ${i}`, null);

    assert.strictEqual(logger.lines.length, 3, 'bounded so a long run cannot grow it');
    assert.strictEqual(logger.lines[0].line, 'line 2');
    assert.strictEqual(seen.length, 5, 'the page sees every line regardless');
    assert.strictEqual(env.logged.length, 5);
});

test('lifecycle.exit resolves a promise and never navigates', async () => {
    // Unloading the page would cancel the final POST /api/results, losing the
    // run's metrics exactly when they matter.
    const env = stubEnv();
    const lifecycle = createLifecycle({ env });
    lifecycle.exit(0);
    assert.strictEqual(await lifecycle.done, 0);
    assert.strictEqual(lifecycle.exitCode, 0);
    assert.strictEqual(typeof lifecycle.exit, 'function');
    assert.ok(!('location' in lifecycle), 'no navigation surface at all');
});

test('lifecycle shutdown requests reach every registered finalizer', () => {
    const lifecycle = createLifecycle({ env: stubEnv() });
    let calls = 0;
    lifecycle.onShutdownRequest(() => { calls++; });
    lifecycle.onShutdownRequest(() => { calls++; });
    assert.strictEqual(lifecycle.handlerCount, 2);
    lifecycle.requestShutdown();
    assert.strictEqual(calls, 2);
});
