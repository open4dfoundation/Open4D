'use strict';

/**
 * The browser user study: one participant through every chosen method, under
 * the same network trace, from the same start view, along the same camera path.
 *
 *   setup      operator: participant code, methods, trace, trial length
 *   record     the participant drives one unrated pass; that path is the one
 *              every trial replays -- so no method is rated from active viewing
 *              while the others are rated from passive viewing
 *   trials     in Williams order, blinded to a position label; each arms the
 *              trace on the server at its start, plays for the trial length,
 *              then asks the questionnaire
 *   done       a table, and the CSV
 *
 * Each method runs in a frame pointed at its own page with ?study=..., so the
 * study adds no second implementation of any method: what is rated is exactly
 * the page anyone else would open. Results come back by postMessage.
 */

const { runQuestionnaire } = require('./questionnaire-form');
const { MESSAGE_SOURCE } = require('./harness');
const { parseTrace, windowTrace, summarize } = require('../../../Server/study/trace');

/**
 * Methods whose page runs a study pass today. A method not listed here is
 * shown but cannot be chosen, with the reason -- rather than being offered and
 * then failing on its first trial.
 */
const STUDY_PAGES = new Set(['mesh', 'vivo', 'nava', 'vega']);
/** Baselines served by a restartable Python process, with its own object set. */
const POINTCLOUD = new Set(['vivo', 'nava']);
/**
 * Methods that load their whole clip before playing. Loaded before the trace
 * is armed, behind the same cover as everything else: under the trace, Vega's
 * 64 MB took 41 s at 12.5 Mbps, which made its trial the one with a long
 * wait -- identifiable, and several times longer than the rest.
 */
const PRELOAD = new Set(['vega']);
/** How long a preload may take before the trial is declared stuck. */
const PRELOAD_TIMEOUT_SECONDS = 300;
/**
 * Why a method that speaks the same protocol still cannot take part here.
 * Stated rather than leaving it off the list, so a missing method reads as a
 * known gap with a known fix, not as an oversight.
 */
const UNAVAILABLE = {
    livo: 'the browser client cannot decode LiVo\u2019s RGB-D segments yet, and no LiVo package is prepared',
    metastream: 'needs the RGB-D dataset from baselines.DeltaStream.orbitstream.converter',
    deltastream: 'needs the RGB-D dataset from baselines.DeltaStream.orbitstream.converter'
};

/**
 * WebGL implementations that rasterise on the CPU. Headless Chrome falls back
 * to SwiftShader, where the splat and mesh pages run at 2-9 fps; a trial
 * rendered that way measures the machine, not the method.
 */
const SOFTWARE_GL = /swiftshader|llvmpipe|softpipe|software rasterizer|microsoft basic render/i;

/** The GPU this browser renders WebGL with, as the driver names it. */
function glRenderer() {
    try {
        const gl = document.createElement('canvas').getContext('webgl2');
        const info = gl.getExtension('WEBGL_debug_renderer_info');
        return String(gl.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : gl.RENDERER));
    } catch {
        return null;
    }
}

/** How long past its length a trial may run before it is declared stuck. */
const TRIAL_GRACE_SECONDS = 180;

const $ = id => document.getElementById(id);

const state = {
    config: null, systems: [], session: null, trajectory: null,
    traceText: null, traceName: null, listener: null, glRenderer: null
};

// ------------------------------------------------------------------- http ---

