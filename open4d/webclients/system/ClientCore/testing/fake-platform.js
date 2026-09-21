'use strict';

/**
 * In-memory ClientPlatform for tests, and the reference for what a real adapter
 * has to do.
 *
 * Two jobs:
 *   1. Let the client core be tested with no server, no disk, no GPU and no
 *      real time — `clock.advance(ms)` drives segment ticks deterministically,
 *      so a 40-segment run is a millisecond of test time instead of 80 seconds.
 *   2. Pin the contract by example. When writing the browser adapter, read this
 *      alongside ClientCore/platform.js: anything this fake does, that adapter
 *      has to do for real.
 *
 * Not a mock framework: every capability is a working implementation over
 * ordinary JS structures, so a test asserts on observable state
 * (`storage.files`, `transport.requests`, `renderer.staged`) rather than on
 * call expectations.
 */

const { validatePlatform } = require('../platform');

// --------------------------------------------------------------------------
// Clock: virtual time.
// --------------------------------------------------------------------------
class FakeClock {
    constructor(start = 1_700_000_000_000) {
        this.t = start;
        this._seq = 0;
        this._timers = new Map();
    }

    now() { return this.t; }

    every(ms, fn) {
        if (!(ms > 0)) throw new RangeError('every() needs a positive interval');
        const id = ++this._seq;
        this._timers.set(id, { kind: 'interval', ms, next: this.t + ms, fn });
        return id;
    }

    delay(ms) {
        return new Promise(resolve => {
            const id = ++this._seq;
            this._timers.set(id, { kind: 'timeout', next: this.t + ms, fn: resolve });
        });
    }

    cancel(handle) { this._timers.delete(handle); }

    /** Timers still armed — a leak check for finishStream(). */
    get pending() { return this._timers.size; }

    /**
     * Advance virtual time, firing every timer that comes due in order and
     * letting each one's microtasks drain before the next fires. Async because
     * the core's timer callbacks are async (a segment tick awaits a manifest).
     */
    async advance(ms) {
        const target = this.t + ms;
        // Bounded so a pathological interval cannot hang the suite.
        for (let guard = 0; guard < 100_000; guard++) {
            let earliest = null;
            let earliestId = null;
            for (const [id, timer] of this._timers) {
                if (timer.next <= target && (earliest === null || timer.next < earliest.next)) {
                    earliest = timer;
                    earliestId = id;
                }
            }
            if (earliest === null) break;

            this.t = earliest.next;
            if (earliest.kind === 'interval') {
                earliest.next = this.t + earliest.ms;
            } else {
                this._timers.delete(earliestId);
            }
            await earliest.fn();
            await Promise.resolve();   // drain microtasks queued by the callback
        }
        this.t = target;
    }
}

// --------------------------------------------------------------------------
// Transport: routed, recording, no sockets.
// --------------------------------------------------------------------------
class FakeTransport {
    constructor({ baseUrl = 'http://fake-server:3000' } = {}) {
        this.baseUrl = baseUrl;
        this.requests = [];        // { method, path, body }
        this.assetRequests = [];   // { url, handle }
        this._routes = [];         // { method, pattern, handler }
        /**
         * Per-URL asset behaviour. Keys are matched as substrings so a test can
         * fail "every geometry file of dancer" with one entry. Default: succeed.
         */
        this.assetRules = [];      // { match, success, size, timeMs }
        this.defaultAsset = { success: true, size: 50_000, timeMs: 5 };
    }

    on(method, pattern, handler) {
        this._routes.push({ method, pattern, handler });
        return this;
    }

    onGet(pattern, handler) { return this.on('GET', pattern, handler); }
    onPost(pattern, handler) { return this.on('POST', pattern, handler); }

    /** Fail or slow specific assets: failAsset('dancer_fr0003') */
    failAsset(match) {
        this.assetRules.push({ match, success: false, size: 0, timeMs: 1 });
        return this;
    }

