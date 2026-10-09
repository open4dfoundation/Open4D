'use strict';

const assert = require('node:assert');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');

// Installed by `npm install` in system/Server; the HTTP tests skip without it.
let express = null;
try { express = require('../system/Server/node_modules/express'); } catch { /* skipped below */ }
const NO_EXPRESS = express ? false : 'express is not installed in system/Server';

const { williamsRows, participantIndex, positionLabel, trialOrder } =
    require('../system/Server/study/order');
const questionnaire = require('../system/Server/study/questionnaire');
const { StudyStore, validateTrajectory } = require('../system/Server/study/store');
const { TraceShaper } = require('../system/Server/study/shaper');
const { createStudyRouter, shapedStatic } = require('../system/Server/study/routes');

const TRACE = 'time,bandwidth\n0,40\n5,8\n10,40\n';

function trajectory(seconds = 4) {
    const samples = [];
    for (let t = 0; t <= seconds; t += 0.5) {
        samples.push({ t, position: [Math.sin(t), 1.6, 4 * Math.cos(t)],
                       target: [0, 1, 0], up: [0, 1, 0], fovDegrees: 60 });
    }
    return { schemaVersion: 1, coordinateSpace: 'three-world-metres-y-up',
             recordedWith: 'mesh', samples };
}

const RESPONSE = { ratings: { C1: 4, C2: 3, C3: 5, C4: 4 }, artifacts: ['freezing'] };

function tempDir() { return fs.mkdtempSync(path.join(os.tmpdir(), 'vs4d-study-')); }

// --------------------------------------------------------------- order ---

test('a Williams design balances position and first-order carryover', () => {
    for (const n of [2, 3, 4, 5]) {
        const rows = williamsRows(n);
        assert.strictEqual(rows.length, n % 2 ? 2 * n : n);
        // Every condition once per position.
        for (let position = 0; position < n; position++) {
            const column = rows.map(row => row[position]).sort();
            const expected = [];
            for (let k = 0; k < rows.length / n; k++) for (let c = 0; c < n; c++) expected.push(c);
            assert.deepStrictEqual(column, expected.sort());
        }
        // Every ordered pair (a immediately before b) equally often.
        const pairs = new Map();
        for (const row of rows) for (let i = 1; i < n; i++) {
            const key = `${row[i - 1]}>${row[i]}`;
            pairs.set(key, (pairs.get(key) || 0) + 1);
        }
        assert.strictEqual(new Set(pairs.values()).size, 1, `n=${n}: carryover unbalanced`);
        assert.strictEqual(pairs.size, n * (n - 1));
    }
});

test('a participant code reads as its number, so p07 and p7 agree', () => {
    assert.strictEqual(participantIndex('p07'), 7);
    assert.strictEqual(participantIndex('p7'), 7);
    assert.strictEqual(participantIndex('pilot'), 0);
});

test('position labels run A..Z then AA', () => {
    assert.deepStrictEqual([0, 1, 25, 26].map(positionLabel), ['A', 'B', 'Z', 'AA']);
});

test('the order shows the participant labels, never method names', () => {
    const { order } = trialOrder(['mesh', 'vivo', 'nava'], 1);
    assert.deepStrictEqual(order.map(entry => entry.label), ['A', 'B', 'C']);
    assert.deepStrictEqual([...order.map(entry => entry.method)].sort(), ['mesh', 'nava', 'vivo']);
});

// ------------------------------------------------------- questionnaire ---

test('a complete response validates', () => {
    const clean = questionnaire.validateResponse(RESPONSE);
    assert.deepStrictEqual(clean.ratings, RESPONSE.ratings);
    assert.deepStrictEqual(clean.artifacts, ['freezing']);
});

test('a missing or out-of-range rating is refused', () => {
    assert.throws(() => questionnaire.validateResponse(
        { ...RESPONSE, ratings: { C1: 4, C2: 3, C3: 5 } }), /C4/);
    assert.throws(() => questionnaire.validateResponse(
        { ...RESPONSE, ratings: { ...RESPONSE.ratings, C1: 6 } }), /C1/);
    assert.throws(() => questionnaire.validateResponse(
        { ...RESPONSE, ratings: { ...RESPONSE.ratings, C2: 2.5 } }), /C2/);
});

