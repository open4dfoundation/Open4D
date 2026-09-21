'use strict';

/**
 * Browser client entry point.
 *
 * The counterpart of system/Client/client.js: read configuration, assemble the
 * platform, hand it to the shared `StreamingClient`. No streaming logic here.
 *
 * Configuration comes from the query string so a run can be launched from a URL
 * without a rebuild, mirroring how the Node client takes environment variables:
 *
 *   /web/?server=http://host:3000&mode=interactive&segments=20
 *
 * | parameter    | default              | meaning                              |
 * |--------------|----------------------|--------------------------------------|
 * | server       | the page's own origin| server base URL                      |
 * | mode         | interactive          | interactive \| simulated             |
 * | storage      | memory               | memory \| opfs asset store           |
 * | viewpoints   | (none)               | path to a viewpoint index JSON       |
 * | decodeBudget | 512                  | decoded-clip cache budget, MB        |
 * | concurrency  | 10                   | parallel asset requests              |
 * | inflight     | 2                    | max concurrent segment downloads     |
 * | objects      | (server's full scene)| comma-separated object subset        |
 *
 * `?objects=` is the one parameter that changes what the experiment can show.
 * The ladder publishes at least one representation per object in the scene, so
 * nine ORBIT objects floor at ~116 Mbps; under that the MCKP can only buy the
 * few highest-weighted objects at the cheapest rung and freezes the rest, and
 * the page looks permanently starved however good the link is. Three objects
 * floor near 21 Mbps, which leaves an ordinary link enough headroom to climb
 * the ladder — which is the behaviour worth watching.
 */

const { StreamingClient } = require('../../ClientCore/streaming-client');
const { mountLinkRate } = require('./link-rate');
const { createBrowserPlatform } = require('./browser-platform');
const { WebGLRenderer } = require('./webgl-renderer');

function readConfig() {
    const params = new URLSearchParams(window.location.search);
    const number = (name, fallback) => {
        const raw = params.get(name);
        const value = Number(raw);
        return raw !== null && Number.isFinite(value) ? value : fallback;
    };
    return {
        serverUrl: (params.get('server') || window.location.origin)
            .replace(/\/+$/, ''),
        mode: (params.get('mode') || 'interactive').toLowerCase(),
        storageMode: (params.get('storage') || 'memory').toLowerCase(),
        viewpointIndexPath: params.get('viewpoints'),
        decodeBudgetBytes: number('decodeBudget', 512) * 1024 * 1024,
        downloadConcurrency: number('concurrency', 10),
        maxInflightSegments: number('inflight', 2),
        runLabel: params.get('label') || 'web-client',
        sceneObjects: (params.get('objects') || '')
            .split(',').map(name => name.trim()).filter(Boolean)
    };
}

/** Minimal page chrome: status line, log pane, artifact downloads. */
function createUi() {
    const status = document.getElementById('status');
    const logPane = document.getElementById('log');
    const artifactList = document.getElementById('artifacts');
    const stopButton = document.getElementById('stop');

    return {
        stopButton,
        setStatus(text) { status.textContent = text; },
        appendLog({ level, line }) {
            // DEBUG is for the console, not the pane. Lines like
            // "mitch superseded" fire several times a segment and bury the
            // INFO/WARN lines that actually tell you what the run is doing.
            if (level.toUpperCase() === 'DEBUG') return;
            const row = document.createElement('div');
            row.className = `log-line log-${level.toLowerCase()}`;
            row.textContent = line;
            logPane.appendChild(row);
            while (logPane.childElementCount > 400) {
                logPane.removeChild(logPane.firstChild);
            }
            logPane.scrollTop = logPane.scrollHeight;
        },
        /**
         * A browser cannot write a file unprompted, so every artifact the core
         * "writes" is surfaced as a download link instead.
         */
        offerArtifact(name, text) {
            let link = artifactList.querySelector(`[data-name="${name}"]`);
            if (!link) {
                link = document.createElement('a');
                link.dataset.name = name;
                link.textContent = name;
                link.download = name;
                artifactList.appendChild(link);
            }
            if (link.href) URL.revokeObjectURL(link.href);
            link.href = URL.createObjectURL(
                new Blob([text], { type: 'application/json' }));
        }
    };
}

