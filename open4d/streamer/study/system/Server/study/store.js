'use strict';

/**
 * Study sessions on disk: one directory per session, self-contained.
 *
 *   <root>/<session>/session.json     who, what order, which trace, which path
 *   <root>/<session>/trace.csv        the trace exactly as uploaded
 *   <root>/<session>/trajectory.json  the camera path every method replays
 *   <root>/<session>/trials/<NN>-<label>/trial.json   metrics + questionnaire
 *
 * The uploaded bytes are kept verbatim beside their hash, so a result can be
 * traced to the exact link it ran under even after the trace file is edited.
 *
 * Privacy follows docs/user-study-data.md: a participant is a pseudonymous
 * code, and nothing identifying -- no name, no address, no IP -- is written.
 */

const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const questionnaire = require('./questionnaire');
const { participantIndex, trialOrder } = require('./order');
const { parseTrace, windowTrace, summarize } = require('./trace');

const SCHEMA_VERSION = 1;
const PARTICIPANT = /^[A-Za-z0-9._-]{1,40}$/;
const SESSION_ID = /^[A-Za-z0-9._-]{1,80}$/;

/**
 * Every method the comparison page knows, and whether it can follow a camera
 * path. NeVo plays pre-rendered panels with no camera control
 * (`nevo-client.js`), so it cannot take part when every method must see the
 * same trajectory; offering it would put a fixed view beside moving ones and
 * call them comparable.
 */
const METHODS = Object.freeze({
    mesh: { name: 'Ours', followsTrajectory: true },
    vivo: { name: 'ViVo', followsTrajectory: true },
    nava: { name: 'NAVA', followsTrajectory: true },
    vega: { name: 'Vega', followsTrajectory: true },
    nevo: { name: 'NeVo', followsTrajectory: false },
    // Speak the V4DS protocol like ViVo and NAVA, but cannot run here yet;
    // listed so the study page can say why rather than leave them out.
    livo: { name: 'LiVo', followsTrajectory: true },
    metastream: { name: 'MetaStream', followsTrajectory: true },
    deltastream: { name: 'DeltaStream', followsTrajectory: true }
});

const TRAJECTORY_SPACE = 'three-world-metres-y-up';

function sha256(text) {
    return crypto.createHash('sha256').update(text).digest('hex');
}

function timestamp(date = new Date()) {
    return date.toISOString().replace(/[-:]/g, '').replace(/\.\d+Z$/, 'Z');
}

function vector(value, name) {
    if (!Array.isArray(value) || value.length !== 3 || !value.every(Number.isFinite)) {
        throw new Error(`${name} must be three finite numbers`);
    }
    return value.map(Number);
}

/**
 * Throw unless `trajectory` is a camera path every renderer can replay.
 *
 * Samples are keyed on *playback* time -- seconds of content shown -- not on
 * wall time since connecting, as the Quest's offline benchmark does: methods
 * start up at different speeds, and a path keyed on connection time would put
 * the camera somewhere else relative to the performers in each of them.
 */
function validateTrajectory(trajectory) {
    if (!trajectory || trajectory.schemaVersion !== 1) {
        throw new Error('trajectory must have schemaVersion 1');
    }
    if (trajectory.coordinateSpace !== TRAJECTORY_SPACE) {
        throw new Error(`trajectory coordinateSpace must be "${TRAJECTORY_SPACE}"`);
    }
    const samples = trajectory.samples;
    if (!Array.isArray(samples) || samples.length < 2) {
        throw new Error('trajectory needs at least two samples');
    }
    let previous = -Infinity;
    const clean = samples.map((sample, index) => {
        const t = Number(sample?.t);
        if (!Number.isFinite(t) || t < 0) throw new Error(`sample ${index}: bad time`);
        if (t < previous) throw new Error(`sample ${index}: time moves backwards`);
        previous = t;
        const fov = Number(sample.fovDegrees);
        if (!(fov > 1 && fov < 179)) throw new Error(`sample ${index}: bad field of view`);
        return {
            t,
            position: vector(sample.position, `sample ${index} position`),
            target: vector(sample.target, `sample ${index} target`),
            up: vector(sample.up ?? [0, 1, 0], `sample ${index} up`),
            fovDegrees: fov
        };
    });
    return {
        schemaVersion: 1,
        coordinateSpace: TRAJECTORY_SPACE,
        scene: trajectory.scene ?? null,
        durationSeconds: clean[clean.length - 1].t,
        recordedWith: trajectory.recordedWith ?? null,
        samples: clean
    };
}

class StudyStore {
    constructor(root) {
        this.root = path.resolve(root);
        fs.mkdirSync(this.root, { recursive: true });
    }

