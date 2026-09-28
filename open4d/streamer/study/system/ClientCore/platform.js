'use strict';

/**
 * The platform contract the client core runs on.
 *
 * `system/ClientCore` holds the streaming logic — segment timing, the MCKP ABR,
 * bandwidth estimation, download planning, stall accounting, metrics. It must
 * never import `fs`, `http`, `path`, `os`, `process` or touch `console`,
 * `Date.now` or `setInterval` directly. Everything platform-shaped goes through
 * one object implementing the seven capabilities below, so the Node desktop
 * client and the browser client run identical logic.
 *
 * Derived by auditing every platform touchpoint in system/Client/client.js:
 * 10 fetch sites, 9 distinct fs calls, 5 process calls, 4 timer sites, the
 * renderer child process, and console logging. Nothing here is speculative —
 * each method backs at least one existing call site.
 *
 * ------------------------------------------------------------------------
 * Asset handles
 * ------------------------------------------------------------------------
 * The core never learns where a downloaded byte lives. `storage.handle(...)`
 * mints an OPAQUE handle, `transport.fetchAsset` fills it, and
 * `renderer.stageSegment` consumes it. The core only moves handles around.
 *
 *   Node    handle = a filesystem path string
 *   Browser handle = an OPFS path, or a key into an in-memory buffer map
 *
 * This is why ClientCore/download-plan.js takes `destinationFor` injected: the
 * planner decides WHICH assets a segment needs and slots them by frame index,
 * without knowing what a destination is. Treat handles as values to pass along,
 * never to parse.
 *
 * ------------------------------------------------------------------------
 * What is NOT in the contract
 * ------------------------------------------------------------------------
 * Deliberately absent, because they are implementation details of one adapter:
 *   - HTTP keep-alive agents and socket limits (Node transport internals)
 *   - the `.part-<pid>-<rand>` write-then-rename dance (Node storage internals)
 *   - process.env / process.argv: these become the plain config object the core
 *     is constructed with, not a capability it calls out to
 */

/**
 * @typedef {string|object} AssetHandle
 *   Opaque locator for a media asset. Minted by `storage.handle`, never parsed
 *   by the core.
 *
 * @typedef {object} HttpResult
 * @property {boolean} ok            transport succeeded AND status was 2xx
 * @property {number} status         HTTP status, or 0 when the request never landed
 * @property {any} body             parsed JSON, or null when there was no JSON body
 *
 * @typedef {object} AssetResult
 * @property {boolean} success       the asset arrived intact
 * @property {number} size          bytes received (0 on failure)
 * @property {number} timeMs        wall time for this asset
 * @property {string} [error]       failure reason, for logs only
 *
 * @typedef {object} AppendStream
 * @property {(line: string) => void} write   MUST NOT block; telemetry runs at 30 Hz
 * @property {() => Promise<void>} close      resolves once flushed
 *
 * @typedef {object} TimerHandle    opaque, only ever passed back to clock.cancel
 */

/**
 * @typedef {object} TransportAdapter  Everything that crosses the network.
 * @property {(path: string) => Promise<HttpResult>} getJson
 *   GET a server API path (e.g. '/api/manifest'). The adapter owns the base URL.
 * @property {(path: string, body: object) => Promise<HttpResult>} postJson
 *   POST JSON. Resolves for any HTTP status; rejects only if the request never
 *   completed. Callers decide what a non-ok status means — /api/config treats it
 *   as fatal, /api/manifest silently skips, telemetry ignores it.
 * @property {(assetPath: string) => (string|null)} assetUrl
 *   Manifest path -> absolute URL. Needs the base URL, hence platform-side.
 *   Returns null for an unusable path, which the planner treats as "no asset".
 * @property {(url: string, handle: AssetHandle|null) => Promise<AssetResult>} fetchAsset
 *   Fetch one media file into `handle`. A null handle means "count the bytes,
 *   keep nothing" (simulated mode). MUST resolve, never reject: a failed asset
 *   is normal and is judged by download-plan's 90% rules.
 */