    setAsset(match, result) {
        this.assetRules.push({ match, ...result });
        return this;
    }

    _resolve(method, path) {
        for (const route of this._routes) {
            if (route.method !== method) continue;
            if (route.pattern === path) return route.handler;
        }
        // Prefix fallback, so '/api/segment/' catches '/api/segment/17'.
        for (const route of this._routes) {
            if (route.method !== method) continue;
            if (typeof route.pattern === 'string' && path.startsWith(route.pattern)) {
                return route.handler;
            }
        }
        return null;
    }

    async _call(method, path, body) {
        this.requests.push({ method, path, body });
        const handler = this._resolve(method, path);
        if (!handler) return { ok: false, status: 404, body: null };
        const result = await handler(body, path);
        if (result && typeof result === 'object' && 'ok' in result) return result;
        return { ok: true, status: 200, body: result ?? null };
    }

    getJson(path) { return this._call('GET', path, null); }
    postJson(path, body) { return this._call('POST', path, body); }

    assetUrl(assetPath) {
        if (!assetPath) return null;
        if (/^https?:\/\//.test(assetPath)) return assetPath;
        if (assetPath.startsWith('/files/')) return `${this.baseUrl}${assetPath}`;
        const match = assetPath.match(/files\/(.+)/);
        return match ? `${this.baseUrl}/files/${match[1]}` : null;
    }

    async fetchAsset(url, handle) {
        this.assetRequests.push({ url, handle });
        const rule = this.assetRules.find(r => url.includes(r.match));
        const { success, size, timeMs } = rule || this.defaultAsset;
        return success
            ? { success: true, size, timeMs }
            : { success: false, size: 0, timeMs, error: 'fake asset failure' };
    }

    /** Paths requested, for order assertions. */
    pathsFor(method) {
        return this.requests.filter(r => r.method === method).map(r => r.path);
    }

    bodyFor(method, path) {
        const hit = [...this.requests].reverse().find(
            r => r.method === method && r.path === path);
        return hit ? hit.body : null;
    }
}

// --------------------------------------------------------------------------
// Storage: a Map with handle composition.
// --------------------------------------------------------------------------
class FakeStorage {
    constructor() {
        this.files = new Map();      // name -> text
        this.streams = new Map();    // name -> string[]
        this.result = null;          // writeResult payload
        this.scratch = null;
        this.released = [];
        this._scratchCount = 0;
    }

    handle(...parts) { return parts.filter(p => p != null).join('/'); }

    async createScratch() {
        this.scratch = `/fake-scratch/run-${++this._scratchCount}`;
        return this.scratch;
    }

    async release(handle) {
        // Tolerating an absent handle is part of the contract: an object whose
        // download failed may never have had a directory created.
        this.released.push(handle);
    }

    async writeText(name, text) { this.files.set(name, text); }

    async writeResult(text) { this.result = text; }

    async openAppendStream(name) {
        const lines = [];
        this.streams.set(name, lines);
        let closed = false;
        return {
            write: line => {
                if (closed) throw new Error(`write after close on ${name}`);
                lines.push(line);
            },
            close: async () => { closed = true; }
        };
    }

    /** Parsed JSONL for a telemetry stream. */
    linesOf(name) {
        return (this.streams.get(name) || []).map(l => JSON.parse(l));
    }
}

// --------------------------------------------------------------------------
// Renderer: records what it was told, emits events on demand.
// --------------------------------------------------------------------------
class FakeRenderer {
    constructor({ initialCamera = { position: [0, 1.6, 3] } } = {}) {
        this.latestCamera = initialCamera;
        this.started = null;
        this.stopped = false;
        this.staged = [];            // { segmentId, objects }
        this.playbackStates = [];    // { segmentId, objects }
        this.callbacks = {};
    }

    /** The adapter takes event callbacks at construction; mirror that. */
    setCallbacks(callbacks) { this.callbacks = callbacks || {}; }