    _dir(sessionId) {
        if (!SESSION_ID.test(String(sessionId || ''))) {
            throw Object.assign(new Error('invalid session id'), { statusCode: 400 });
        }
        return path.join(this.root, sessionId);
    }

    _read(sessionId, name) {
        const file = path.join(this._dir(sessionId), name);
        if (!fs.existsSync(file)) {
            throw Object.assign(new Error(`${name} not found`), { statusCode: 404 });
        }
        return JSON.parse(fs.readFileSync(file, 'utf8'));
    }

    _write(sessionId, name, value) {
        const file = path.join(this._dir(sessionId), name);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        // Written beside and renamed over, so a crash mid-write cannot leave a
        // half-file that parses as a truncated session.
        const partial = `${file}.partial`;
        fs.writeFileSync(partial, `${JSON.stringify(value, null, 2)}\n`);
        fs.renameSync(partial, file);
    }

    /**
     * @param {object} spec
     * @param {string} spec.participant pseudonymous code, e.g. "p07"
     * @param {string[]} spec.methods ids from METHODS
     * @param {number} spec.durationSeconds how long each trial plays
     * @param {{name: string, text: string, start?: number, end?: number, scale?: number}} spec.trace
     * @param {boolean} [spec.hold=true]
     * @param {object} [spec.trajectory] reuse a path instead of recording one
     * @param {string[]} [spec.objects] scene subset shown to every method
     */
    createSession(spec) {
        const participant = String(spec.participant || '');
        if (!PARTICIPANT.test(participant)) {
            throw new Error('participant must be a pseudonymous code such as "p07" '
                + '(letters, digits, . _ -), never a name');
        }
        const methods = [...new Set(spec.methods || [])];
        if (!methods.length) throw new Error('choose at least one method');
        for (const method of methods) {
            if (!METHODS[method]) throw new Error(`unknown method "${method}"`);
            if (!METHODS[method].followsTrajectory) {
                throw new Error(`${METHODS[method].name} cannot follow a camera path, `
                    + 'so it cannot be compared under the same trajectory');
            }
        }
        const duration = Number(spec.durationSeconds);
        if (!(duration >= 1 && duration <= 600)) {
            throw new Error('durationSeconds must be between 1 and 600');
        }
        if (!spec.trace || typeof spec.trace.text !== 'string') {
            throw new Error('a session needs a network trace');
        }
        const raw = parseTrace(spec.trace.text, { scale: spec.trace.scale ?? 1 });
        const points = windowTrace(raw, {
            start: spec.trace.start ?? 0, end: spec.trace.end ?? Infinity
        });
        const index = Number.isInteger(spec.participantIndex)
            ? spec.participantIndex : participantIndex(participant);
        // Canonical order before counterbalancing, so the same participant
        // always gets the same row whatever order the methods were ticked in.
        const canonical = Object.keys(METHODS).filter(id => methods.includes(id));
        const { row, order } = trialOrder(canonical, index);
        const id = `${participant}-${timestamp()}`;
        const trajectory = spec.trajectory ? validateTrajectory(spec.trajectory) : null;

        const session = {
            schemaVersion: SCHEMA_VERSION,
            id,
            participant,
            participantIndex: index,
            createdAt: new Date().toISOString(),
            durationSeconds: duration,
            objects: Array.isArray(spec.objects) ? spec.objects.map(String) : [],
            trace: {
                name: String(spec.trace.name || 'uploaded.csv').slice(0, 120),
                sha256: sha256(spec.trace.text),
                window: { start: spec.trace.start ?? 0, end: spec.trace.end ?? null },
                scale: spec.trace.scale ?? 1,
                hold: spec.hold !== false,
                summary: summarize(points),
                points
            },
            counterbalancing: { design: 'williams', row, conditions: canonical },
            trajectory: trajectory
                ? { source: 'uploaded', sha256: sha256(JSON.stringify(trajectory)),
                    samples: trajectory.samples.length, durationSeconds: trajectory.durationSeconds }
                : null,
            trials: order.map(entry => ({ ...entry, status: 'pending' }))
        };
        this._write(id, 'session.json', session);
        fs.writeFileSync(path.join(this._dir(id), 'trace.csv'), spec.trace.text);
        if (trajectory) this._write(id, 'trajectory.json', trajectory);
        return session;
    }

    session(id) { return this._read(id, 'session.json'); }

    listSessions() {
        return fs.readdirSync(this.root, { withFileTypes: true })
            .filter(entry => entry.isDirectory()
                && fs.existsSync(path.join(this.root, entry.name, 'session.json')))
            .map(entry => {
                const session = this.session(entry.name);
                return {
                    id: session.id, participant: session.participant,
                    createdAt: session.createdAt,
                    done: session.trials.filter(trial => trial.status === 'done').length,
                    trials: session.trials.length
                };
            })
            .sort((a, b) => b.createdAt.localeCompare(a.createdAt));
    }

