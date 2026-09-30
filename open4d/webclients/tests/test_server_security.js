'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const { app } = require('../system/Server/server');

const results = path.resolve(__dirname, '../system/Server/server_results');

test('client log cannot write outside the results root', async t => {
    const outside = fs.mkdtempSync(path.join(os.tmpdir(), 'open4d-log-security-'));
    t.after(() => fs.rmSync(outside, { recursive: true, force: true }));
    const server = await new Promise(resolve => {
        const s = app.listen(0, '127.0.0.1', () => resolve(s));
    });
    t.after(() => new Promise(resolve => server.close(resolve)));
    const response = await fetch(`http://127.0.0.1:${server.address().port}/api/client-log`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ broadcastId: path.relative(results, outside),
                               entries: [{ message: 'fixture' }] })
    });
    assert.equal(response.status, 400);
    assert.equal(fs.existsSync(path.join(outside, 'client_log.jsonl')), false);
});

test('foreign websites cannot invoke the local control API', async t => {
    const server = await new Promise(resolve => {
        const s = app.listen(0, '127.0.0.1', () => resolve(s));
    });
    t.after(() => new Promise(resolve => server.close(resolve)));
    const response = await fetch(`http://127.0.0.1:${server.address().port}/api/health`, {
        headers: { Origin: 'https://untrusted.example' }
    });
    assert.equal(response.status, 403);
    assert.equal(response.headers.get('access-control-allow-origin'), null);
});

test('a forged hostname cannot bypass the browser origin check', async t => {
    const server = await new Promise(resolve => {
        const s = app.listen(0, '127.0.0.1', () => resolve(s));
    });
    t.after(() => new Promise(resolve => server.close(resolve)));
    const status = await new Promise((resolve, reject) => {
        const request = http.get({ host: '127.0.0.1', port: server.address().port,
            path: '/api/health', headers: { Host: 'untrusted.example', Origin: 'http://untrusted.example' }
        }, response => { response.resume(); resolve(response.statusCode); });
        request.on('error', reject);
    });
    assert.equal(status, 403);
});

test('the same-origin UI can still read the API', async t => {
    const server = await new Promise(resolve => {
        const s = app.listen(0, '127.0.0.1', () => resolve(s));
    });
    t.after(() => new Promise(resolve => server.close(resolve)));
    const base = `http://127.0.0.1:${server.address().port}`;
    const response = await fetch(`${base}/api/health`, { headers: { Origin: base } });
    assert.equal(response.status, 200);
    assert.equal(response.headers.get('access-control-allow-origin'), base);
});