async function main() {
    const config = readConfig();
    const ui = createUi();
    // Kept so it can be stopped: a poller that outlives the run keeps hitting
    // /api/shaping after Stop, and in a test harness the live interval stops
    // the process exiting at all.
    const stopLinkRate = mountLinkRate(document.getElementById('link'));
    ui.setStatus(`connecting to ${config.serverUrl} …`);

    const interactive = config.mode === 'interactive';
    if (!['interactive', 'simulated'].includes(config.mode)) {
        throw new Error(
            `?mode must be "interactive" or "simulated", got "${config.mode}"`);
    }
    // Simulated mode replays canned poses, so it has no camera of its own. There
    // is deliberately no default: solving the first ladder against an arbitrary
    // pose would silently invalidate the viewpoint-aware comparison, which is
    // the whole point of the experiment.
    if (!interactive && !config.viewpointIndexPath) {
        throw new Error(
            'simulated mode needs ?viewpoints=<path to a viewpoint index JSON>; '
            + 'interactive mode uses the live camera instead');
    }
    let renderer = null;

    // The platform is built first so the renderer can read downloaded bytes
    // back out of the asset store by handle.
    const platform = await createBrowserPlatform({
        serverUrl: config.serverUrl,
        storageMode: config.storageMode,
        viewpointIndexPath: config.viewpointIndexPath,
        // Interactive mode's pose IS the live camera, so no canned list is
        // needed; the renderer's own pose is adopted immediately after start.
        initialPose: interactive && !config.viewpointIndexPath
            ? { objects: {} } : null,
        onArtifact: (name, text) => ui.offerArtifact(name, text),
        onLogLine: entry => ui.appendLog(entry)
    });

    if (interactive) {
        renderer = new WebGLRenderer({
            canvas: document.getElementById('view'),
            readAsset: handle => platform.assetStore.get(handle),
            decodeBudgetBytes: config.decodeBudgetBytes
        });
        platform.renderer = renderer;
    }

    const client = new StreamingClient({
        platform,
        config: {
            clientMode: interactive ? 'interactive' : 'simulated',
            downloadConcurrency: config.downloadConcurrency,
            maxInflightSegments: config.maxInflightSegments,
            runLabel: config.runLabel,
            sceneObjects: config.sceneObjects
        }
    });

    // Stop must finalize the run, not unload the page: the final
    // POST /api/results happens during shutdown and navigating away cancels it.
    ui.stopButton.addEventListener('click', () => {
        ui.stopButton.disabled = true;
        ui.setStatus('finishing …');
        platform.lifecycle.requestShutdown();
    });
    window.addEventListener('pagehide', () => platform.lifecycle.requestShutdown());

    // Debug handle. Deliberate and documented: without it the only way to
    // inspect a live run is to add logging and rebuild, and the renderer's
    // state (what is on screen, where the camera is) is exactly what you need
    // when the page looks wrong but every log line looks right.
    window.__vs4d = {
        client, platform, renderer,
        inspect() {
            const scene = renderer?._three?.scene;
            const camera = renderer?._three?.camera;
            return {
                objects: [...(renderer?._objects || new Map())].map(([name, e]) => ({
                    name,
                    visible: e.mesh.visible,
                    vertices: e.mesh.geometry.attributes.position?.count ?? 0,
                    frames: e.clip?.frameCount ?? 0,
                    textured: Boolean(e.material.map),
                    centre: e.mesh.geometry.boundingSphere
                        ? e.mesh.geometry.boundingSphere.center.toArray()
                            .map(v => Number(v.toFixed(2)))
                        : null
                })),
                playback: renderer?._playback,
                camera: camera ? {
                    position: camera.position.toArray().map(v => Number(v.toFixed(2))),
                    target: renderer._three.controls.target.toArray()
                        .map(v => Number(v.toFixed(2))),
                    fov: camera.fov, near: camera.near, far: camera.far
                } : null,
                sceneChildren: scene?.children?.length ?? 0,
                stats: renderer?.stats
            };
        }
    };

    await client.run();
    ui.setStatus(`streaming · broadcast ${client.broadcastId ?? '(none)'}`);

    const code = await platform.lifecycle.done;
    ui.setStatus(code === 0
        ? 'run complete — metrics.json is ready below'
        : `run failed (exit ${code}) — see the log`);
    ui.stopButton.disabled = true;
    stopLinkRate();
    if (renderer) await renderer.stop();
}

main().catch(err => {
    const status = document.getElementById('status');
    if (status) status.textContent = `fatal: ${err.message}`;
    // eslint-disable-next-line no-console
    console.error(err);
});