    saveTrajectory(id, trajectory) {
        const session = this.session(id);
        if (session.trials.some(trial => trial.status !== 'pending')) {
            throw Object.assign(new Error(
                'trials have already been run against this path; start a new session '
                + 'rather than changing the trajectory under recorded results'),
            { statusCode: 409 });
        }
        const clean = validateTrajectory(trajectory);
        this._write(id, 'trajectory.json', clean);
        session.trajectory = {
            source: clean.recordedWith ? 'recorded' : 'uploaded',
            sha256: sha256(JSON.stringify(clean)),
            samples: clean.samples.length,
            durationSeconds: clean.durationSeconds
        };
        this._write(id, 'session.json', session);
        return session;
    }

    trajectory(id) { return this._read(id, 'trajectory.json'); }

    _trial(session, position) {
        const trial = session.trials[Number(position)];
        if (!trial) throw Object.assign(new Error(`no trial ${position}`), { statusCode: 404 });
        return trial;
    }

    _trialFile(trial) {
        return `trials/${String(trial.position + 1).padStart(2, '0')}-${trial.label}/trial.json`;
    }

    startTrial(id, position) {
        const session = this.session(id);
        if (!session.trajectory) {
            throw Object.assign(new Error('record or upload the trajectory before the trials'),
                { statusCode: 409 });
        }
        const trial = this._trial(session, position);
        if (trial.status === 'done') {
            throw Object.assign(new Error(`trial ${trial.label} is already recorded`),
                { statusCode: 409 });
        }
        trial.status = 'running';
        trial.startedAt = new Date().toISOString();
        this._write(id, 'session.json', session);
        return { session, trial };
    }

    /**
     * Store what one trial measured and what the participant said about it.
     * The questionnaire is validated here as well as in the page: this is the
     * record, and a response the form could not have produced must not reach it.
     */
    finishTrial(id, position, { metrics, questionnaire: response, shaping }) {
        const session = this.session(id);
        const trial = this._trial(session, position);
        if (trial.status !== 'running') {
            throw Object.assign(new Error(`trial ${trial.label} is not running`),
                { statusCode: 409 });
        }
        const record = {
            schemaVersion: SCHEMA_VERSION,
            session: session.id,
            participant: session.participant,
            position: trial.position,
            label: trial.label,
            method: trial.method,
            startedAt: trial.startedAt,
            finishedAt: new Date().toISOString(),
            trace: { name: session.trace.name, sha256: session.trace.sha256 },
            trajectory: session.trajectory && { sha256: session.trajectory.sha256 },
            shaping: shaping ?? null,
            metrics: metrics && typeof metrics === 'object' ? metrics : {},
            questionnaire: questionnaire.validateResponse(response)
        };
        this._write(id, this._trialFile(trial), record);
        trial.status = 'done';
        trial.finishedAt = record.finishedAt;
        this._write(id, 'session.json', session);
        return { session, record };
    }

    trialRecords(id) {
        const session = this.session(id);
        return session.trials
            .filter(trial => trial.status === 'done')
            .map(trial => this._read(id, this._trialFile(trial)));
    }

    /** One row per finished trial, in the order they were run. */
    exportCsv(id) {
        const session = this.session(id);
        const records = this.trialRecords(id);
        const metricKeys = [...new Set(records.flatMap(record => Object.keys(record.metrics)))]
            .filter(key => {
                const value = records.find(record => key in record.metrics)?.metrics[key];
                return value === null || ['number', 'string', 'boolean'].includes(typeof value);
            })
            .sort();
        const columns = [
            'session', 'participant', 'position', 'label', 'method',
            'trace', 'trace_sha256', 'trajectory_sha256',
            ...questionnaire.RATINGS.map(rating => rating.id), 'artifacts',
            'questionnaire_seconds',
            ...metricKeys.map(key => `m_${key}`)
        ];
        const cell = value => {
            if (value === null || value === undefined) return '';
            const text = String(value);
            return /[",\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
        };
        const rows = records.map(record => [
            record.session, record.participant, record.position + 1, record.label, record.method,
            session.trace.name, record.trace.sha256, record.trajectory?.sha256,
            ...questionnaire.RATINGS.map(rating => record.questionnaire.ratings[rating.id]),
            record.questionnaire.artifacts.join(';'),
            record.questionnaire.timing.totalSeconds,
            ...metricKeys.map(key => record.metrics[key])
        ].map(cell).join(','));
        return `${columns.join(',')}\n${rows.join('\n')}${rows.length ? '\n' : ''}`;
    }
}

module.exports = { StudyStore, METHODS, validateTrajectory, TRAJECTORY_SPACE, sha256 };