test('artifacts: at most two, "none" is exclusive, one is required', () => {
    const artifacts = list => questionnaire.validateResponse({ ...RESPONSE, artifacts: list });
    assert.throws(() => artifacts(['freezing', 'missing_parts', 'quality_flicker']), /at most 2/);
    assert.throws(() => artifacts(['none', 'freezing']), /cannot be combined/);
    assert.throws(() => artifacts([]), /at least one/);
    assert.throws(() => artifacts(['sparkles']), /unknown artifact/);
    assert.deepStrictEqual(artifacts(['none']).artifacts, ['none']);
});

const QUEST_PANEL = path.join(__dirname,
    '../system/QuestClient/Assets/QuestClient/Scripts/QuestQuestionnairePanel.cs');

// Where the Quest client is not checked out (the Open4D vendored copy), there
// is nothing to compare against, and the wording is guarded upstream instead.
test('the wording is the Quest panel\'s',
    { skip: !fs.existsSync(QUEST_PANEL) && 'QuestClient is not in this checkout' }, () => {
    const quest = fs.readFileSync(QUEST_PANEL, 'utf8');
    for (const rating of questionnaire.RATINGS) {
        const prompt = rating.prompt.replace('—', '—');
        assert.ok(quest.includes(prompt), `${rating.id} prompt drifted from the Quest panel`);
    }
    for (const artifact of questionnaire.ARTIFACTS) {
        assert.ok(quest.includes(`("${artifact.id}", "${artifact.label}")`),
            `artifact ${artifact.id} drifted from the Quest panel`);
    }
});

// --------------------------------------------------------------- store ---

test('a session records the trace verbatim, and its hash', () => {
    const store = new StudyStore(tempDir());
    const session = store.createSession({
        participant: 'p02', methods: ['mesh', 'vivo'], durationSeconds: 20,
        trace: { name: 'cascade.csv', text: TRACE }
    });
    assert.strictEqual(session.trials.length, 2);
    assert.strictEqual(session.trace.summary.minMbps, 8);
    const kept = fs.readFileSync(path.join(store.root, session.id, 'trace.csv'), 'utf8');
    assert.strictEqual(kept, TRACE);
});

test('a name is refused as a participant code', () => {
    const store = new StudyStore(tempDir());
    assert.throws(() => store.createSession({
        participant: 'Ryan Kim', methods: ['mesh'], durationSeconds: 20,
        trace: { text: TRACE }
    }), /pseudonymous/);
});

test('NeVo is refused: it cannot follow the shared camera path', () => {
    const store = new StudyStore(tempDir());
    assert.throws(() => store.createSession({
        participant: 'p01', methods: ['mesh', 'nevo'], durationSeconds: 20,
        trace: { text: TRACE }
    }), /NeVo cannot follow a camera path/);
});

test('the same participant gets the same order whatever the tick order', () => {
    const store = new StudyStore(tempDir());
    const a = store.createSession({ participant: 'p03', methods: ['nava', 'mesh', 'vivo'],
                                    durationSeconds: 20, trace: { text: TRACE } });
    const b = store.createSession({ participant: 'p03', methods: ['vivo', 'nava', 'mesh'],
                                    durationSeconds: 20, trace: { text: TRACE } });
    assert.deepStrictEqual(a.trials.map(t => t.method), b.trials.map(t => t.method));
});

test('trials cannot start before the path exists, nor the path change after', () => {
    const store = new StudyStore(tempDir());
    const session = store.createSession({ participant: 'p04', methods: ['mesh'],
                                          durationSeconds: 20, trace: { text: TRACE } });
    assert.throws(() => store.startTrial(session.id, 0), /trajectory before the trials/);
    store.saveTrajectory(session.id, trajectory());
    store.startTrial(session.id, 0);
    assert.throws(() => store.saveTrajectory(session.id, trajectory()), /already been run/);
});

