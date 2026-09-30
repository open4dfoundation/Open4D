'use strict';

/**
 * Entry point for the NeVo viewer.
 *
 *   /web/nevo.html?object=g_dancer
 *
 * | parameter | default       | meaning                                    |
 * |-----------|---------------|--------------------------------------------|
 * | assets    | /nevo-assets  | URL prefix serving the render output root  |
 * | object    | g_dancer      | clip directory name                        |
 * | fps       | 8             | playback rate                              |
 * | nevoOnly  | 0             | 1 shows only NeVo's own filtered output    |
 */

const { NevoClient } = require('./nevo-client');

async function main() {
    const params = new URLSearchParams(window.location.search);
    const status = document.getElementById('status');
    const logPane = document.getElementById('log');
    const statsPane = document.getElementById('stats');

    const log = (level, message) => {
        const row = document.createElement('div');
        row.className = `log-line log-${level}`;
        row.textContent = message;
        logPane.appendChild(row);
        while (logPane.childElementCount > 200) {
            logPane.removeChild(logPane.firstChild);
        }
        logPane.scrollTop = logPane.scrollHeight;
    };

    const fps = Number(params.get('fps'));
    const client = new NevoClient({
        assetBase: params.get('assets') || '/nevo-assets',
        canvas: document.getElementById('view'),
        object: params.get('object') || 'g_dancer',
        nevoOnly: params.get('nevoOnly') === '1',
        fps: Number.isFinite(fps) && fps > 0 ? fps : 8,
        onEvent: event => {
            if (event.type === 'manifest') {
                log('info', `${event.name} · ${event.representation}`);
                log('info', `${event.frames} frames · ${event.source} · `
                    + `view ${event.view}`
                    + (event.viewInTrainingSet
                        ? ' (in the training set)' : ' (held out)'));
                for (const kept of event.keptFractions) {
                    log('info', `  ${kept.label}: kept `
                        + `${(kept.keptFraction * 100).toFixed(1)}% of voxels`);
                }
                status.textContent = 'loading renders …';
            } else if (event.type === 'ready') {
                log('info', `${event.loaded} renders loaded`
                    + (event.failed ? `, ${event.failed} missing` : ''));
                status.textContent = 'playing pre-rendered frames';
            } else if (event.type === 'error') {
                log('error', event.message);
            }
        }
    });
    window.__vs4dNevo = client;

    const playButton = document.getElementById('play');
    playButton.addEventListener('click', () => {
        client.setPlaying(!client.playing);
        playButton.textContent = client.playing ? 'Pause' : 'Play';
    });

    setInterval(() => {
        const info = client.inspect();
        statsPane.textContent = [
            `clip    ${info.object}`,
            `frame   ${info.frameIndex + 1} / ${info.frameCount}`,
            ...(info.stats.imagesFailed
                ? [`MISSING ${info.stats.imagesFailed} renders`] : [])
        ].join('\n');
    }, 500);

    try {
        await client.start();
    } catch (error) {
        status.textContent = `could not start: ${error.message}`;
        log('error', error.message);
        log('info', 'Renders come from orbitnevo/render_frames.py; point '
            + '?assets= at its output root (default ~/nevo_output, served at '
            + '/nevo-assets).');
    }
}

main().catch(error => {
    document.getElementById('status').textContent = `fatal: ${error.message}`;
    console.error(error);
});
