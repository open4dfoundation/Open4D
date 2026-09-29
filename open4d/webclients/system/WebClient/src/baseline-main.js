'use strict';

/**
 * Entry point for the V4DS baseline viewer.
 *
 * Separate from `main.js` because the two are genuinely different clients: that
 * one drives our HTTP segment ladder through ClientCore, this one consumes a
 * pushed socket stream. Sharing an entry would mean a mode flag that changes
 * almost everything.
 *
 *   /web/baseline.html?bridge=ws://host:8790
 *
 * | parameter | default                    | meaning                        |
 * |-----------|----------------------------|--------------------------------|
 * | bridge    | ws://<page host>:8790      | v4ds-bridge WebSocket address  |
 * | pointSize | 0.012                      | point size in metres           |
 * | strict    | 0                          | 1 = abort on a frame gap       |
 */

const { BaselineClient } = require('./baseline-client');
const { mountLinkRate } = require('./link-rate');
const study = require('./study/harness');

function readConfig() {
    const params = new URLSearchParams(window.location.search);
    const host = window.location.hostname || '127.0.0.1';
    const number = (name, fallback) => {
        const value = Number(params.get(name));
        return params.get(name) !== null && Number.isFinite(value) ? value : fallback;
    };
    return {
        bridgeUrl: params.get('bridge') || `ws://${host}:8790`,
        pointSize: number('pointSize', 0.012),
        strictOrder: params.get('strict') === '1'
    };
}

function createUi() {
    const status = document.getElementById('status');
    const logPane = document.getElementById('log');
    const statsPane = document.getElementById('stats');
    return {
        setStatus: text => { status.textContent = text; },
        log(level, message) {
            const row = document.createElement('div');
            row.className = `log-line log-${level}`;
            row.textContent = `[${new Date().toISOString().slice(11, 23)}] ${message}`;
            logPane.appendChild(row);
            while (logPane.childElementCount > 300) {
                logPane.removeChild(logPane.firstChild);
            }
            logPane.scrollTop = logPane.scrollHeight;
        },
        setStats(lines) { statsPane.textContent = lines.join('\n'); }
    };
}

async function main() {
    const config = readConfig();
    const ui = createUi();
    // A study pass (see src/study/harness.js). Which baseline this is -- ViVo
    // or NAVA -- is the session's to say; the page is the same for both.
    const studyParams = study.studyParams();
    const studyContext = studyParams
        ? await study.loadStudyContext({ serverUrl: window.location.origin, params: studyParams })
        : null;
    if (studyContext) study.enterStudyChrome('view');
    const method = studyContext?.method || 'baseline';
    let driver = null;
    let studyOver = false;
    let client = null;
    const finishStudy = () => {
        if (studyOver) return;
        studyOver = true;
        client?.stop();
        const stats = client ? client.inspect().stats : {};
        const native = Object.fromEntries(Object.entries(stats).filter(([, value]) =>
            value === null || ['number', 'string', 'boolean'].includes(typeof value)));
        study.post('ended', {
            method, exitCode: 0, broadcastId: null,
            summary: driver.summary(), native: { stats: native }, trajectory: driver.trajectory()
        });
    };
    const stopLinkRate = mountLinkRate(document.getElementById('link'));
    ui.setStatus(`connecting to ${config.bridgeUrl} …`);

    client = new BaselineClient({
        bridgeUrl: config.bridgeUrl,
        canvas: document.getElementById('view'),
        pointSize: config.pointSize,
        strictOrder: config.strictOrder,
        autoFrame: !studyContext,
        onEvent: event => {
            if (driver && event.type === 'header') study.post('ready', { method, mode: driver.mode });
            // The trial runs on its own clock: a baseline that closes early is
            // measured as the freeze it is, not allowed to end the trial. Only
            // closing before anything arrived is a failure to start.
            if (driver && event.type === 'closed' && driver.firstContentAt === null && !studyOver) {
                studyOver = true;
                study.post('error', { method,
                    message: `${method} closed before any content (${event.code}: ${event.reason})` });
            }
            switch (event.type) {
                case 'open':
                    ui.log('info', `bridge connected: ${event.url}`);
                    ui.setStatus('waiting for the stream header …');
                    break;
                case 'header':
                    ui.log('info', `${event.mode.toUpperCase()} · `
                        + `${event.objects.length} objects · ${event.fps} fps · `
                        + `source ${event.source}`
                        + (event.tiled ? ' · tiled/ABR' : ''));
                    for (const o of event.objects) {
                        ui.log('info', `  object ${o.objectId} ${o.name}: `
                            + `${o.cameras} cameras, ${o.loopFrames} frames`);
                    }
                    ui.setStatus(`streaming ${event.mode}`);
                    break;
                case 'resync':
                    ui.log('warn', `resynchronising: ${event.message}`);
                    break;
                case 'unsupported':
                    ui.log('warn', event.message);
                    break;
                case 'error':
                    ui.log('error', event.message);
                    break;
                case 'closed':
                    ui.log('warn', `bridge closed (${event.code}): ${event.reason}`);
                    ui.setStatus('disconnected');
                    break;
                default:
                    break;
            }
        }
    });

    if (studyContext) {
        driver = study.createStudyDriver({ context: studyContext, method, onEnded: finishStudy });
        client.renderer.setCameraDriver(driver);
    }
    window.__vs4dBaseline = client;
    window.__vs4dBaseline.study = driver;

    document.getElementById('stop').addEventListener('click', () => {
        stopLinkRate();
        client.stop();
        ui.setStatus('stopped');
        document.getElementById('stop').disabled = true;
    });

    setInterval(() => {
        const info = client.inspect();
        const s = info.stats;
        // Four lines. Frame/resync/drop counters and byte totals were useful
        // while bringing the protocol up and are noise now; they remain on
        // window.__vs4dBaseline. Faults appear only when non-zero, so a clean
        // run stays quiet and a broken one still says so.
        const faults = [
            s.decodeFailures ? `${s.decodeFailures} decode` : null,
            s.resyncs ? `${s.resyncs} resync` : null,
            s.framesDropped ? `${s.framesDropped} dropped` : null
        ].filter(Boolean);
        ui.setStats([
            `system   ${info.mode ?? '-'}`,
            `rate     ${s.measuredFps.toFixed(1)} fps`,
            `goodput  ${s.goodputMbps ? s.goodputMbps.toFixed(1) + ' Mbps' : '-'}`,
            `points   ${info.renderer.objects
                .reduce((sum, o) => sum + o.points, 0).toLocaleString()}`,
            ...(faults.length ? [`FAULTS   ${faults.join(', ')}`] : [])
        ]);
    }, 500);

    try {
        await client.start();
    } catch (err) {
        if (driver) study.post('error', { method, message: `could not connect: ${err.message}` });
        ui.setStatus(`could not connect: ${err.message}`);
        ui.log('error', 'Is the bridge running? '
            + 'node system/WebClient/bridge/v4ds-bridge.js --baseline-port <port>');
    }
}

main().catch(err => {
    document.getElementById('status').textContent = `fatal: ${err.message}`;
    study.post('error', { method: 'baseline', message: err.message });
    console.error(err);
});