test('a trajectory is refused if time runs backwards', () => {
    const bad = trajectory();
    bad.samples[2].t = 0.1;
    assert.throws(() => validateTrajectory(bad), /backwards/);
});

// -------------------------------------------------------------- routes ---

async function serve({ bridgeShaping } = {}) {
    const root = tempDir();
    const files = tempDir();
    // 2 MB at the trace's opening 40 Mbps is about 0.4 s; unshaped it is instant.
    fs.writeFileSync(path.join(files, 'segment.bin'), Buffer.alloc(2 * 1024 * 1024, 3));
    const shaper = new TraceShaper();
    const app = express();
    app.use(express.json({ limit: '200mb' }));
    app.use('/files', shapedStatic(files, shaper));
    app.use('/files', express.static(files));
    app.use('/api/study', createStudyRouter({ resultsRoot: root, shaper, bridgeShaping }));
    const server = await new Promise(resolve => {
        const s = app.listen(0, '127.0.0.1', () => resolve(s));
    });
    const base = `http://127.0.0.1:${server.address().port}`;
    const call = async (method, url, body) => {
        const response = await fetch(base + url, {
            method, headers: { 'Content-Type': 'application/json' },
            body: body === undefined ? undefined : JSON.stringify(body)
        });
        const text = await response.text();
        let json = null;
        try { json = JSON.parse(text); } catch { /* CSV */ }
        return { status: response.status, json, text, headers: response.headers };
    };
    return { server, base, call, shaper, root };
}

test('a whole session over HTTP: create, record, run, rate, export', { skip: NO_EXPRESS }, async () => {
    const { server, base, call, shaper } = await serve();
    try {
        const created = await call('POST', '/api/study/sessions', {
            participant: 'p05', methods: ['mesh', 'vivo'], durationSeconds: 20,
            trace: { name: 'cascade.csv', text: TRACE }
        });
        assert.strictEqual(created.status, 200, created.text);
        const session = created.json;

        const early = await call('POST', `/api/study/sessions/${session.id}/trials/0/start`);
        assert.strictEqual(early.status, 409, 'no trial without a trajectory');

        const saved = await call('PUT', `/api/study/sessions/${session.id}/trajectory`, trajectory());
        assert.strictEqual(saved.status, 200, saved.text);
        assert.strictEqual(saved.json.trajectory.source, 'recorded');

        // Unshaped between trials: the download is effectively instant.
        let start = performance.now();
        let body = await fetch(`${base}/files/segment.bin`).then(r => r.arrayBuffer());
        const unshaped = (performance.now() - start) / 1000;
        assert.strictEqual(body.byteLength, 2 * 1024 * 1024);

        const started = await call('POST', `/api/study/sessions/${session.id}/trials/0/start`);
        assert.strictEqual(started.status, 200, started.text);
        assert.strictEqual(started.json.shaping.rateMbps, 40);

        const other = await call('POST', `/api/study/sessions/${session.id}/trials/1/start`);
        assert.strictEqual(other.status, 409, 'a second trial may not take the live link');

        start = performance.now();
        const response = await fetch(`${base}/files/segment.bin`);
        assert.strictEqual(response.headers.get('x-study-shaped'), '1');
        body = await response.arrayBuffer();
        const shaped = (performance.now() - start) / 1000;
        assert.strictEqual(body.byteLength, 2 * 1024 * 1024);
        // (2 MiB - 128 KiB burst) at 5 MB/s is 0.393 s.
        assert.ok(shaped > 0.3 && shaped < 1.0, `shaped download took ${shaped.toFixed(3)} s`);
        assert.ok(shaped > 5 * unshaped, 'shaping visibly slowed the transfer');

        const status = await call('GET', '/api/study/shaping');
        assert.strictEqual(status.json.shaped, true);
        assert.strictEqual(status.json.live.label, session.trials[0].label);

        const bad = await call('POST', `/api/study/sessions/${session.id}/trials/0/finish`,
            { metrics: {}, questionnaire: { ...RESPONSE, artifacts: ['none', 'freezing'] } });
        assert.strictEqual(bad.status, 400, 'an impossible questionnaire is not recorded');
        assert.strictEqual(shaper.armed, true, 'and the trial stays live to be retried');

        const finished = await call('POST', `/api/study/sessions/${session.id}/trials/0/finish`,
            { metrics: { startupDelayMs: 812, stalledSeconds: 1.5 }, questionnaire: RESPONSE });
        assert.strictEqual(finished.status, 200, finished.text);
        assert.strictEqual(shaper.armed, false, 'finishing a trial frees the link');
        assert.strictEqual(finished.json.record.shaping.shaped, true);

        const csv = await call('GET', `/api/study/sessions/${session.id}/export.csv`);
        const [header, row] = csv.text.trim().split('\n');
        assert.ok(header.startsWith('session,participant,position,label,method'));
        assert.ok(header.includes('C1,C2,C3,C4,artifacts'));
        assert.ok(header.includes('m_startupDelayMs'));
        assert.ok(row.includes(',4,3,5,4,freezing,'));
        const cells = row.split(',');
        const columns = header.split(',');
        assert.strictEqual(cells[columns.indexOf('method')], session.trials[0].method);
        assert.strictEqual(cells[columns.indexOf('label')], 'A');
    } finally {
        shaper.disarm();
        server.close();
    }
});

