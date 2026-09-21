'use strict';

/**
 * Browser implementation of the ClientPlatform contract
 * (../../ClientCore/platform.js).
 *
 * Six of the seven capabilities live here; the renderer is ./webgl-renderer.js
 * because it is much larger and needs a GPU. All streaming logic is in
 * ClientCore and is shared verbatim with the Node desktop client.
 *
 * Browser globals are reached through an injected `env` rather than captured at
 * module scope, so this file can be exercised in Node against stubs — which is
 * how tests/test_browser_platform.js verifies the cache policy, the OPFS paths
 * and the non-blocking telemetry buffer without a browser.
 */

/** Default environment: the real browser globals. */
function defaultEnv() {
    return {
        fetch: typeof fetch === 'function' ? fetch.bind(globalThis) : undefined,
        navigator: typeof navigator !== 'undefined' ? navigator : undefined,
        document: typeof document !== 'undefined' ? document : undefined,
        console: typeof console !== 'undefined' ? console : undefined,
        setInterval: globalThis.setInterval?.bind(globalThis),
        clearInterval: globalThis.clearInterval?.bind(globalThis),
        setTimeout: globalThis.setTimeout?.bind(globalThis),
        now: () => Date.now(),
        URL: globalThis.URL,
        Blob: globalThis.Blob
    };
}

// --------------------------------------------------------------------------
// Asset stores
// --------------------------------------------------------------------------

/**
 * Downloaded media held as compressed bytes in memory.
 *
 * This is the default, and it is the right default: what blows up a browser's
 * memory is DECODED frames (a 1920-wide texture frame is 5.5 MB), not the
 * compressed segment, which is tens of MB for a whole scene. The decode cache
 * is the renderer's problem; this only has to hold what arrived off the wire
 * until the renderer has consumed it.
 */
class MemoryAssetStore {
    constructor() {
        this.buffers = new Map();   // handle -> ArrayBuffer
    }

    async put(handle, buffer) { this.buffers.set(handle, buffer); }

    /** The renderer reads bytes back out by handle. */
    get(handle) { return this.buffers.get(handle) || null; }

    has(handle) { return this.buffers.has(handle); }

    /** Drop everything under a handle prefix (an object-segment directory). */
    async releasePrefix(prefix) {
        for (const key of [...this.buffers.keys()]) {
            if (key === prefix || key.startsWith(`${prefix}/`)) {
                this.buffers.delete(key);
            }
        }
    }

    get byteLength() {
        let total = 0;
        for (const buffer of this.buffers.values()) total += buffer.byteLength || 0;
        return total;
    }
}

/**
 * Downloaded media written to the Origin Private File System.
 *
 * Slower than memory but survives a reload and does not compete with the
 * decoder for heap. Note that `createSyncAccessHandle` is worker-only; this uses
 * the async writable-stream API so it works on the main thread.
 */
class OpfsAssetStore {
    constructor(rootDirectory) {
        this.root = rootDirectory;
    }

    static async create(env, name) {
        const opfsRoot = await env.navigator.storage.getDirectory();
        const directory = await opfsRoot.getDirectoryHandle(name, { create: true });
        return new OpfsAssetStore(directory);
    }

    async _dirFor(parts, { create }) {
        let directory = this.root;
        for (const part of parts) {
            directory = await directory.getDirectoryHandle(part, { create });
        }
        return directory;
    }

    async put(handle, buffer) {
        const parts = handle.split('/').filter(Boolean);
        const filename = parts.pop();
        const directory = await this._dirFor(parts, { create: true });
        const fileHandle = await directory.getFileHandle(filename, { create: true });
        const writable = await fileHandle.createWritable();
        await writable.write(buffer);
        await writable.close();
    }

    async get(handle) {
        const parts = handle.split('/').filter(Boolean);
        const filename = parts.pop();
        try {
            const directory = await this._dirFor(parts, { create: false });
            const fileHandle = await directory.getFileHandle(filename);
            return await (await fileHandle.getFile()).arrayBuffer();
        } catch (_) {
            return null;
        }
    }

    async releasePrefix(prefix) {
        const parts = prefix.split('/').filter(Boolean);
        const name = parts.pop();
        try {
            const directory = await this._dirFor(parts, { create: false });
            await directory.removeEntry(name, { recursive: true });
        } catch (_) {
            // Contract: release must tolerate a handle that was never created.
        }
    }
}

// --------------------------------------------------------------------------
// Transport
// --------------------------------------------------------------------------

