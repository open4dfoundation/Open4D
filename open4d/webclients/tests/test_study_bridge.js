'use strict';

const assert = require('node:assert');
const net = require('node:net');
const test = require('node:test');

const { createBridge } = require('../system/WebClient/bridge/v4ds-bridge');
const { TraceShaper } = require('../system/Server/study/shaper');
const { ShapingFollower } = require('../system/Server/study/follower');
const { parseTrace } = require('../system/Server/study/trace');

const MESSAGE_BYTES = 64 * 1024;
const MESSAGES = 32;                     // 2 MiB in all

/** A stand-in baseline: accepts one connection and pushes framed messages flat out. */
function fakeBaseline() {
    return new Promise(resolve => {
        const server = net.createServer(socket => {
            for (let index = 0; index < MESSAGES; index++) {
                const payload = Buffer.alloc(MESSAGE_BYTES, index);
                const prefix = Buffer.alloc(4);
                prefix.writeUInt32BE(payload.length);
                socket.write(Buffer.concat([prefix, payload]));
            }
        });
        server.listen(0, '127.0.0.1', () => resolve(server));
    });
}

async function receive(bridgePort) {
    // Node's own WebSocket, as test_v4ds_bridge.js uses: no npm dependency.
    const socket = new WebSocket(`ws://127.0.0.1:${bridgePort}`);
    socket.binaryType = 'arraybuffer';
    const got = [];
    const started = performance.now();
    await new Promise((resolve, reject) => {
        socket.onmessage = event => {
            got.push(Buffer.from(event.data));
            if (got.length === MESSAGES) resolve();
        };
        socket.onerror = reject;
    });
    const seconds = (performance.now() - started) / 1000;
    socket.close();
    return { got, seconds };
}

async function withBridge(shaper, body) {
    const baseline = await fakeBaseline();
    const bridge = createBridge({
        listenHost: '127.0.0.1', listenPort: 0,
        baselineHost: '127.0.0.1', baselinePort: baseline.address().port,
        maxMessageBytes: 16 * 1024 * 1024, verbose: false, shaper
    });
    await new Promise(resolve => bridge.on('listening', resolve));
    try {
        return await body(bridge.address().port);
    } finally {
        bridge.close();
        baseline.close();
    }
}

test('a shaped bridge delivers at the trace rate, intact and in order', async () => {
    const shaper = new TraceShaper();
    shaper.arm(parseTrace('0,16\n'));                 // 2 MB/s
    const { got, seconds } = await withBridge(shaper, receive);
    got.forEach((message, index) => {
        assert.strictEqual(message.length, MESSAGE_BYTES);
        assert.strictEqual(message[0], index, `message ${index} arrived out of order`);
    });
    // (2 MiB - 128 KiB burst) at 2 MB/s is 0.98 s.
    assert.ok(seconds > 0.85 && seconds < 1.6, `took ${seconds.toFixed(3)} s`);
    shaper.disarm();
});

test('an unarmed shaper leaves the bridge a straight proxy', async () => {
    const shaper = new TraceShaper();
    const { got, seconds } = await withBridge(shaper, receive);
    assert.strictEqual(got.length, MESSAGES);
    assert.ok(seconds < 0.5, `took ${seconds.toFixed(3)} s unshaped`);
});

test('the follower arms from the server, at the server\'s instant', async () => {
    const startedAt = Date.now() / 1000 - 3;          // armed three seconds ago
    const shaping = { startedAt, points: parseTrace('0,40\n2,8\n'), hold: true,
                      label: 'p01 A', rateMbps: 8 };
    const follower = new ShapingFollower({
        serverUrl: 'http://study',
        fetchImpl: async () => ({ ok: true, json: async () => shaping })
    });
    await follower.poll();
    assert.strictEqual(follower.shaper.armed, true);
    // Three seconds into the trace is past the 2 s step: the follower is on the
    // server's clock, not on when it happened to poll.
    assert.strictEqual(follower.shaper.rateMbps(), 8);

    shaping.startedAt = null;
    shaping.points = null;
    await follower.poll();
    assert.strictEqual(follower.shaper.armed, false, 'trial over, link unshaped');
});

test('losing the server unshapes after a few misses, rather than freezing the link', async () => {
    let up = true;
    const follower = new ShapingFollower({
        serverUrl: 'http://study',
        fetchImpl: async () => {
            if (!up) throw new Error('ECONNREFUSED');
            return { ok: true, json: async () => ({
                startedAt: Date.now() / 1000, points: parseTrace('0,5\n'), hold: true }) };
        }
    });
    await follower.poll();
    assert.strictEqual(follower.shaper.armed, true);
    up = false;
    for (let miss = 0; miss < 3; miss++) await follower.poll().catch(() => {});
    assert.strictEqual(follower.shaper.armed, false);
});