test('abort frees the link without recording a result', { skip: NO_EXPRESS }, async () => {
    const { server, call, shaper } = await serve();
    try {
        const session = (await call('POST', '/api/study/sessions', {
            participant: 'p06', methods: ['mesh'], durationSeconds: 20, trace: { text: TRACE }
        })).json;
        await call('PUT', `/api/study/sessions/${session.id}/trajectory`, trajectory());
        await call('POST', `/api/study/sessions/${session.id}/trials/0/start`);
        assert.strictEqual(shaper.armed, true);
        await call('POST', '/api/study/abort');
        assert.strictEqual(shaper.armed, false);
        const csv = await call('GET', `/api/study/sessions/${session.id}/export.csv`);
        assert.strictEqual(csv.text.trim().split('\n').length, 1, 'header only');
    } finally {
        server.close();
    }
});


test('session ids cannot name the root or its parent', () => {
    const store = new StudyStore(tempDir());
    for (const id of ['.', '..', '../outside', '/outside', ['p01']]) {
        assert.throws(() => store._dir(id), /invalid session id/);
    }
});

test('two sessions created in the same second never overwrite each other', () => {
    const store = new StudyStore(tempDir());
    const spec = { participant: 'p03', methods: ['mesh'], durationSeconds: 20,
                   trace: { text: TRACE } };
    const sessions = Array.from({ length: 10 }, () => store.createSession(spec));
    assert.strictEqual(new Set(sessions.map(s => s.id)).size, sessions.length);
    assert.strictEqual(store.listSessions().length, sessions.length);
});

test('finishing an abandoned trial cannot disarm another session', { skip: NO_EXPRESS }, async () => {
    const { server, call, shaper } = await serve();
    try {
        const make = async participant => (await call('POST', '/api/study/sessions', {
            participant, methods: ['mesh'], durationSeconds: 20,
            trace: { text: TRACE }, trajectory: trajectory()
        })).json;
        const first = await make('p10');
        const second = await make('p11');
        await call('POST', `/api/study/sessions/${first.id}/trials/0/start`);
        await call('POST', '/api/study/abort');
        await call('POST', `/api/study/sessions/${second.id}/trials/0/start`);
        const response = await call('POST', `/api/study/sessions/${first.id}/trials/0/finish`,
            { metrics: {}, questionnaire: RESPONSE });
        assert.strictEqual(response.status, 409);
        assert.strictEqual(shaper.armed, true);
        assert.strictEqual((await call('GET', '/api/study/shaping')).json.live.session, second.id);
    } finally { shaper.disarm(); server.close(); }
});