function createTransport({ serverUrl, assetStore, env }) {
    async function call(method, apiPath, body) {
        const res = await env.fetch(`${serverUrl}${apiPath}`, {
            method,
            // API responses must never come from the HTTP cache: the manifest
            // changes every segment and a cached menu would silently pin the
            // client to a stale ladder.
            cache: 'no-store',
            ...(body === undefined || body === null ? {} : {
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(body)
            })
        });
        let parsed = null;
        try {
            parsed = await res.json();
        } catch (_) {
            // Not every endpoint answers with JSON; telemetry posts do not.
        }
        return { ok: res.ok, status: res.status, body: parsed };
    }

    return {
        getJson: apiPath => call('GET', apiPath, null),
        postJson: (apiPath, body) => call('POST', apiPath, body),

        assetUrl(assetPath) {
            if (!assetPath) return null;
            if (/^https?:\/\//.test(assetPath)) return assetPath;
            if (assetPath.startsWith('/files/')) return `${serverUrl}${assetPath}`;
            const match = assetPath.match(/files\/(.+)/);
            return match ? `${serverUrl}/files/${match[1]}` : null;
        },

        /**
         * Fetch one media file. Never rejects: a failed asset is normal and is
         * judged by download-plan's 90% rules.
         *
         * `cache: 'no-store'` is REQUIRED, not a nicety. The segment must keep
         * generating real network load or the shaped-bandwidth experiment stops
         * meaning anything — the same reason system/Client/decode_cache.py
         * caches decodes but deliberately never caches downloads. A browser
         * quietly serving a segment from its HTTP cache turns a bandwidth
         * measurement into fiction.
         */
        async fetchAsset(url, handle) {
            const start = env.now();
            try {
                const res = await env.fetch(url, { cache: 'no-store' });
                if (!res.ok) {
                    return {
                        success: false, size: 0, timeMs: 0, error: `HTTP ${res.status}`
                    };
                }
                const buffer = await res.arrayBuffer();
                // A null handle means "count the bytes, keep nothing".
                if (handle) await assetStore.put(handle, buffer);
                return {
                    success: true,
                    size: buffer.byteLength,
                    timeMs: env.now() - start
                };
            } catch (err) {
                return {
                    success: false, size: 0,
                    timeMs: env.now() - start, error: err.message
                };
            }
        }
    };
}

// --------------------------------------------------------------------------
// Storage
// --------------------------------------------------------------------------

/**
 * Buffered line sink.
 *
 * `write` MUST NOT block: render telemetry arrives at 30 Hz, and in Node a
 * synchronous write per frame stalled the event loop badly enough to starve the
 * renderer. Lines accumulate in an array and are flushed on an interval, so the
 * hot path is one array push.
 */
class BufferedLineSink {
    constructor({ name, flush, env, flushIntervalMs = 2000 }) {
        this.name = name;
        this.lines = [];
        this._pending = [];
        this._flush = flush;
        this._env = env;
        this._closed = false;
        this._timer = flush
            ? env.setInterval(() => { this._drain(); }, flushIntervalMs)
            : null;
    }

    write(line) {
        if (this._closed) throw new Error(`write after close on ${this.name}`);
        this.lines.push(line);
        this._pending.push(line);
    }

    _drain() {
        if (this._pending.length === 0) return Promise.resolve();
        const batch = this._pending.splice(0, this._pending.length);
        // The try/catch is load-bearing: a SYNCHRONOUS throw from the flush
        // (an OPFS quota error, say) escapes before Promise.resolve can wrap
        // it, so a trailing .catch alone would let it reject close() — which
        // finish() awaits, taking down the metrics upload with it. Telemetry
        // must never take the run down.
        try {
            return Promise.resolve(this._flush(batch)).catch(() => {});
        } catch (_) {
            return Promise.resolve();
        }
    }

    async close() {
        this._closed = true;
        if (this._timer) this._env.clearInterval(this._timer);
        await this._drain();
    }

    text() { return this.lines.join('\n') + (this.lines.length ? '\n' : ''); }
}

/**
 * @param {object} args
 * @param {MemoryAssetStore|OpfsAssetStore} args.assetStore
 * @param {object} args.env
 * @param {(filename: string, text: string) => void} [args.onArtifact]
 *   Called with the run's result and telemetry so the page can offer them as
 *   downloads. The browser has nowhere to "write a file" unprompted.
 */
function createStorage({ assetStore, env, onArtifact = null }) {
    const artifacts = new Map();   // filename -> text
    const sinks = new Map();       // name -> BufferedLineSink
    let scratchCount = 0;

    return {
        artifacts,
        sinks,

        handle: (...parts) => parts.filter(p => p != null).join('/'),

        async createScratch() {
            return `run-${++scratchCount}`;
        },

        async release(handle) {
            await assetStore.releasePrefix(handle);
        },

        async writeText(name, text) {
            artifacts.set(name, text);
            onArtifact?.(name, text);
        },

        async writeResult(text) {
            artifacts.set('metrics.json', text);
            onArtifact?.('metrics.json', text);
        },

        async openAppendStream(name) {
            const sink = new BufferedLineSink({
                name,
                env,
                // Keep the accumulated text available as a downloadable
                // artifact; flushing to OPFS as well would double-store it for
                // no benefit at these sizes.
                flush: () => { artifacts.set(name, sink.text()); }
            });
            sinks.set(name, sink);
            return sink;
        }
    };
}

// --------------------------------------------------------------------------
// Viewpoints
// --------------------------------------------------------------------------

/**
 * Where the initial camera pose comes from.
 *
 * `initialPose` short-circuits the fetch, which is what an interactive page
 * does: the user's camera is the pose, and the canned list only matters in
 * simulated mode.
 *
 * Rejects when nothing is available, per the contract — a run with no initial
 * pose would solve the first ladder against a default camera and silently
 * invalidate the viewpoint-aware comparison.
 */
function createViewpoints({ serverUrl, indexPath, initialPose, env }) {
    return {
        async list() {
            if (initialPose) {
                return [{ filename: 'browser-initial-pose', data: initialPose }];
            }
            if (!indexPath) {
                throw new Error(
                    'no viewpoints available: pass initialPose or viewpointIndexPath');
            }
            const res = await env.fetch(`${serverUrl}${indexPath}`,
                { cache: 'no-store' });
            if (!res.ok) {
                throw new Error(`viewpoint index fetch failed: HTTP ${res.status}`);
            }
            const body = await res.json();
            const list = Array.isArray(body) ? body : body?.viewpoints;
            if (!Array.isArray(list) || list.length === 0) {
                throw new Error('viewpoint index contained no poses');
            }
            return list.map((entry, index) => ({
                filename: entry.filename || `view_${String(index).padStart(2, '0')}.json`,
                data: entry.data || entry
            }));
        }
    };
}

// --------------------------------------------------------------------------
// Clock, logger, lifecycle
// --------------------------------------------------------------------------

function createClock(env) {
    return {
        now: () => env.now(),
        every: (ms, fn) => env.setInterval(fn, ms),
        cancel: handle => env.clearInterval(handle),
        delay: ms => new Promise(resolve => env.setTimeout(resolve, ms))
    };
}

/**
 * @param {object} args
 * @param {(entry: {level: string, line: string, data: object|null}) => void} [args.onLine]
 *   Page sink, e.g. an on-screen log pane.
 * @param {number} [args.keep] ring-buffer size held for inspection
 */
function createLogger({ env, onLine = null, keep = 500 }) {
    const lines = [];
    return {
        lines,
        emit(level, line, data) {
            lines.push({ level, line, data });
            if (lines.length > keep) lines.shift();
            if (data) env.console?.log(line, data);
            else env.console?.log(line);
            onLine?.({ level, line, data });
        }
    };
}

/**
 * Page lifecycle.
 *
 * `exit` MUST NOT navigate away. The final POST /api/results happens during
 * shutdown, and unloading the page cancels it — the run's metrics would be lost
 * exactly when they matter. So this resolves a promise the page can await and
 * leaves the document alone.
 */
function createLifecycle({ env } = { env: defaultEnv() }) {
    let resolveDone;
    const done = new Promise(resolve => { resolveDone = resolve; });
    const handlers = [];

    return {
        /** Resolves with the exit code once the run has finalized. */
        done,
        exitCode: null,

        exit(code) {
            this.exitCode = code;
            resolveDone(code);
        },

        onShutdownRequest(fn) {
            handlers.push(fn);
        },

        /** Wire a Stop control / pagehide to the registered finalizers. */
        requestShutdown() {
            for (const fn of handlers) fn();
        },

        get handlerCount() { return handlers.length; }
    };
}

// --------------------------------------------------------------------------
// Assembly
// --------------------------------------------------------------------------

/**
 * Build the browser platform.
 *
 * @param {object} args
 * @param {string} args.serverUrl
 * @param {object|null} [args.renderer] a RendererAdapter, or null for simulated mode
 * @param {object} [args.initialPose]
 * @param {string} [args.viewpointIndexPath]
 * @param {'memory'|'opfs'} [args.storageMode='memory']
 * @param {Function} [args.onArtifact]
 * @param {Function} [args.onLogLine]
 * @param {object} [args.env] injected globals, for tests
 * @returns {Promise<object>} the platform, plus `assetStore` and `lifecycle`
 */
async function createBrowserPlatform({
    serverUrl,
    renderer = null,
    initialPose = null,
    viewpointIndexPath = null,
    storageMode = 'memory',
    onArtifact = null,
    onLogLine = null,
    env = defaultEnv()
}) {
    if (!serverUrl) throw new Error('serverUrl is required');
    if (typeof env.fetch !== 'function') {
        throw new Error('this environment has no fetch()');
    }

    const assetStore = storageMode === 'opfs'
        ? await OpfsAssetStore.create(env, 'vs4d-client')
        : new MemoryAssetStore();

    return {
        transport: createTransport({ serverUrl, assetStore, env }),
        storage: createStorage({ assetStore, env, onArtifact }),
        viewpoints: createViewpoints({
            serverUrl, indexPath: viewpointIndexPath, initialPose, env
        }),
        clock: createClock(env),
        logger: createLogger({ env, onLine: onLogLine }),
        renderer,
        lifecycle: createLifecycle({ env }),
        // Not part of the contract; the renderer needs to read downloaded bytes
        // back out by handle.
        assetStore
    };
}

module.exports = {
    createBrowserPlatform,
    createTransport,
    createStorage,
    createViewpoints,
    createClock,
    createLogger,
    createLifecycle,
    MemoryAssetStore,
    OpfsAssetStore,
    BufferedLineSink,
    defaultEnv
};
