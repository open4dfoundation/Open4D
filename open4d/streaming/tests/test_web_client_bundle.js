'use strict';

/**
 * Integration test: run the BUNDLED browser client through a complete stream.
 *
 * This loads `system/WebClient/dist/main.js` — the actual esbuild output a
 * browser would fetch — inside a stubbed DOM and drives a real run in simulated
 * mode, which needs no GPU. It therefore covers the parts a unit test cannot:
 * that ClientCore bundles correctly for the browser, that main.js wires the
 * platform up in the right order, and that a run completes and uploads its
 * metrics through browser APIs.
 *
 * NOT covered, because it needs a real browser: WebGL rendering, WebCodecs
 * texture decode, the Draco worker, and OrbitControls. Those live in
 * webgl-renderer.js / texture-decoder.js / draco-worker.js and are exercised
 * only when someone opens the page.
 *
 * Requires a build: cd system/WebClient && node build.js
 *
 * Run: node --test tests/test_web_client_bundle.js
 */

const assert = require('assert');
const test = require('node:test');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const BUNDLE = path.join(__dirname, '..', 'system/WebClient/dist/main.js');

/** Simulated mode needs a pose source; interactive mode uses the live camera. */
const SIMULATED = '?mode=simulated&viewpoints=/viewpoints/index.json';

// --------------------------------------------------------------------------
// Fixtures — a two-object scene, timed fast so the test is quick.
// --------------------------------------------------------------------------
const STREAM_CONFIG = {
    segmentDuration: 2,
    framesPerSegment: 3,
    // The SERVER owns the tick rate, so a test can ask for a fast one without
    // the client knowing anything about it.
    segmentIntervalMs: 80,
    totalSegments: 4,
    updateIntervalSegments: 1
};

function representation(objName, res, crf, qp, bitrate, quality) {
    const id = `r_res${res}_crf${crf}_qp${qp}`;
    return {
        id,
        predicted: { bitrate_mbps: bitrate, quality },
        paths: {
            base_dir: `/files/media/${objName}/${id}`,
            texture_urls: [`/files/media/${objName}/${id}/${objName}_tex_part00.mp4`],
            geometry_url_pattern:
                `/files/media/${objName}/${id}/${objName}_fr%04d_qp${qp}.drc`
        }
    };
}

function manifest() {
    const objects = {};
    ['dancer', 'mitch'].forEach((name, index) => {
        objects[name] = {
            start_number: 1 + index * 60,
            weight: 0.5,
            representations: [
                representation(name, 240, 34, 7, 4, 30),
                representation(name, 480, 30, 8, 10, 35),
                representation(name, 960, 24, 9, 25, 40)
            ]
        };
    });
    return { segment: { t: 0, fps: 30, n_frames: 3 }, objects };
}

