'use strict';

/**
 * Glue between a method page and the study page that hosts it in a frame.
 *
 *   ?study=<session>&trial=<position>   replay the shared path, as trial n
 *   ?study=<session>&record=1           the unrated pass that records the path
 *   ...&preload=1                       load the whole clip, say 'preloaded',
 *                                       and play only when the host says 'go'
 *
 * Everything the page needs comes from the server by session id -- the object
 * subset, the trial length, the path -- so a trial URL is complete on its own
 * and can be opened outside the study page to debug one condition. Results go
 * to the hosting page by postMessage; recording them is the study page's job,
 * because it also holds the questionnaire answer that belongs with them.
 */

const { stagePose } = require('./camera');
const { StudyDriver } = require('./driver');

/**
 * One aspect for every trial and for the recording. The start pose fits the
 * narrower field of view, so a different aspect would crop the "same" pose
 * differently; the study page sizes its frame to this.
 */
const STUDY_ASPECT = 16 / 9;
const MESSAGE_SOURCE = 'vs4d-study';

function studyParams(search = globalThis.location?.search || '') {
    const params = new URLSearchParams(search);
    const session = params.get('study');
    if (!session) return null;
    const record = params.get('record') === '1';
    const trial = params.get('trial');
    if (!record && trial === null) {
        throw new Error('?study needs either &trial=<position> or &record=1');
    }
    // Which method the recording pass runs on, so the path records it.
    return { session, record, trial: record ? null : Number(trial), method: params.get('method'),
             preload: !record && params.get('preload') === '1' };
}

async function getJson(serverUrl, route) {
    const response = await fetch(`${serverUrl}${route}`, { cache: 'no-store' });
    const body = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(body.error || `${route}: HTTP ${response.status}`);
    return body;
}

/** Everything a method page needs to run one study pass. */
async function loadStudyContext({ serverUrl, params }) {
    const encoded = encodeURIComponent(params.session);
    const [session, layout] = await Promise.all([
        getJson(serverUrl, `/api/study/sessions/${encoded}`),
        getJson(serverUrl, '/api/study/layout')
    ]);
    const trajectory = params.record
        ? null : await getJson(serverUrl, `/api/study/sessions/${encoded}/trajectory`);
    const trial = params.record ? null : session.trials[params.trial];
    if (!params.record && !trial) throw new Error(`session has no trial ${params.trial}`);
    return {
        session,
        trial,
        trajectory,
        mode: params.record ? 'record' : 'replay',
        method: params.record ? params.method : trial.method,
        durationSeconds: session.durationSeconds,
        preload: Boolean(params.preload),
        startPose: stagePose(layout, session.objects, { aspect: STUDY_ASPECT })
    };
}

/**
 * Show nothing but the view. Every method page carries chrome that names it --
 * the baseline page's reads "ViVo / NAVA over the V4DS protocol" -- and a
 * participant who can read which system they are watching is no longer blind
 * to it. Hidden with a stylesheet rather than removed, because the page's own
 * code still writes to those elements.
 */
function enterStudyChrome(canvasId = 'view') {
    const style = document.createElement('style');
    style.textContent = `
        html, body { margin: 0; padding: 0; overflow: hidden; background: #000; }
        body > :not(#${canvasId}) { display: none !important; }
        #${canvasId} { position: fixed; inset: 0; width: 100vw !important;
                       height: 100vh !important; display: block; }`;
    document.head.appendChild(style);
}

/** Tell the hosting study page something. A no-op when not in a frame. */
function post(type, payload = {}) {
    if (!globalThis.parent || globalThis.parent === globalThis) return;
    globalThis.parent.postMessage({ source: MESSAGE_SOURCE, type, ...payload },
        globalThis.location.origin);
}

/**
 * Resolve when the hosting study page says 'go'. A preloading page waits on
 * this between loading its clip and playing it: the host arms the trace in
 * between, so the load is not shaped and the trial starts at playback.
 */
function awaitGo() {
    return new Promise(resolve => {
        const listener = event => {
            if (event.origin !== globalThis.location.origin) return;
            if (event.data?.source !== MESSAGE_SOURCE || event.data.type !== 'go') return;
            globalThis.removeEventListener('message', listener);
            resolve();
        };
        globalThis.addEventListener('message', listener);
    });
}

/**
 * Build the driver for this pass, and report its progress to the host.
 *
 * @param {object} args
 * @param {object} args.context from loadStudyContext
 * @param {string} args.method this page's system id
 * @param {(summary: object) => void} args.onEnded
 */
function createStudyDriver({ context, method, onEnded }) {
    let lastReport = -1;
    const driver = new StudyDriver({
        mode: context.mode,
        startPose: context.startPose,
        trajectory: context.trajectory,
        durationSeconds: context.durationSeconds,
        method,
        createdAt: performance.now(),
        onEnded,
        waitForObjects: context.mode === 'record' ? context.session.objects.length : 0
    });
    // Wrapped rather than subclassed, so the driver stays free of the DOM.
    const apply = driver.apply.bind(driver);
    driver.apply = (camera, controls, info) => {
        apply(camera, controls, info);
        if (driver.firstContentAt === null) {
            // Still loading: say how far, once a second, so the host can show it.
            const waited = Math.floor((info.now - driver.createdAt) / 1000);
            if (waited !== lastReport) {
                lastReport = waited;
                post('loading', { seconds: waited, visible: driver.visible,
                                  expected: context.session.objects.length });
            }
            return;
        }
        const second = Math.floor(driver.t) + 100000;
        if (second !== lastReport) {
            lastReport = second;
            post('progress', { t: driver.t, duration: driver.durationSeconds });
        }
    };
    return driver;
}

module.exports = {
    studyParams, loadStudyContext, createStudyDriver, post, awaitGo, enterStudyChrome,
    STUDY_ASPECT, MESSAGE_SOURCE
};
