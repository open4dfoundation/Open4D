'use strict';

/**
 * Tests for the WebSocket <-> TCP bridge.
 *
 * The framer gets the most attention because TCP chunk boundaries are where a
 * proxy like this fails: everything works on localhost, where a whole keyframe
 * usually arrives in one chunk, and then breaks on a real link where a
 * multi-megabyte point cloud is split across dozens of segments — or where a
 * length prefix itself straddles two chunks. Those cases are cheap to test and
 * expensive to debug.
 *
 * The end-to-end tests run a fake baseline server (a plain TCP listener
 * speaking real V4DS bytes from the Python-generated golden fixtures) so the
 * proxy path is exercised for real, not mocked.
 *
 * Run: node --test tests/test_v4ds_bridge.js
 */

const assert = require('assert');
const test = require('node:test');
const net = require('net');
const fs = require('fs');
const path = require('path');

const {
    MessageFramer, frame, createBridge, parseArgs, looksLikeV4ds
} = require('../system/WebClient/bridge/v4ds-bridge');
const WebSocket = require(
    path.join(__dirname, '..', 'system/WebClient/node_modules/ws'));

const GOLDEN = JSON.parse(fs.readFileSync(
    path.join(__dirname, 'fixtures_v4ds_golden.json'), 'utf8'));
const connectionBytes = Buffer.from(GOLDEN.connection, 'hex');
const frameBytes = Buffer.from(GOLDEN.frame_delta, 'hex');

// --------------------------------------------------------------------------
// Framing
// --------------------------------------------------------------------------

test('frame() writes a big-endian length prefix', () => {
    const framed = frame(Buffer.alloc(300));
    assert.strictEqual(framed.length, 304);
    assert.strictEqual(framed.readUInt32BE(0), 300);
    // Big-endian: 300 = 0x0000012C. Little-endian would put 0x2C first, which
    // the Python server would read as a 738197504-byte message.
    assert.deepStrictEqual(Array.from(framed.subarray(0, 4)), [0, 0, 1, 0x2c]);
});

test('the framer returns one whole message from one whole chunk', () => {
    const framer = new MessageFramer();
    const messages = framer.push(frame(connectionBytes));
    assert.strictEqual(messages.length, 1);
    assert.ok(messages[0].equals(connectionBytes));
    assert.strictEqual(framer.pendingBytes, 0);
});

test('the framer reassembles a message split one byte at a time', () => {
    // The pathological case, and the one that proves the prefix itself can
    // straddle chunk boundaries.
    const framer = new MessageFramer();
    const wire = frame(frameBytes);
    const collected = [];
    for (const byte of wire) {
        collected.push(...framer.push(Buffer.from([byte])));
    }
    assert.strictEqual(collected.length, 1);
    assert.ok(collected[0].equals(frameBytes));
    assert.strictEqual(framer.pendingBytes, 0);
});

test('the framer splits several messages arriving in one chunk', () => {
    const framer = new MessageFramer();
    const wire = Buffer.concat([
        frame(connectionBytes), frame(frameBytes), frame(frameBytes)]);
    const messages = framer.push(wire);
    assert.strictEqual(messages.length, 3);
    assert.ok(messages[0].equals(connectionBytes));
    assert.ok(messages[1].equals(frameBytes));
    assert.ok(messages[2].equals(frameBytes));
});

test('the framer holds a partial message and completes it later', () => {
    const framer = new MessageFramer();
    const wire = frame(frameBytes);
    const head = wire.subarray(0, 20);
    const tail = wire.subarray(20);

    assert.deepStrictEqual(framer.push(head), []);
    assert.ok(framer.pendingBytes > 0, 'the partial message is held');
    const messages = framer.push(tail);
    assert.strictEqual(messages.length, 1);
    assert.ok(messages[0].equals(frameBytes));
});