// --------------------------------------------------------------------------
// A stubbed browser.
// --------------------------------------------------------------------------
function createStubBrowser({ search = '' } = {}) {
    const requests = [];
    const elements = new Map();
    const listeners = new Map();

    function element(id) {
        if (!elements.has(id)) {
            const node = {
                id,
                textContent: '',
                disabled: false,
                dataset: {},
                children: [],
                className: '',
                get childElementCount() { return node.children.length; },
                get firstChild() { return node.children[0]; },
                scrollTop: 0,
                scrollHeight: 0,
                appendChild(child) { node.children.push(child); return child; },
                removeChild(child) {
                    node.children = node.children.filter(c => c !== child);
                    return child;
                },
                querySelector(selector) {
                    const match = /\[data-name="([^"]+)"\]/.exec(selector);
                    if (!match) return null;
                    return node.children.find(c => c.dataset?.name === match[1]) || null;
                },
                addEventListener(type, fn) {
                    listeners.set(`${id}:${type}`, fn);
                }
            };
            elements.set(id, node);
        }
        return elements.get(id);
    }

    const sandbox = {
        console,
        performance,
        setTimeout, clearTimeout, setInterval, clearInterval,
        URLSearchParams,
        TextEncoder, TextDecoder,
        Blob: class { constructor(parts) { this.parts = parts; } },
        URL: Object.assign(
            function StubURL(...args) { return new (require('url').URL)(...args); },
            {
                createObjectURL: () => 'blob:stub',
                revokeObjectURL: () => {}
            }),
        requests,
        elements,
        listeners,
        artifacts: new Map(),

        window: {
            location: { search, origin: 'http://web-test:3000' },
            devicePixelRatio: 1,
            addEventListener: (type, fn) => listeners.set(`window:${type}`, fn),
            removeEventListener: () => {}
        },

        document: {
            getElementById: id => element(id),
            createElement: tag => {
                const node = element(`${tag}-${Math.random().toString(16).slice(2)}`);
                node.tag = tag;
                return node;
            }
        },

        async fetch(url, options = {}) {
            const requestPath = String(url).replace(/^https?:\/\/[^/]+/, '');
            requests.push({ path: requestPath, options });

            if (requestPath.startsWith('/files/')) {
                // Deliberately realistic in size and latency. The link meter
                // refuses to emit a sample from a degenerate measurement (a few
                // KB in ~0 ms would yield a nonsense bandwidth estimate), so a
                // 1 KB instant stub would never exercise the estimator at all.
                await new Promise(resolve => setTimeout(resolve, 3));
                const bytes = new Uint8Array(512 * 1024);
                return {
                    ok: true, status: 200,
                    arrayBuffer: async () => bytes.buffer,
                    json: async () => { throw new Error('not json'); }
                };
            }
            const routes = {
                '/api/config': STREAM_CONFIG,
                '/api/manifest': manifest(),
                '/api/broadcast/start': { broadcastId: 'web-bcast-1' },
                // Simulated mode replays canned poses, so it must be given a
                // viewpoint index; there is no default pose by design.
                '/viewpoints/index.json': {
                    viewpoints: [
                        { filename: 'view_00.json', data: { objects: {} } },
                        { filename: 'view_01.json', data: { objects: {} } }
                    ]
                }
            };
            const body = routes[requestPath]
                ?? (requestPath.startsWith('/api/') ? { status: 'ok' } : null);
            if (body === null) return { ok: false, status: 404, json: async () => null };
            return { ok: true, status: 200, json: async () => body };
        }
    };

    sandbox.globalThis = sandbox;
    sandbox.self = sandbox;
    return sandbox;
}

function runBundle(sandbox) {
    const code = fs.readFileSync(BUNDLE, 'utf8');
    vm.createContext(sandbox);
    vm.runInContext(code, sandbox, { filename: 'dist/main.js' });
}

async function waitFor(predicate, { timeoutMs = 8000, label = 'condition' } = {}) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        if (predicate()) return;
        await new Promise(resolve => setTimeout(resolve, 5));
    }
    throw new Error(`timed out waiting for ${label}`);
}

const posted = (sandbox, p) =>
    sandbox.requests.some(r => r.path === p && r.options.method === 'POST');

// --------------------------------------------------------------------------

test('the bundle exists — run system/WebClient/build.js first', () => {
    assert.ok(fs.existsSync(BUNDLE),
        'dist/main.js is missing; run: cd system/WebClient && node build.js');
});

test('the bundled client completes a simulated run and uploads its metrics',
    async () => {
        const sandbox = createStubBrowser({ search: SIMULATED });
        runBundle(sandbox);

        await waitFor(() => posted(sandbox, '/api/results'),
            { label: 'POST /api/results' });

        // Startup order the server depends on.
        const apiPaths = sandbox.requests.map(r => r.path);
        assert.ok(apiPaths.indexOf('/api/config')
            < apiPaths.indexOf('/api/broadcast/start'));
        assert.ok(apiPaths.indexOf('/api/broadcast/start')
            < apiPaths.indexOf('/api/viewpoint'));
        assert.ok(apiPaths.indexOf('/api/viewpoint')
            < apiPaths.indexOf('/api/manifest'),
            'the segment-0 pose must reach the server before the first menu');

        // The run really streamed.
        const segmentPosts = sandbox.requests.filter(
            r => /^\/api\/segment\/\d+$/.test(r.path));
        assert.ok(segmentPosts.length >= STREAM_CONFIG.totalSegments,
            `expected >= ${STREAM_CONFIG.totalSegments} segment reports, `
            + `got ${segmentPosts.length}`);
        assert.ok(sandbox.requests.some(r => r.path.startsWith('/files/')),
            'media was actually fetched');

        // And the metrics it uploaded describe a real run.
        const results = sandbox.requests.find(
            r => r.path === '/api/results' && r.options.method === 'POST');
        const metrics = JSON.parse(results.options.body);
        assert.strictEqual(metrics.clientMode, 'simulated');
        assert.strictEqual(metrics.broadcastId, 'web-bcast-1');
        assert.strictEqual(metrics.summary.totalSegments, STREAM_CONFIG.totalSegments);
        assert.strictEqual(metrics.segments.length, STREAM_CONFIG.totalSegments);
        assert.ok(metrics.bandwidthHistory.length > 0,
            'the estimator sampled the link');
    });