/**
 * @typedef {object} StorageAdapter  Durable artifacts and per-run staging.
 * @property {(...parts: string[]) => AssetHandle} handle
 *   Compose a handle under the session scratch. Replaces path.join in the core.
 * @property {() => Promise<AssetHandle>} createScratch
 *   Per-run staging root (Node: mkdtemp; browser: an OPFS directory).
 * @property {(handle: AssetHandle) => Promise<void>} release
 *   Recursively drop a handle. Called per object-segment once decoded, and once
 *   for the scratch root at shutdown. MUST tolerate an already-gone handle.
 * @property {(name: string, text: string) => Promise<void>} writeText
 *   Small named debug artifact (the fetched menu.json).
 * @property {(text: string) => Promise<void>} writeResult
 *   The run's final metrics JSON, to wherever this platform keeps results.
 * @property {(name: string) => Promise<AppendStream>} openAppendStream
 *   Line-oriented telemetry (render frames, decode events). Streamed rather than
 *   written at exit so an aborted run keeps its diagnostics.
 */

/**
 * @typedef {object} ViewpointAdapter  Where camera poses come from.
 * @property {() => Promise<Array<{filename: string, data: object}>>} list
 *   Ordered poses. Interactive mode uses only [0] as the startup pose and then
 *   follows the live camera; simulated mode cycles the whole list.
 *   MUST reject if no pose is available — a run with no initial pose would
 *   solve the first ladder against a default camera and silently invalidate it.
 */

/**
 * @typedef {object} ClockAdapter  All time and scheduling.
 * @property {() => number} now                        epoch ms
 * @property {(ms: number, fn: Function) => TimerHandle} every   repeating timer
 * @property {(handle: TimerHandle) => void} cancel    idempotent
 * @property {(ms: number) => Promise<void>} delay     one-shot, for the bounded
 *   manifest-fetch race in processSegmentTick
 */

/**
 * @typedef {object} LoggerAdapter  Line sink.
 * @property {(level: string, line: string, data: object|null) => void} emit
 *   The core formats the `[wall:+Xs][play:Ys][seg:N][LEVEL][COMPONENT]` prefix,
 *   because it owns the run clock and segment counter. The adapter only decides
 *   where the line goes (stdout, DevTools, an on-page pane).
 */

/**
 * @typedef {object} RendererAdapter  Decode and display. May be null.
 * @property {(opts: object) => Promise<void>} start
 * @property {() => Promise<void>} stop
 * @property {(segmentId: number, objects: object) => void} stageSegment
 *   Hand over one segment's per-object asset handles for decode.
 * @property {(segmentId: number, objects: object) => void} setPlaybackState
 *   Per-tick buffer state, so the renderer knows what may be shown.
 * @property {(callbacks: object) => void} setCallbacks
 *   Subscribe to renderer events: `onFrame`, `onObjectReady`, `onClosed`,
 *   `onLog`. The core calls this once during startup — it cannot pass them at
 *   construction because the platform is built before the core exists.
 * @property {object|null} latestCamera
 *   Live pose, read every segment tick in interactive mode.
 *
 * Null in simulated mode: the core credits segments straight from the download
 * result instead of waiting for an object_ready event. Both paths must stay,
 * because they measure different things.
 */

/**
 * @typedef {object} LifecycleAdapter  Process/page control.
 * @property {(code: number) => void} exit
 *   Node: process.exit. Browser: resolve the run promise and stop timers; it
 *   must NOT navigate away, or the final POST /api/results would be cancelled.
 * @property {(fn: () => void) => void} onShutdownRequest
 *   Node: SIGINT/SIGTERM. Browser: a Stop control or beforeunload. Fires the
 *   partial-run finalizer so an interrupted run still uploads its metrics.
 */

/**
 * @typedef {object} ClientPlatform
 * @property {TransportAdapter} transport
 * @property {StorageAdapter} storage
 * @property {ViewpointAdapter} viewpoints
 * @property {ClockAdapter} clock
 * @property {LoggerAdapter} logger
 * @property {RendererAdapter|null} renderer
 * @property {LifecycleAdapter} lifecycle
 */