test('the framer handles a chunk that ends mid-prefix', () => {
    const framer = new MessageFramer();
    const wire = frame(connectionBytes);
    assert.deepStrictEqual(framer.push(wire.subarray(0, 2)), [],
        'two bytes is not even a full length prefix');
    const messages = framer.push(wire.subarray(2));
    assert.strictEqual(messages.length, 1);
    assert.ok(messages[0].equals(connectionBytes));
});

test('the framer preserves message order across ragged chunks', () => {
    const framer = new MessageFramer();
    const payloads = Array.from({ length: 12 },
        (_, i) => Buffer.concat([connectionBytes.subarray(0, 8),
                                 Buffer.from([i])]));
    const wire = Buffer.concat(payloads.map(frame));
    const received = [];
    // Chunk sizes chosen to land awkwardly relative to message boundaries.
    for (let offset = 0; offset < wire.length; offset += 7) {
        received.push(...framer.push(wire.subarray(offset, offset + 7)));
    }
    assert.strictEqual(received.length, payloads.length);
    assert.deepStrictEqual(received.map(m => m[8]),
        payloads.map((_, i) => i), 'order preserved');
});

test('the framer refuses an absurd length instead of allocating', () => {
    // A framing desync, or a hostile peer, would otherwise have this process
    // try to buffer gigabytes.
    const framer = new MessageFramer({ maxMessageBytes: 1024 });
    const bogus = Buffer.alloc(8);
    bogus.writeUInt32BE(1 << 30, 0);
    assert.throws(() => framer.push(bogus), /exceeds the 1024 limit/);
});

test('a zero-length message is passed through, not treated as an error', () => {
    const framer = new MessageFramer();
    const messages = framer.push(frame(Buffer.alloc(0)));
    assert.strictEqual(messages.length, 1);
    assert.strictEqual(messages[0].length, 0);
});

// --------------------------------------------------------------------------
// Argument handling and validation
// --------------------------------------------------------------------------

test('parseArgs reads the options and rejects unknown ones', () => {
    const options = parseArgs([
        '--baseline-port', '12345', '--listen-port', '9001',
        '--baseline-host', '10.0.0.5', '--verbose']);
    assert.strictEqual(options.baselinePort, 12345);
    assert.strictEqual(options.listenPort, 9001);
    assert.strictEqual(options.baselineHost, '10.0.0.5');
    assert.strictEqual(options.verbose, true);
    assert.throws(() => parseArgs(['--nope']), /unknown argument/);
    assert.throws(() => parseArgs(['--baseline-port']), /needs a value/);
});

test('looksLikeV4ds accepts real messages and rejects junk', () => {
    assert.ok(looksLikeV4ds(connectionBytes));
    assert.ok(looksLikeV4ds(frameBytes));
    assert.ok(!looksLikeV4ds(Buffer.from('HELLO___')));
    assert.ok(!looksLikeV4ds(Buffer.from('V4DS')), 'magic alone is too short');
});

// --------------------------------------------------------------------------
// End to end, through a real socket and a real WebSocket
// --------------------------------------------------------------------------

/** A stand-in baseline server: sends canned V4DS messages, records feedback. */
function fakeBaseline({ send = [], onMessage = () => {} } = {}) {
    const received = [];
    const framer = new MessageFramer();
    const server = net.createServer(socket => {
        socket.setNoDelay(true);
        server.emit('client', socket);
        socket.on('data', chunk => {
            for (const message of framer.push(chunk)) {
                received.push(message);
                onMessage(message, socket);
            }
        });
        for (const payload of send) socket.write(frame(payload));
    });
    server.received = received;
    return server;
}

const listen = (server, port = 0) => new Promise(resolve =>
    server.listen(port, '127.0.0.1', () => resolve(server.address().port)));

const closeAll = async (...things) => {
    for (const thing of things) {
        if (!thing) continue;
        await new Promise(resolve => {
            try {
                if (typeof thing.close === 'function') thing.close(() => resolve());
                else resolve();
            } catch (_) { resolve(); }
            setTimeout(resolve, 300);
        });
    }
};