test('CSV exports keep uploaded formula-like strings as text', () => {
    const store = new StudyStore(tempDir());
    const session = store.createSession({ participant: 'p12', methods: ['mesh'],
        durationSeconds: 20, trace: { name: '=1+1', text: TRACE }, trajectory: trajectory() });
    store.startTrial(session.id, 0);
    store.finishTrial(session.id, 0, { metrics: { note: '@SUM(1)', loss: -1, 'bad,key': 'safe' },
                                     questionnaire: RESPONSE });
    const csv = store.exportCsv(session.id);
    assert.ok(csv.includes("'=1+1"));
    assert.ok(csv.includes("'@SUM(1)"));
    assert.ok(csv.includes(',-1,'));
    assert.ok(csv.includes('"m_bad,key"'));
});

test('a point-cloud trial records what the bridge shaped, not the server', { skip: NO_EXPRESS }, async () => {
    const net = require('node:net');
    const { createBridge } = require('../system/WebClient/bridge/v4ds-bridge');
    const MESSAGE_BYTES = 64 * 1024;
    const MESSAGES = 16;
    const baseline = await new Promise(resolve => {
        const server = net.createServer(socket => {
            for (let index = 0; index < MESSAGES; index++) {
                const prefix = Buffer.alloc(4);
                prefix.writeUInt32BE(MESSAGE_BYTES);
                socket.write(Buffer.concat([prefix, Buffer.alloc(MESSAGE_BYTES, index)]));
            }
        });
        server.listen(0, '127.0.0.1', () => resolve(server));
    });
    let bridgePort = null;
    const bridgeStatus = () => fetch(`http://127.0.0.1:${bridgePort}/shaping`).then(r => r.json());
    const { server, base, call, shaper } = await serve({
        bridgeShaping: async method => (method === 'vivo' ? bridgeStatus() : null)
    });
    const bridge = createBridge({
        listenHost: '127.0.0.1', listenPort: 0, baselineHost: '127.0.0.1',
        baselinePort: baseline.address().port, maxMessageBytes: 1 << 24,
        verbose: false, studyServer: base
    });
    await new Promise(resolve => bridge.on('listening', resolve));
    bridgePort = bridge.address().port;
    try {
        const session = (await call('POST', '/api/study/sessions', {
            participant: 'p07', methods: ['vivo'], durationSeconds: 20, trace: { text: TRACE }
        })).json;
        await call('PUT', `/api/study/sessions/${session.id}/trajectory`, trajectory());
        await call('POST', `/api/study/sessions/${session.id}/trials/0/start`);

        // The follower polls every 500 ms; wait for it to pick the trial up.
        for (let tries = 0; tries < 40 && !(await bridgeStatus()).shaped; tries++) {
            await new Promise(resolve => setTimeout(resolve, 50));
        }
        const socket = new WebSocket(`ws://127.0.0.1:${bridgePort}`);
        await new Promise((resolve, reject) => {
            let got = 0;
            socket.onmessage = () => { if (++got === MESSAGES) resolve(); };
            socket.onerror = reject;
        });
        socket.close();

        const finished = await call('POST', `/api/study/sessions/${session.id}/trials/0/finish`,
            { metrics: {}, questionnaire: RESPONSE });
        assert.strictEqual(finished.status, 200, finished.text);
        const shaping = finished.json.record.shaping;
        assert.strictEqual(shaping.deliveredBytes, 0, 'none of it crossed the server');
        assert.strictEqual(shaping.bridge.following, true);
        assert.strictEqual(shaping.bridge.matchesTrial, true);
        assert.strictEqual(shaping.bridge.deliveredBytes, MESSAGES * MESSAGE_BYTES);

        const csv = await call('GET', `/api/study/sessions/${session.id}/export.csv`);
        const [header, row] = csv.text.trim().split('\n');
        const columns = header.split(',');
        const cells = row.split(',');
        assert.strictEqual(cells[columns.indexOf('shaped_by')], 'bridge');
        assert.strictEqual(Number(cells[columns.indexOf('shaped_bytes')]), MESSAGES * MESSAGE_BYTES);
    } finally {
        shaper.disarm();
        await new Promise(resolve => bridge.close(resolve));
        baseline.close();
        server.close();
    }
});