test('every request the bundled client makes is uncacheable', async () => {
    // The whole shaped-bandwidth methodology depends on this: a browser serving
    // a segment from its HTTP cache turns the measurement into fiction.
    const sandbox = createStubBrowser({ search: SIMULATED });
    runBundle(sandbox);
    await waitFor(() => posted(sandbox, '/api/results'),
        { label: 'POST /api/results' });

    const cached = sandbox.requests.filter(r => r.options.cache !== 'no-store');
    assert.deepStrictEqual(cached.map(r => r.path), [],
        'every API and media request must set cache: no-store');

    const media = sandbox.requests.filter(r => r.path.startsWith('/files/'));
    assert.ok(media.length > 0);
    assert.ok(media.every(r => r.options.cache === 'no-store'));
});

test('the page surfaces metrics.json as a download artifact', async () => {
    // A browser cannot write a file unprompted, so the core's writeResult has
    // to become a link the user can click.
    const sandbox = createStubBrowser({ search: SIMULATED });
    runBundle(sandbox);
    await waitFor(() => posted(sandbox, '/api/results'),
        { label: 'POST /api/results' });

    const artifacts = sandbox.elements.get('artifacts');
    const names = artifacts.children.map(child => child.dataset.name);
    assert.ok(names.includes('metrics.json'),
        `expected a metrics.json link, got ${JSON.stringify(names)}`);
    assert.ok(names.includes('menu.json'), 'the fetched manifest is offered too');
});

test('the log pane receives lines and stays bounded', async () => {
    const sandbox = createStubBrowser({ search: SIMULATED });
    runBundle(sandbox);
    await waitFor(() => posted(sandbox, '/api/results'),
        { label: 'POST /api/results' });

    const log = sandbox.elements.get('log');
    assert.ok(log.children.length > 0, 'the run logged to the page');
    assert.ok(log.children.length <= 400, 'the pane is capped');
    assert.ok(log.children.some(c => c.textContent.includes('STREAM')));
});

test('the Stop control finalizes the run instead of navigating away', async () => {
    // Unloading the page would cancel the final POST /api/results, losing the
    // run's metrics exactly when they matter.
    const sandbox = createStubBrowser({ search: SIMULATED });
    runBundle(sandbox);
    // A long run, so Stop is what ends it rather than the segment count.
    await waitFor(() => sandbox.listeners.has('stop:click'),
        { label: 'the stop handler to be wired' });

    assert.ok(sandbox.listeners.has('window:pagehide'),
        'pagehide also finalizes, so a closed tab still uploads');

    sandbox.listeners.get('stop:click')();
    await waitFor(() => posted(sandbox, '/api/results'),
        { label: 'metrics upload after Stop' });
    assert.strictEqual(sandbox.elements.get('stop').disabled, true);
});

test('query-string configuration reaches the core', async () => {
    const sandbox = createStubBrowser({
        search: '?mode=simulated&viewpoints=/viewpoints/index.json&concurrency=3&inflight=1&label=web-cfg-test'
    });
    runBundle(sandbox);
    await waitFor(() => posted(sandbox, '/api/results'),
        { label: 'POST /api/results' });

    const start = sandbox.requests.find(
        r => r.path === '/api/broadcast/start' && r.options.method === 'POST');
    const body = JSON.parse(start.options.body);
    assert.strictEqual(body.label, 'web-cfg-test', '?label reached the server');
    assert.strictEqual(body.algorithm, 'mckp-abr-realtime-simulated');
});

test('a server that publishes no manifest yields empty segment reports', async () => {
    const sandbox = createStubBrowser({ search: SIMULATED });
    const realFetch = sandbox.fetch;
    sandbox.fetch = async (url, options) => {
        if (String(url).endsWith('/api/manifest')) {
            return { ok: false, status: 404, json: async () => null };
        }
        return realFetch(url, options);
    };
    runBundle(sandbox);
    await waitFor(() => posted(sandbox, '/api/results'),
        { label: 'POST /api/results' });

    const segmentPosts = sandbox.requests.filter(
        r => /^\/api\/segment\/\d+$/.test(r.path));
    assert.ok(segmentPosts.length > 0);
    assert.ok(segmentPosts.every(r => JSON.parse(r.options.body).isEmpty === true),
        'with nothing published, every report is an empty-segment report');
    assert.ok(!sandbox.requests.some(r => r.path.startsWith('/files/')),
        'and no media is requested');
});
