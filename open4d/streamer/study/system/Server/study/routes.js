'use strict';

/**
 * HTTP surface of the browser study, mounted by server.js at /api/study.
 *
 * One trial runs at a time, because there is one link: the shaper is the
 * process-wide bottleneck every shaped response passes through, and a second
 * trial arming it would reset the first one's trace mid-run. Starting a trial
 * while another is live is therefore refused rather than allowed to corrupt it.
 */

const express = require('express');
const fs = require('fs');
const path = require('path');
const mime = require('mime-types');

const questionnaire = require('./questionnaire');
const { StudyStore, METHODS } = require('./store');
const { summarize, parseTrace } = require('./trace');

/**
 * `/files` through the shaper, while a trial is armed.
 *
 * Unarmed, it steps aside and express.static serves exactly as before, so the
 * demo pages are untouched by this existing. Armed, it serves the file itself
 * so every byte crosses the bucket. A Range header is ignored and the whole
 * entity sent, which HTTP permits; the segment client never asks for ranges,
 * and serving one unshaped would be a hole in the link.
 */
function shapedStatic(root, shaper) {
    const base = path.resolve(root);
    return (req, res, next) => {
        if (!shaper.armed || !['GET', 'HEAD'].includes(req.method)) return next();
        let relative;
        try { relative = decodeURIComponent(req.path); } catch { return next(); }
        const file = path.resolve(base, `.${relative}`);
        if (file !== base && !file.startsWith(base + path.sep)) return res.status(403).end();
        fs.stat(file, (error, stat) => {
            if (error || !stat.isFile()) return next();
            res.status(200);
            res.set({
                'Content-Type': mime.lookup(file) || 'application/octet-stream',
                'Content-Length': String(stat.size),
                // The segment loop must keep generating real load; a cached
                // segment is a free one, and the link would stop meaning anything.
                'Cache-Control': 'no-store',
                'X-Study-Shaped': '1'
            });
            if (req.method === 'HEAD') return res.end();
            const source = fs.createReadStream(file);
            const paced = shaper.pace();
            const abort = () => { source.destroy(); paced.destroy(); };
            res.on('close', abort);
            source.on('error', abort);
            source.pipe(paced).pipe(res);
        });
    };
}

function builtInTraces(directory) {
    if (!directory || !fs.existsSync(directory)) return [];
    return fs.readdirSync(directory)
        .filter(name => name.endsWith('.csv'))
        .sort()
        .map(name => {
            try {
                return { name, ...summarize(parseTrace(fs.readFileSync(path.join(directory, name), 'utf8'))) };
            } catch (error) {
                return { name, error: error.message };
            }
        });
}

/**
 * @param {object} args
 * @param {string} args.resultsRoot where sessions are stored
 * @param {import('./shaper').TraceShaper} args.shaper
 * @param {string} [args.tracesDir] built-in traces offered beside uploads
 * @param {string} [args.layoutFile] scene_layout.json, for the start pose
 * @param {(method: string) => Promise<object|null>} [args.bridgeShaping] the
 *   shaper status of the bridge carrying `method`, or null if none does
 */