function openSocket(url) {
    return new Promise((resolve, reject) => {
        const socket = new WebSocket(url);
        socket.binaryType = 'nodebuffer';
        socket.once('open', () => resolve(socket));
        socket.once('error', reject);
    });
}

const freePort = () => 19000 + Math.floor(Math.random() * 4000);

test('a baseline message reaches the browser as one WebSocket frame', async () => {
    const baseline = fakeBaseline({ send: [connectionBytes, frameBytes] });
    const baselinePort = await listen(baseline);
    const listenPort = freePort();
    const bridge = createBridge({
        listenHost: '127.0.0.1', listenPort,
        baselineHost: '127.0.0.1', baselinePort,
        maxMessageBytes: 1 << 20
    });

    const socket = await openSocket(`ws://127.0.0.1:${listenPort}`);
    const messages = [];
    socket.on('message', data => messages.push(data));
    await new Promise(resolve => setTimeout(resolve, 400));

    assert.strictEqual(messages.length, 2, 'two messages, not a merged stream');
    assert.ok(messages[0].equals(connectionBytes), 'CONNECTION byte-exact');
    assert.ok(messages[1].equals(frameBytes), 'FRAME byte-exact');
    assert.ok(!messages[0].subarray(0, 4).equals(Buffer.from([0, 0, 0, 0])),
        'the length prefix was stripped');

    socket.close();
    await closeAll(bridge, baseline);
});

test('a message split across TCP writes still arrives whole', async () => {
    // This is the case the framer exists for; proving it through a real socket
    // guards the wiring as well as the logic.
    const baseline = net.createServer(socket => {
        const wire = frame(frameBytes);
        socket.write(wire.subarray(0, 11));
        setTimeout(() => socket.write(wire.subarray(11, 40)), 30);
        setTimeout(() => socket.write(wire.subarray(40)), 60);
    });
    const baselinePort = await listen(baseline);
    const listenPort = freePort();
    const bridge = createBridge({
        listenHost: '127.0.0.1', listenPort,
        baselineHost: '127.0.0.1', baselinePort,
        maxMessageBytes: 1 << 20
    });

    const socket = await openSocket(`ws://127.0.0.1:${listenPort}`);
    const messages = [];
    socket.on('message', data => messages.push(data));
    await new Promise(resolve => setTimeout(resolve, 500));

    assert.strictEqual(messages.length, 1);
    assert.ok(messages[0].equals(frameBytes));
    socket.close();
    await closeAll(bridge, baseline);
});

test('browser feedback reaches the baseline with a length prefix', async () => {
    const baseline = fakeBaseline();
    const baselinePort = await listen(baseline);
    const listenPort = freePort();
    const bridge = createBridge({
        listenHost: '127.0.0.1', listenPort,
        baselineHost: '127.0.0.1', baselinePort,
        maxMessageBytes: 1 << 20
    });

    const socket = await openSocket(`ws://127.0.0.1:${listenPort}`);
    const feedback = Buffer.from(GOLDEN.feedback_full, 'hex');
    socket.send(feedback);
    await new Promise(resolve => setTimeout(resolve, 400));

    assert.strictEqual(baseline.received.length, 1);
    assert.ok(baseline.received[0].equals(feedback),
        'the baseline sees exactly the bytes the browser sent');
    socket.close();
    await closeAll(bridge, baseline);
});

test('upstream junk is dropped rather than corrupting the baseline stream', async () => {
    const baseline = fakeBaseline();
    const baselinePort = await listen(baseline);
    const listenPort = freePort();
    const bridge = createBridge({
        listenHost: '127.0.0.1', listenPort,
        baselineHost: '127.0.0.1', baselinePort,
        maxMessageBytes: 1 << 20
    });

    const socket = await openSocket(`ws://127.0.0.1:${listenPort}`);
    socket.send('a text frame');                       // wrong type
    socket.send(Buffer.from('NOTV4DS_payload'));       // wrong magic
    socket.send(Buffer.from(GOLDEN.feedback_minimal, 'hex'));  // valid
    await new Promise(resolve => setTimeout(resolve, 400));

    assert.strictEqual(baseline.received.length, 1,
        'only the valid message was forwarded');
    socket.close();
    await closeAll(bridge, baseline);
});