/**
 * Freeze the contract all the way down, method arrays included.
 *
 * A shallow Object.freeze leaves `methods` mutable, so one stray
 * `PLATFORM_CONTRACT.transport.methods.push(...)` would silently add a
 * requirement that every adapter then fails — and because the contract is a
 * module singleton, the corruption outlives the code that caused it.
 */
function deepFreezeContract(contract) {
    for (const spec of Object.values(contract)) {
        Object.freeze(spec.methods);
        Object.freeze(spec.properties);
        Object.freeze(spec);
    }
    return Object.freeze(contract);
}

/**
 * The contract, as data. `validatePlatform` and ClientCore/README.md are both
 * driven by this, so an adapter cannot drift from its documentation.
 *
 * `renderer` is listed but optional — see RendererAdapter.
 */
const PLATFORM_CONTRACT = deepFreezeContract({
    transport: {
        methods: ['getJson', 'postJson', 'assetUrl', 'fetchAsset'],
        properties: []
    },
    storage: {
        methods: ['handle', 'createScratch', 'release', 'writeText', 'writeResult',
                  'openAppendStream'],
        properties: []
    },
    viewpoints: { methods: ['list'], properties: [] },
    clock: {
        methods: ['now', 'every', 'cancel', 'delay'],
        properties: []
    },
    logger: { methods: ['emit'], properties: [] },
    renderer: {
        methods: ['start', 'stop', 'stageSegment', 'setPlaybackState', 'setCallbacks'],
        properties: ['latestCamera'],
        optional: true
    },
    lifecycle: { methods: ['exit', 'onShutdownRequest'], properties: [] }
});

/**
 * Fail loudly and completely on an incomplete platform.
 *
 * Called once at startup rather than letting a missing method surface as
 * `undefined is not a function` twelve segments into a run — in a research
 * client that produces a half-finished result file that looks legitimate.
 * Every problem is reported at once so a new adapter can be finished in one
 * pass instead of one error per run.
 *
 * @param {ClientPlatform} platform
 * @param {{requireRenderer?: boolean}} [options]
 *   requireRenderer: true in interactive mode, where a null renderer would mean
 *   nothing is ever decoded and every object would be reported missing.
 * @returns {ClientPlatform} the same object, for chaining
 * @throws {TypeError} listing every missing capability, method and property
 */
function validatePlatform(platform, { requireRenderer = false } = {}) {
    if (!platform || typeof platform !== 'object') {
        throw new TypeError('ClientPlatform must be an object');
    }

    const problems = [];

    for (const [name, spec] of Object.entries(PLATFORM_CONTRACT)) {
        const capability = platform[name];
        const isRenderer = name === 'renderer';

        if (capability == null) {
            // renderer is the one optional capability, and only in simulated
            // mode: interactive mode without a renderer would decode nothing
            // and report every object missing.
            if (spec.optional && !(isRenderer && requireRenderer)) continue;
            problems.push(isRenderer
                ? 'renderer is required in interactive mode'
                : `missing capability: ${name}`);
            continue;
        }
        if (typeof capability !== 'object') {
            problems.push(`${name} must be an object, got ${typeof capability}`);
            continue;
        }
        for (const method of spec.methods) {
            if (typeof capability[method] !== 'function') {
                problems.push(`${name}.${method} must be a function`);
            }
        }
        for (const property of spec.properties) {
            if (!(property in capability)) {
                problems.push(`${name}.${property} must be present`);
            }
        }
    }

    if (problems.length) {
        throw new TypeError(
            `Incomplete ClientPlatform:\n  - ${problems.join('\n  - ')}`);
    }
    return platform;
}

/** Capability names, in contract order. */
function platformCapabilities() {
    return Object.keys(PLATFORM_CONTRACT);
}

module.exports = { PLATFORM_CONTRACT, validatePlatform, platformCapabilities };