function createStudyRouter({ resultsRoot, shaper, tracesDir, layoutFile,
                             bridgeShaping = async () => null }) {
    const store = new StudyStore(resultsRoot);
    const router = express.Router();
    router.use(express.json({ limit: '8mb' }));
    // Which trial owns the link right now, if any.
    let live = null;

    const wrap = handler => (req, res) => {
        try {
            const out = handler(req, res);
            if (out !== undefined) res.set('Cache-Control', 'no-store').json(out);
        } catch (error) {
            res.status(error.statusCode || 400).json({ error: error.message });
        }
    };

    router.get('/config', wrap(() => ({
        methods: METHODS,
        questionnaire: {
            ratings: questionnaire.RATINGS,
            artifacts: questionnaire.ARTIFACTS,
            artifactPrompt: questionnaire.ARTIFACT_PROMPT,
            min: questionnaire.RATING_MIN,
            max: questionnaire.RATING_MAX,
            maxArtifacts: questionnaire.MAXIMUM_ARTIFACT_SELECTIONS
        },
        traces: builtInTraces(tracesDir)
    })));

    // Where every object stands, so a page can frame the whole stage before any
    // of it has downloaded -- the start pose must not depend on what arrived.
    router.get('/layout', (req, res) => {
        if (!layoutFile || !fs.existsSync(layoutFile)) {
            return res.status(404).json({ error: 'no scene_layout.json on this server' });
        }
        res.set('Cache-Control', 'no-store').type('application/json')
            .send(fs.readFileSync(layoutFile, 'utf8'));
    });

    router.get('/traces/:name', (req, res) => {
        const name = path.basename(String(req.params.name));
        const file = tracesDir && path.join(tracesDir, name);
        if (!file || !name.endsWith('.csv') || !fs.existsSync(file)) {
            return res.status(404).json({ error: `no built-in trace ${name}` });
        }
        res.type('text/csv').set('Cache-Control', 'no-store').send(fs.readFileSync(file, 'utf8'));
    });

    router.get('/sessions', wrap(() => ({ sessions: store.listSessions() })));
    router.post('/sessions', wrap(req => store.createSession(req.body || {})));
    router.get('/sessions/:id', wrap(req => store.session(req.params.id)));
    router.get('/sessions/:id/trajectory', wrap(req => store.trajectory(req.params.id)));
    router.get('/sessions/:id/trials', wrap(req => ({ trials: store.trialRecords(req.params.id) })));
    router.put('/sessions/:id/trajectory',
        wrap(req => store.saveTrajectory(req.params.id, req.body)));

    router.post('/sessions/:id/trials/:position/start', wrap(req => {
        if (live && !(live.session === req.params.id
            && live.position === Number(req.params.position))) {
            throw Object.assign(new Error(
                `trial ${live.label} of ${live.session} owns the link; finish or abort it first`),
            { statusCode: 409 });
        }
        const { session, trial } = store.startTrial(req.params.id, req.params.position);
        // Armed at the start of *every* trial, so each method plays the same
        // stretch of trace from its first second -- the Quest study re-arms
        // the trace player per trial for the same reason.
        shaper.arm(session.trace.points, {
            hold: session.trace.hold, label: `${session.id} ${trial.label}`
        });
        live = { session: session.id, position: trial.position, label: trial.label };
        return { trial, shaping: shaper.status(), trace: session.trace.points };
    }));

    router.post('/sessions/:id/trials/:position/finish', async (req, res) => {
        try {
            if (!live || live.session !== req.params.id || live.position !== Number(req.params.position)) {
                throw Object.assign(new Error('this trial does not own the live link'), { statusCode: 409 });
            }
            // Read before disarming: the bridge follows this shaper, and once
            // it sees the link released it resets its own counters.
            const shaping = shaper.status();
            const trial = store.session(req.params.id).trials[Number(req.params.position)];
            const bridge = trial && await bridgeShaping(trial.method)
                .catch(error => ({ error: error.message }));
            if (bridge) {
                shaping.bridge = { ...bridge,
                    matchesTrial: bridge.label === shaping.label && bridge.label !== null };
            }
            const result = store.finishTrial(req.params.id, req.params.position, {
                ...(req.body || {}), shaping
            });
            shaper.disarm();
            live = null;
            res.set('Cache-Control', 'no-store').json(result);
        } catch (error) {
            res.status(error.statusCode || 400).json({ error: error.message });
        }
    });

    /** Release the link after an abandoned trial, without recording it. */
    router.post('/abort', wrap(() => {
        shaper.disarm();
        const was = live;
        live = null;
        return { aborted: was, shaping: shaper.status() };
    }));

    /**
     * What the link is doing, for anything else that must follow it. The
     * ViVo/NAVA bridge polls this so its socket follows the same trace from
     * the same instant as the HTTP responses.
     */
    router.get('/shaping', wrap(() => ({
        ...shaper.status(),
        startedAt: shaper.armed ? shaper._startedAt : null,
        points: shaper.armed ? shaper._points : null,
        live
    })));

    router.get('/sessions/:id/export.csv', (req, res) => {
        try {
            const csv = store.exportCsv(req.params.id);
            res.type('text/csv')
                .set('Content-Disposition', `attachment; filename="${req.params.id}.csv"`)
                .send(csv);
        } catch (error) {
            res.status(error.statusCode || 400).json({ error: error.message });
        }
    });

    return router;
}

module.exports = { createStudyRouter, shapedStatic };