test('a second browser is refused, since the baseline accepts one client', async () => {
    const baseline = fakeBaseline({ send: [connectionBytes] });
    const baselinePort = await listen(baseline);
    const listenPort = freePort();
    const bridge = createBridge({
        listenHost: '127.0.0.1', listenPort,
        baselineHost: '127.0.0.1', baselinePort,
        maxMessageBytes: 1 << 20
    });

    const first = await openSocket(`ws://127.0.0.1:${listenPort}`);
    await new Promise(resolve => setTimeout(resolve, 200));

    const closed = await new Promise(resolve => {
        const second = new WebSocket(`ws://127.0.0.1:${listenPort}`);
        second.once('close', (code, reason) =>
            resolve({ code, reason: reason.toString() }));
        second.once('error', () => resolve({ code: -1, reason: 'error' }));
    });
    assert.match(closed.reason, /already has a client/,
        'the second client is told why, not left hanging');

    first.close();
    await closeAll(bridge, baseline);
});

test('the browser is disconnected when the baseline goes away', async () => {
    // Leaving the page connected to a dead baseline would look like a stall
    // forever; a close is something the client can report and act on.
    const baseline = net.createServer(socket => {
        socket.write(frame(connectionBytes));
        setTimeout(() => socket.destroy(), 100);
    });
    const baselinePort = await listen(baseline);
    const listenPort = freePort();
    const bridge = createBridge({
        listenHost: '127.0.0.1', listenPort,
        baselineHost: '127.0.0.1', baselinePort,
        maxMessageBytes: 1 << 20
    });

    const socket = await openSocket(`ws://127.0.0.1:${listenPort}`);
    const closeCode = await new Promise(resolve => {
        socket.once('close', code => resolve(code));
        setTimeout(() => resolve(null), 2000);
    });
    assert.notStrictEqual(closeCode, null, 'the client socket was closed');
    await closeAll(bridge, baseline);
});

test('a browser connecting to a dead baseline is closed with the reason', async () => {
    const listenPort = freePort();
    const bridge = createBridge({
        listenHost: '127.0.0.1', listenPort,
        baselineHost: '127.0.0.1', baselinePort: 1,   // nothing listens on port 1
        maxMessageBytes: 1 << 20
    });
    const result = await new Promise(resolve => {
        const socket = new WebSocket(`ws://127.0.0.1:${listenPort}`);
        socket.once('close', (code, reason) =>
            resolve({ code, reason: reason.toString() }));
        socket.once('error', () => resolve({ code: -1, reason: 'error' }));
        setTimeout(() => resolve({ code: null, reason: 'timeout' }), 3000);
    });
    assert.notStrictEqual(result.reason, 'timeout',
        'a failed dial must not leave the browser waiting');
    await closeAll(bridge);
});

test('a large message survives the round trip intact', async () => {
    // A point-cloud keyframe is megabytes; this is where a naive proxy that
    // assumed one chunk per message falls over.
    const big = Buffer.concat([
        connectionBytes.subarray(0, 8), Buffer.alloc(3 * 1024 * 1024, 0x5a)]);
    const baseline = fakeBaseline({ send: [big] });
    const baselinePort = await listen(baseline);
    const listenPort = freePort();
    const bridge = createBridge({
        listenHost: '127.0.0.1', listenPort,
        baselineHost: '127.0.0.1', baselinePort,
        maxMessageBytes: 16 * 1024 * 1024
    });

    const socket = await openSocket(`ws://127.0.0.1:${listenPort}`);
    const received = await new Promise(resolve => {
        socket.once('message', data => resolve(data));
        setTimeout(() => resolve(null), 5000);
    });
    assert.ok(received, 'the large message arrived');
    assert.strictEqual(received.length, big.length);
    assert.ok(received.equals(big), 'byte-exact over 3 MiB');
    socket.close();
    await closeAll(bridge, baseline);
});