async function api(method, route, body) {
    const response = await fetch(route, {
        method, cache: 'no-store',
        headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
        body: body === undefined ? undefined : JSON.stringify(body)
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(payload.error || `${route}: HTTP ${response.status}`);
    return payload;
}

// --------------------------------------------------------------------- ui ---

function show(section) {
    for (const id of ['setup', 'record', 'trial', 'questionnaire', 'done']) {
        $(id).hidden = id !== section;
    }
}

function fail(error) {
    const box = $('error');
    box.textContent = String(error?.message || error);
    box.hidden = false;
    // eslint-disable-next-line no-console
    console.error(error);
}

function clearError() { $('error').hidden = true; }

function where() {
    $('where').textContent = state.session
        ? `session ${state.session.id}` : 'no session';
}

/**
 * Mount a method page in a stage, replacing whatever was there, with a loading
 * cover over it. Startup takes seconds -- the first manifest alone can take ten
 * on a cold server -- and an uncovered frame is a black rectangle that reads as
 * broken. The cover comes off at the pass's first content.
 */
function mount(stageId, url, loadingText = 'Loading…') {
    const stage = $(stageId);
    stage.replaceChildren();
    const frame = document.createElement('iframe');
    frame.src = url;
    frame.allow = 'autoplay';
    const cover = document.createElement('div');
    cover.className = 'veil';
    cover.id = `${stageId}-cover`;
    cover.textContent = loadingText;
    stage.append(frame, cover);
    return frame;
}

function cover(stageId, text, { banner = false } = {}) {
    const node = $(`${stageId}-cover`);
    if (!node) return;
    if (text === null) { node.remove(); return; }
    node.textContent = text;
    node.classList.toggle('banner', banner);
}

/** Remove the frame so the method stops streaming and frees its GPU memory. */
function unmount(stageId, message) {
    const stage = $(stageId);
    stage.replaceChildren();
    const veil = document.createElement('div');
    veil.className = 'veil';
    veil.textContent = message;
    stage.appendChild(veil);
}

/**
 * Wait for the hosted page to say it has ended. Resolves with the message;
 * rejects on an error from the page, or if it runs far past its length.
 */
function awaitPass(durationSeconds, onProgress, onLoading) {
    return new Promise((resolve, reject) => {
        const timeout = setTimeout(() => {
            cleanup();
            reject(new Error(`no result after ${durationSeconds + TRIAL_GRACE_SECONDS} s; `
                + 'the method page may have stalled or failed to start'));
        }, (durationSeconds + TRIAL_GRACE_SECONDS) * 1000);
        const listener = event => {
            if (event.origin !== window.location.origin) return;
            const data = event.data;
            if (!data || data.source !== MESSAGE_SOURCE) return;
            if (data.type === 'progress') onProgress?.(data.t / data.duration);
            else if (data.type === 'loading') onLoading?.(data);
            else if (data.type === 'ended') { cleanup(); resolve(data); }
            else if (data.type === 'error') { cleanup(); reject(new Error(data.message)); }
        };
        const cleanup = () => {
            clearTimeout(timeout);
            window.removeEventListener('message', listener);
        };
        window.addEventListener('message', listener);
    });
}

/** Wait for one message of `type` from the hosted page; rejects on its error. */
function awaitMessage(type, timeoutSeconds) {
    return new Promise((resolve, reject) => {
        const timeout = setTimeout(() => {
            cleanup();
            reject(new Error(`the method page did not report ${type} within ${timeoutSeconds} s`));
        }, timeoutSeconds * 1000);
        const listener = event => {
            if (event.origin !== window.location.origin) return;
            const data = event.data;
            if (!data || data.source !== MESSAGE_SOURCE) return;
            if (data.type === type) { cleanup(); resolve(data); }
            else if (data.type === 'error') { cleanup(); reject(new Error(data.message)); }
        };
        const cleanup = () => {
            clearTimeout(timeout);
            window.removeEventListener('message', listener);
        };
        window.addEventListener('message', listener);
    });
}

function progress(barId, fraction) {
    $(barId).style.width = `${Math.max(0, Math.min(1, fraction)) * 100}%`;
}

/** Where a method's page lives, carrying what it needs to run one pass. */
function methodUrl(method, { trial = null, record = false, bridge = null, preload = false } = {}) {
    const system = state.systems.find(entry => entry.id === method);
    if (!system) throw new Error(`the server does not offer ${method}`);
    const url = new URL(system.page, window.location.origin);
    const socket = bridge || system.bridge;
    if (socket) url.searchParams.set('bridge', socket);
    url.searchParams.set('study', state.session.id);
    if (record) {
        url.searchParams.set('record', '1');
        url.searchParams.set('method', method);
    }
    else url.searchParams.set('trial', String(trial));
    if (preload) url.searchParams.set('preload', '1');
    return url.toString();
}

// ------------------------------------------------------------------ setup ---

function drawTrace(points) {
    const canvas = $('trace-spark');
    const width = (canvas.width = canvas.clientWidth * devicePixelRatio);
    const height = (canvas.height = canvas.clientHeight * devicePixelRatio);
    const context = canvas.getContext('2d');
    context.clearRect(0, 0, width, height);
    if (!points?.length) return;
    const end = Math.max(points[points.length - 1].time, 1);
    // Headroom, so a flat trace draws as a line at its level, not along the edge.
    const top = Math.max(...points.map(point => point.bandwidth)) * 1.25;
    context.strokeStyle = '#5b9dff';
    context.lineWidth = 2 * devicePixelRatio;
    context.beginPath();
    points.forEach((point, index) => {
        const x = (point.time / end) * width;
        const y = height - (point.bandwidth / top) * (height - 4) - 2;
        if (index === 0) context.moveTo(x, y);
        else {
            context.lineTo(x, height - (points[index - 1].bandwidth / top) * (height - 4) - 2);
            context.lineTo(x, y);
        }
    });
    context.lineTo(width, height - (points[points.length - 1].bandwidth / top) * (height - 4) - 2);
    context.stroke();
}

/** The trace as the session will see it: parsed, windowed, scaled. */
function previewTrace() {
    try {
        if (!state.traceText) { $('trace-summary').textContent = ''; drawTrace(null); return; }
        const points = windowTrace(parseTrace(state.traceText, { scale: Number($('trace-scale').value) || 1 }), {
            start: Number($('trace-start').value) || 0,
            end: Number($('trace-end').value) || Number($('duration').value) || Infinity
        });
        const summary = summarize(points);
        $('trace-summary').textContent = `${state.traceName}: ${summary.minMbps.toFixed(1)}`
            + `–${summary.maxMbps.toFixed(1)} Mbps over ${summary.durationSeconds} s `
            + `(${summary.points} steps), held at the last rate after that`;
        drawTrace(points);
    } catch (error) {
        $('trace-summary').textContent = `trace problem: ${error.message}`;
        drawTrace(null);
    }
}

async function chooseBuiltinTrace() {
    const name = $('trace-builtin').value;
    if (!name) return;
    const response = await fetch(`/api/study/traces/${encodeURIComponent(name)}`, { cache: 'no-store' });
    if (!response.ok) throw new Error(`could not load ${name}`);
    state.traceText = await response.text();
    state.traceName = name;
    $('trace-file').value = '';
    previewTrace();
}

/** The session's objects a method cannot show; empty when it has them all. */
function missingObjects(system) {
    const offered = (system?.objects || []).map(entry => (typeof entry === 'string' ? entry : entry.name));
    // An empty list is "not reported", not "none": a point-cloud baseline
    // names its objects only once its tile corpus is readable.
    if (!offered.length) return [];
    const have = new Set(offered);
    return chosenObjects().filter(name => !have.has(name));
}

function chosenObjects() {
    return $('objects').value.split(',').map(name => name.trim()).filter(Boolean);
}

function renderMethods() {
    const box = $('methods');
    const previouslyChosen = new Set([...box.querySelectorAll('input:checked')].map(input => input.value));
    const first = !box.childElementCount;
    box.replaceChildren();
    for (const [id, method] of Object.entries(state.config.methods)) {
        const system = state.systems.find(entry => entry.id === id);
        // A point-cloud baseline need not be running: each trial starts it on
        // the session's objects. It only needs its tile corpus to exist.
        const available = POINTCLOUD.has(id) ? system?.restartable : system?.ready;
        const missing = missingObjects(system);
        // Every method must show the same performers, or the trials are not
        // the same stage and the ratings compare different scenes.
        const why = UNAVAILABLE[id] ? UNAVAILABLE[id]
            : !method.followsTrajectory ? 'cannot follow a camera path'
            : !STUDY_PAGES.has(id) ? 'its page cannot run a study pass yet'
                : !available ? (system?.detail || 'not available on this server')
                    : missing.length ? `has no ${missing.join(', ')}`
                        : '';
        const input = document.createElement('input');
        input.type = 'checkbox';
        input.value = id;
        input.disabled = Boolean(why);
        input.checked = !why && (first || previouslyChosen.has(id));
        const label = document.createElement('label');
        label.append(input, document.createTextNode(method.name));
        if (why) {
            const reason = document.createElement('span');
            reason.className = 'why';
            reason.textContent = `— ${why}`;
            label.append(reason);
        }
        box.appendChild(label);
    }
    renderPracticeChoice();
}

/**
 * Which method runs the unrated practice pass that records the path. Any that
 * can take part will do -- the path is a path -- but the choice is the
 * operator's, because it matters: a participant has then seen that method
 * before rating it, and Ours in particular may not show the whole stage at
 * startup (see the README).
 */
function renderPracticeChoice() {
    const select = $('practice-method');
    const previous = select.value;
    const usable = [...$('methods').querySelectorAll('input')]
        .filter(input => !input.disabled).map(input => input.value);
    select.replaceChildren(...usable.map(id => new Option(state.config.methods[id].name, id)));
    select.value = usable.includes(previous) ? previous : (usable.includes('mesh') ? 'mesh' : usable[0] || '');
}

async function boot() {
    const [config, systems, sessions] = await Promise.all([
        api('GET', '/api/study/config'),
        api('GET', '/api/systems').then(body => body.systems || []).catch(() => []),
        api('GET', '/api/study/sessions').then(body => body.sessions).catch(() => [])
    ]);
    state.config = config;
    state.systems = systems;
    state.glRenderer = glRenderer();
    if (state.glRenderer && SOFTWARE_GL.test(state.glRenderer)) {
        fail(new Error(`This browser renders WebGL in software (${state.glRenderer}), so `
            + 'methods will play far below their frame rate. Use a browser with GPU '
            + 'acceleration for real sessions; trials run here are flagged.'));
    }
    renderMethods();

    const select = $('trace-builtin');
    select.replaceChildren(new Option('— choose —', ''));
    for (const trace of config.traces) {
        if (trace.error) continue;
        select.appendChild(new Option(
            `${trace.name}  (${trace.minMbps}–${trace.maxMbps} Mbps, ${trace.durationSeconds} s)`,
            trace.name));
    }
    const resume = $('resume');
    for (const session of sessions) {
        resume.appendChild(new Option(
            `${session.id} — ${session.done}/${session.trials} done`, session.id));
    }

    select.onchange = () => chooseBuiltinTrace().catch(fail);
    $('trace-file').onchange = async () => {
        const file = $('trace-file').files[0];
        if (!file) return;
        state.traceText = await file.text();
        state.traceName = file.name;
        select.value = '';
        previewTrace();
    };
    for (const id of ['trace-start', 'trace-end', 'trace-scale', 'duration']) {
        $(id).oninput = previewTrace;
    }
    // Which methods can show the chosen performers changes with the choice.
    $('objects').oninput = renderMethods;
    $('trajectory-file').onchange = async () => {
        const file = $('trajectory-file').files[0];
        state.trajectory = file ? JSON.parse(await file.text()) : null;
    };
    $('create').onclick = () => createSession().catch(fail);
    $('resume-go').onclick = () => resumeSession($('resume').value).catch(fail);
    $('done-new').onclick = () => window.location.reload();

    // Start with the first built-in trace, so the preview is never empty.
    const first = config.traces.find(trace => !trace.error);
    if (first) { select.value = first.name; await chooseBuiltinTrace(); }
    where();
    show('setup');
}

async function createSession() {
    clearError();
    if (!state.traceText) throw new Error('choose or upload a network trace');
    const methods = [...$('methods').querySelectorAll('input:checked')].map(input => input.value);
    const duration = Number($('duration').value);
    const objects = $('objects').value.split(',').map(name => name.trim()).filter(Boolean);
    if (!objects.length) throw new Error('name the objects on stage; every method must show the same ones');
    const session = await api('POST', '/api/study/sessions', {
        participant: $('participant').value.trim(),
        methods,
        durationSeconds: duration,
        objects,
        trace: {
            name: state.traceName, text: state.traceText,
            start: Number($('trace-start').value) || 0,
            end: Number($('trace-end').value) || duration,
            scale: Number($('trace-scale').value) || 1
        },
        trajectory: state.trajectory || undefined
    });
    state.practiceMethod = $('practice-method').value || 'mesh';
    await enter(session);
}

async function resumeSession(id) {
    clearError();
    if (!id) throw new Error('choose a session to resume');
    await enter(await api('GET', `/api/study/sessions/${encodeURIComponent(id)}`));
}

/** Go to the first step this session still needs. */
async function enter(session) {
    state.session = session;
    where();
    if (!session.trajectory) return recordStep();
    return nextTrial();
}

// ----------------------------------------------------------------- record ---

function recordStep() {
    show('record');
    unmount('record-stage', 'The view will open here.');
    progress('record-bar', 0);
    $('record-start').hidden = false;
    $('record-accept').hidden = true;
    $('record-again').hidden = true;
    $('record-download').hidden = true;
    $('record-start').onclick = () => recordPass().catch(fail);
    $('record-again').onclick = () => recordPass().catch(fail);
}

async function recordPass() {
    clearError();
    $('record-start').hidden = true;
    $('record-again').hidden = true;
    $('record-accept').hidden = true;
    const session = state.session;
    // Recorded with the link unshaped: the participant needs to see the stage
    // to choose a view, and this pass measures nothing.
    // Read now rather than at create time, so a resumed session honours it too.
    const method = $('practice-method').value || state.practiceMethod || 'mesh';
    let bridge = null;
    if (POINTCLOUD.has(method)) {
        cover('record-stage', 'Starting the viewer…');
        bridge = (await api('POST', '/api/pointcloud/start',
            { id: method, objects: session.objects })).bridge;
    }
    const done = awaitPass(session.durationSeconds,
        fraction => { cover('record-stage', null); progress('record-bar', fraction); },
        ({ seconds, visible, expected }) => cover('record-stage',
            `Loading the stage… ${visible} of ${expected} performers (${seconds} s). `
            + 'Recording starts once everyone is on stage.', { banner: visible > 0 }));
    mount('record-stage', methodUrl(method, { record: true, bridge }), 'Loading the stage…');
    const result = await done;
    unmount('record-stage', 'Path recorded.');
    if (!result.trajectory?.samples?.length) {
        throw new Error('the recording pass returned no camera path');
    }
    state.recorded = result.trajectory;
    const blob = new Blob([JSON.stringify(result.trajectory, null, 2)], { type: 'application/json' });
    const link = $('record-download');
    link.href = URL.createObjectURL(blob);
    link.download = `${session.id}-trajectory.json`;
    link.textContent = `download path (${result.trajectory.samples.length} samples)`;
    link.hidden = false;
    $('record-again').hidden = false;
    $('record-accept').hidden = false;
    $('record-accept').onclick = () => acceptRecording().catch(fail);
}

async function acceptRecording() {
    clearError();
    state.session = await api('PUT',
        `/api/study/sessions/${encodeURIComponent(state.session.id)}/trajectory`, state.recorded);
    await nextTrial();
}

// ----------------------------------------------------------------- trials ---

async function nextTrial() {
    const session = state.session;
    const trial = session.trials.find(entry => entry.status !== 'done');
    if (!trial) return finish();
    show('trial');
    progress('trial-bar', 0);
    $('trial-label').textContent = `Clip ${trial.label}`;
    $('trial-count').textContent = `· ${trial.position + 1} of ${session.trials.length}`;
    $('trial-status').textContent = '';
    $('trial-start').hidden = false;
    $('trial-retry').hidden = true;
    unmount('trial-stage', 'Press Start when you are ready.');
    $('trial-start').onclick = () => runTrial(trial).catch(error => trialFailed(trial, error));
}

async function runTrial(trial) {
    clearError();
    const session = state.session;
    $('trial-start').hidden = true;
    $('trial-status').textContent = 'starting…';
    // A point-cloud baseline is (re)started on exactly the session's objects,
    // before the trace is armed: it must show what every other method shows,
    // and it serves one connection from frame zero, so each trial gets a fresh
    // one. The restart is not part of the trial and is not shaped or timed.
    let bridge = null;
    if (POINTCLOUD.has(trial.method)) {
        $('trial-status').textContent = 'preparing…';
        const started = await api('POST', '/api/pointcloud/start',
            { id: trial.method, objects: session.objects });
        bridge = started.bridge;
    }
    // A preloading method loads its clip now, unshaped and untimed, like the
    // point-cloud restart above; its trial starts when it can play.
    let frame = null;
    let preload = null;
    if (PRELOAD.has(trial.method)) {
        $('trial-status').textContent = 'preparing…';
        const loaded = awaitMessage('preloaded', PRELOAD_TIMEOUT_SECONDS);
        const began = performance.now();
        frame = mount('trial-stage', methodUrl(trial.method, { trial: trial.position, preload: true }));
        const report = await loaded;
        preload = { seconds: Number(((performance.now() - began) / 1000).toFixed(2)),
                    bytes: report.bytes ?? null };
    }
    // Arming the trace is the trial's t=0: every method is shaped by the same
    // stretch of it, startup included.
    await api('POST', `/api/study/sessions/${encodeURIComponent(session.id)}/trials/${trial.position}/start`);
    const done = awaitPass(session.durationSeconds, fraction => {
        cover('trial-stage', null);
        progress('trial-bar', fraction);
        $('trial-status').textContent = 'playing';
    }, ({ seconds }) => cover('trial-stage', `Loading… ${seconds} s`));
    if (frame) {
        frame.contentWindow.postMessage({ source: MESSAGE_SOURCE, type: 'go' }, window.location.origin);
    } else {
        mount('trial-stage', methodUrl(trial.method, { trial: trial.position, bridge }));
    }
    const result = await done;
    unmount('trial-stage', 'Clip finished.');
    $('trial-status').textContent = '';

    show('questionnaire');
    const response = await runQuestionnaire({ config: state.config.questionnaire, label: trial.label, $ });
    const metrics = {
        ...scalars(result.summary),
        ...flatten('native_', result.native),
        broadcastId: result.broadcastId ?? null,
        exitCode: result.exitCode ?? null,
        glRenderer: state.glRenderer ?? null,
        softwareRendering: state.glRenderer ? SOFTWARE_GL.test(state.glRenderer) : null,
        preloadedBeforeTrial: preload !== null,
        preloadSeconds: preload?.seconds ?? null,
        preloadBytes: preload?.bytes ?? null
    };
    const finished = await api('POST',
        `/api/study/sessions/${encodeURIComponent(session.id)}/trials/${trial.position}/finish`,
        { metrics, questionnaire: response });
    state.session = finished.session;
    await nextTrial();
}

async function trialFailed(trial, error) {
    fail(error);
    unmount('trial-stage', 'This clip did not complete.');
    // Free the link, so a retry starts from the beginning of the trace.
    await api('POST', '/api/study/abort').catch(() => {});
    show('trial');
    $('trial-start').hidden = true;
    $('trial-retry').hidden = false;
    $('trial-retry').onclick = () => {
        $('trial-retry').hidden = true;
        runTrial(trial).catch(next => trialFailed(trial, next));
    };
}

function scalars(object) {
    return Object.fromEntries(Object.entries(object || {}).filter(([, value]) =>
        value === null || ['number', 'string', 'boolean'].includes(typeof value)));
}

/** Nested native metrics as flat, prefixed scalar columns. */
function flatten(prefix, object, out = {}) {
    if (!object || typeof object !== 'object') return out;
    for (const [key, value] of Object.entries(object)) {
        if (value && typeof value === 'object' && !Array.isArray(value)) {
            flatten(`${prefix}${key}_`, value, out);
        } else if (value === null || ['number', 'string', 'boolean'].includes(typeof value)) {
            out[`${prefix}${key}`] = value;
        }
    }
    return out;
}

// ------------------------------------------------------------------- done ---

async function finish() {
    show('done');
    const session = state.session;
    const encoded = encodeURIComponent(session.id);
    const { trials } = await api('GET', `/api/study/sessions/${encoded}/trials`);
    const columns = [
        ['clip', record => record.label],
        ['method', record => record.method],
        ...state.config.questionnaire.ratings.map(rating =>
            [rating.id, record => record.questionnaire.ratings[rating.id]]),
        ['artifacts', record => record.questionnaire.artifacts.join(', ')],
        ['startup ms', record => record.metrics.startupDelayMs],
        ['freeze ratio', record => record.metrics.freezeRatio],
        ['longest freeze ms', record => record.metrics.longestFreezeMs],
        ['content fps', record => record.metrics.contentAdvanceFps]
    ];
    const table = $('done-table');
    table.replaceChildren();
    const head = table.createTHead().insertRow();
    for (const [name] of columns) {
        const cell = document.createElement('th');
        cell.textContent = name;
        head.appendChild(cell);
    }
    const body = table.createTBody();
    for (const record of trials) {
        const line = body.insertRow();
        for (const [name, value] of columns) {
            const cell = line.insertCell();
            const shown = value(record);
            cell.textContent = shown === null || shown === undefined ? '' : String(shown);
            if (!['clip', 'method', 'artifacts'].includes(name)) cell.className = 'num';
        }
    }
    $('done-lead').textContent = `${trials.length} trial${trials.length === 1 ? '' : 's'} for `
        + `${session.participant}, under ${session.trace.name}. Every method replayed the same `
        + `${session.trajectory?.samples ?? '?'}-sample camera path.`;
    $('done-csv').href = `/api/study/sessions/${encoded}/export.csv`;
    $('done-path').href = `/api/study/sessions/${encoded}/trajectory`;
    $('done-path').download = `${session.id}-trajectory.json`;
}

boot().catch(fail);
