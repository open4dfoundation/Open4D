'use strict';

/**
 * Entry point for the Vega splat viewer.
 *
 *   /web/vega.html?assets=/vega-assets
 *
 * | parameter | default         | meaning                                  |
 * |-----------|-----------------|------------------------------------------|
 * | assets    | /vega-assets    | URL prefix serving the export directory  |
 * | objects   | (all)           | comma-separated object names             |
 * | frame     | object          | object \| all -- fit one subject or the venue |
 * | splatScale| 1.0             | multiplier on Gaussian size, for probing |
 */

const { VegaClient } = require('./vega-client');

function readConfig() {
    const params = new URLSearchParams(window.location.search);
    const number = (name, fallback) => {
        const value = Number(params.get(name));
        return params.get(name) !== null && Number.isFinite(value) ? value : fallback;
    };
    return {
        assetBase: params.get('assets') || '/vega-assets',
        objects: (params.get('objects') || '').split(',').filter(Boolean),
        splatScale: number('splatScale', 1.0),
        frameMode: params.get('frame') === 'all' ? 'all' : 'object',
        splatMode: params.get('splatMode') === 'anisotropic'
            ? 'anisotropic' : 'isotropic'
    };
}

async function main() {
    const config = readConfig();
    const status = document.getElementById('status');
    const logPane = document.getElementById('log');
    const statsPane = document.getElementById('stats');

    const log = (level, message) => {
        const row = document.createElement('div');
        row.className = `log-line log-${level}`;
        row.textContent = `[${new Date().toISOString().slice(11, 23)}] ${message}`;
        logPane.appendChild(row);
        while (logPane.childElementCount > 300) {
            logPane.removeChild(logPane.firstChild);
        }
        logPane.scrollTop = logPane.scrollHeight;
    };

    // Looked up before the client is constructed: its onEvent handler fires
    // during start() and touches this button.
    const focusButton = document.getElementById('focus');

    status.textContent = `loading ${config.assetBase} …`;
    const client = new VegaClient({
        assetBase: config.assetBase,
        canvas: document.getElementById('view'),
        splatScale: config.splatScale,
        splatMode: config.splatMode,
        frameMode: config.frameMode,
        onEvent: event => {
            if (event.type === 'catalog') {
                log('info', `${event.objects.length} objects · `
                    + `${event.frameCount} frames · ${event.fps} fps`);
                for (const object of event.objects) {
                    log('info', `  ${object.name}: ${object.points} splats`);
                }
                log('warn', `colour is baked: ${event.colorApproximation}`);
                // Not "playing" yet: the whole clip is loaded first, which is
                // ~64 MB and takes about 15 s on a 35 Mbps link. Saying
                // "playing" through a 15 s load is indistinguishable from a
                // hang, which is exactly how the storm-era failure read.
                status.textContent = 'loading the clip …';
            } else if (event.type === 'preload') {
                const mb = (event.bytes / 1e6).toFixed(0);
                status.textContent = `loading the clip … frame `
                    + `${event.frames}/${event.totalFrames} (${mb} MB)`;
            } else if (event.type === 'preloaded') {
                const mb = (event.bytes / 1e6).toFixed(1);
                log('info', `loaded ${event.frames}/${event.totalFrames} frames, `
                    + `${mb} MB — looping from memory`);
                if (event.frames < event.totalFrames) {
                    log('warn', `${event.totalFrames - event.frames} frames `
                        + 'could not be loaded; looping the contiguous run that '
                        + 'every object has');
                }
                if (event.failures) {
                    log('warn', `${event.failures} frame fetches failed`);
                }
                status.textContent = `playing ${event.frames} frames from memory`;
            } else if (event.type === 'framed') {
                // Say where the rest of the scene went, or an empty-looking
                // view is indistinguishable from a broken one.
                log('info', `framed on ${event.focus}`
                    + (event.others.length
                        ? ` — ${event.others.join(', ')} `
                          + `${event.others.length === 1 ? 'is' : 'are'} elsewhere `
                          + `in the venue (scene spans ${event.sceneSpanMetres} m); `
                          + 'use Focus to cycle, or ?frame=all to fit everything'
                        : ''));
                focusButton.disabled = event.others.length === 0;
                focusButton.textContent = `Focus: ${event.focus}`;
            } else if (event.type === 'error') {
                log('error', event.message);
            }
        }
    });
    window.__vs4dVega = client;

    focusButton.addEventListener('click', () => {
        const names = client.objectNames;
        if (names.length < 2) return;
        const next = names[(names.indexOf(client._focus) + 1) % names.length];
        client.focus(next);
        focusButton.textContent = `Focus: ${next}`;
        log('info', `framed on ${next}`);
    });

    const playButton = document.getElementById('play');
    playButton.addEventListener('click', () => {
        client.setPlaying(!client.playing);
        playButton.textContent = client.playing ? 'Pause' : 'Play';
    });
    document.getElementById('stop').addEventListener('click', () => {
        client.stop();
        status.textContent = 'stopped';
        document.getElementById('stop').disabled = true;
    });

    setInterval(() => {
        const info = client.inspect();
        const s = info.stats;
        // Two lines, plus a third only when something is wrong. The rest of
        // what this used to show -- depth sorts, bytes decoded, render frames
        // -- is developer instrumentation; it is still on window.__vs4dVega.
        statsPane.textContent = [
            `frame   ${info.frameIndex + 1} / ${info.frameCount}`,
            `splats  ${s.splatsOnScreen.toLocaleString()}`,
            ...(s.decodeFailures ? [`FAILED  ${s.decodeFailures} frames`] : [])
        ].join('\n');
    }, 500);

    try {
        await client.start(config.objects);
    } catch (error) {
        status.textContent = `could not start: ${error.message}`;
        log('error', error.message);
        log('info', 'Export assets first: python -m baselines.Vega.orbitvega.'
            + 'export_quest --prepared-dir results/vega-gaussian/prepared-final '
            + '--output-dir results/vega-web --dataset-root <gaussian corpus>');
    }
}

main().catch(error => {
    document.getElementById('status').textContent = `fatal: ${error.message}`;
    console.error(error);
});