    async start(opts) { this.started = opts; }
    async stop() { this.stopped = true; }
    stageSegment(segmentId, objects) { this.staged.push({ segmentId, objects }); }
    setPlaybackState(segmentId, objects) {
        this.playbackStates.push({ segmentId, objects });
    }

    /** Drive the decode-completion path the core credits buffers from. */
    emitObjectReady(segmentId, objectName, repId, extra = {}) {
        this.callbacks.onObjectReady?.({
            type: 'object_ready', segmentId, objectName, repId, decodeMs: 12, ...extra
        });
    }

    emitObjectError(segmentId, objectName, repId, message = 'fake decode failure',
                    extra = {}) {
        this.callbacks.onObjectReady?.({
            type: 'object_error', segmentId, objectName, repId, message, ...extra
        });
    }

    emitFrame(frame = {}) {
        this.callbacks.onFrame?.({ camera: this.latestCamera, ...frame });
    }

    emitClosed(event = {}) { this.callbacks.onClosed?.(event); }

    moveCamera(camera) { this.latestCamera = camera; }
}

// --------------------------------------------------------------------------
// Lifecycle
// --------------------------------------------------------------------------
class FakeLifecycle {
    constructor() {
        this.exitCode = null;
        this.exitCalls = 0;
        this._shutdownHandlers = [];
    }

    exit(code) { this.exitCode = code; this.exitCalls++; }

    onShutdownRequest(fn) { this._shutdownHandlers.push(fn); }

    /** Simulate SIGINT / the browser Stop control. */
    requestShutdown() {
        for (const fn of this._shutdownHandlers) fn();
    }
}

// --------------------------------------------------------------------------
// Logger
// --------------------------------------------------------------------------
class FakeLogger {
    constructor({ echo = false } = {}) {
        this.lines = [];             // { level, line, data }
        this.echo = echo;
    }

    emit(level, line, data) {
        this.lines.push({ level, line, data });
        if (this.echo) console.log(line, data ? JSON.stringify(data) : '');
    }

    /** Lines at a level, for asserting that a warning actually fired. */
    at(level) { return this.lines.filter(l => l.level === level); }

    /** True when any line contains `text`. */
    saw(text) { return this.lines.some(l => l.line.includes(text)); }
}

// --------------------------------------------------------------------------
// Assembly
// --------------------------------------------------------------------------

/**
 * A complete, contract-valid platform for tests.
 *
 * @param {object} [options]
 * @param {boolean} [options.withRenderer=true]  false models simulated mode
 * @param {Array<{filename: string, data: object}>} [options.viewpoints]
 * @param {boolean} [options.echoLogs=false]     print lines while debugging a test
 * @returns {import('../platform').ClientPlatform & {
 *   clock: FakeClock, transport: FakeTransport, storage: FakeStorage,
 *   renderer: FakeRenderer|null, lifecycle: FakeLifecycle, logger: FakeLogger }}
 */
function createFakePlatform({
    withRenderer = true,
    viewpoints = [{ filename: 'view_00.json', data: { objects: {} } }],
    echoLogs = false
} = {}) {
    const renderer = withRenderer ? new FakeRenderer() : null;
    const platform = {
        transport: new FakeTransport(),
        storage: new FakeStorage(),
        viewpoints: {
            list: async () => {
                if (!viewpoints || viewpoints.length === 0) {
                    throw new Error('no viewpoints available');
                }
                return viewpoints;
            }
        },
        clock: new FakeClock(),
        logger: new FakeLogger({ echo: echoLogs }),
        renderer,
        lifecycle: new FakeLifecycle()
    };
    return validatePlatform(platform, { requireRenderer: withRenderer });
}

module.exports = {
    createFakePlatform,
    FakeClock,
    FakeTransport,
    FakeStorage,
    FakeRenderer,
    FakeLifecycle,
    FakeLogger
};
