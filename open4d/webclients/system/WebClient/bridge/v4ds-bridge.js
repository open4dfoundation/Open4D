#!/usr/bin/env node
'use strict';

/**
 * WebSocket <-> TCP bridge for the V4DS baseline protocol.
 *
 * The five point-cloud baselines (MetaStream, DeltaStream, ViVo, NAVA, LiVo)
 * serve over a raw TCP socket, which a browser cannot open. This proxies one
 * WebSocket client onto one TCP baseline server.
 *
 * It is deliberately a DUMB proxy in everything except framing. It never
 * decodes a message, never rewrites a field, and never invents traffic — so a
 * browser run and a Quest run reach the baseline server with identical bytes,
 * and any difference in their results is a real difference rather than a
 * bridging artifact.
 *
 * What it DOES do is reframe:
 *
 *   TCP  ->  browser   strip the 4-byte big-endian length prefix, deliver each
 *                      complete message as one binary WebSocket message
 *   browser ->  TCP    prepend the length prefix to each WebSocket message
 *
 * That is worth the small amount of state because TCP is a byte stream: without
 * it the browser would have to reassemble partial messages itself, and a
 * point-cloud keyframe can easily span many TCP segments. WebSocket is already
 * message-framed, so this puts the boundary in the one place that has to know
 * about it.
 *
 * One client at a time, because the baseline servers themselves `listen(1)` and
 * `accept()` exactly once. A second browser is refused with a clear reason
 * rather than being silently queued behind the first.
 *
 * Usage:
 *   node bridge/v4ds-bridge.js --baseline-port 12345 [options]
 *
 *   --listen-port N      WebSocket port to serve            (default 8790)
 *   --listen-host H      WebSocket bind address             (default 0.0.0.0)
 *   --baseline-host H    baseline TCP host                  (default 127.0.0.1)
 *   --baseline-port N    baseline TCP port                  (required)
 *   --max-message-bytes  refuse larger frames               (default 256 MiB)
 *   --verbose            log every message
 */

const net = require('net');
const { WebSocketServer } = require('ws');
const { ShapingFollower } = require('../../Server/study/follower');

const MAGIC = Buffer.from('V4DS');
const PREFIX_BYTES = 4;
const DEFAULT_MAX_MESSAGE_BYTES = 256 * 1024 * 1024;

function parseArgs(argv) {
    const options = {
        listenHost: '0.0.0.0',
        listenPort: 8790,
        baselineHost: '127.0.0.1',
        baselinePort: null,
        maxMessageBytes: DEFAULT_MAX_MESSAGE_BYTES,
        verbose: false,
        // Follow a browser study's network trace; see follower.js. The env var
        // is how a bridge the server spawns inherits it without new arguments.
        studyServer: process.env.VS4D_STUDY_SERVER || null
    };
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        const next = () => {
            const value = argv[++i];
            if (value === undefined) throw new Error(`${arg} needs a value`);
            return value;
        };
        switch (arg) {
            case '--listen-host': options.listenHost = next(); break;
            case '--listen-port': options.listenPort = Number(next()); break;
            case '--baseline-host': options.baselineHost = next(); break;
            case '--baseline-port': options.baselinePort = Number(next()); break;
            case '--max-message-bytes':
                options.maxMessageBytes = Number(next()); break;
            case '--verbose': options.verbose = true; break;
            case '--study-server': options.studyServer = next(); break;
            case '--help': case '-h': options.help = true; break;
            default: throw new Error(`unknown argument: ${arg}`);
        }
    }
    return options;
}

/**
 * Reassembles length-prefixed V4DS messages from a TCP byte stream.
 *
 * Kept as a class with no I/O so it can be tested directly against adversarial
 * chunk boundaries — one byte at a time, several messages in one chunk, a prefix
 * split across chunks. Those are exactly the cases that work by luck on
 * localhost and fail on a real link.
 */
class MessageFramer {
    constructor({ maxMessageBytes = DEFAULT_MAX_MESSAGE_BYTES } = {}) {
        this.maxMessageBytes = maxMessageBytes;
        this._buffer = Buffer.alloc(0);
    }

    /** @returns {Buffer[]} every complete message now available */
    push(chunk) {
        this._buffer = this._buffer.length === 0
            ? chunk : Buffer.concat([this._buffer, chunk]);

        const messages = [];
        for (;;) {
            if (this._buffer.length < PREFIX_BYTES) break;
            const length = this._buffer.readUInt32BE(0);
            if (length > this.maxMessageBytes) {
                throw new Error(
                    `message length ${length} exceeds the ${this.maxMessageBytes} limit`);
            }
            if (this._buffer.length < PREFIX_BYTES + length) break;
            messages.push(
                this._buffer.subarray(PREFIX_BYTES, PREFIX_BYTES + length));
            this._buffer = this._buffer.subarray(PREFIX_BYTES + length);
        }
        return messages;
    }

    /** Bytes held back waiting for the rest of a message. */
    get pendingBytes() { return this._buffer.length; }
}

/** Prepend the 4-byte big-endian length prefix. */
function frame(payload) {
    const prefix = Buffer.alloc(PREFIX_BYTES);
    prefix.writeUInt32BE(payload.length, 0);
    return Buffer.concat([prefix, payload]);
}

function looksLikeV4ds(message) {
    return message.length >= 8 && message.subarray(0, 4).equals(MAGIC);
}

function timestamp() {
    return new Date().toISOString().replace('T', ' ').replace('Z', '');
}

function log(level, message) {
    const line = `[${timestamp()}][${level}][BRIDGE] ${message}`;
    if (level === 'ERROR') console.error(line);
    else console.log(line);
}

/** Above this much waiting to cross the shaped link, stop reading the baseline. */
const SHAPED_QUEUE_HIGH_BYTES = 4 * 1024 * 1024;
const SHAPED_QUEUE_LOW_BYTES = 1024 * 1024;

function createBridge(options) {
    const server = new WebSocketServer({
        host: options.listenHost,
        port: options.listenPort
    });
    let active = null;
    // Shared by every connection: it is one link. Unarmed it passes everything
    // straight through, so outside a study trial the bridge is the dumb proxy
    // it always was.
    const follower = options.shaper ? { shaper: options.shaper, stop() {} }
        : options.studyServer
            ? new ShapingFollower({ serverUrl: options.studyServer, log }).start()
            : null;
    server.on('close', () => follower?.stop());

    server.on('listening', () => {
        log('INFO', `WebSocket on ws://${options.listenHost}:${options.listenPort}`
            + ` -> tcp://${options.baselineHost}:${options.baselinePort}`);
    });

    server.on('connection', (socket, request) => {
        const peer = request.socket.remoteAddress;

        // The baseline servers accept exactly one connection, so admitting a
        // second browser would leave it waiting forever with no explanation.
        if (active) {
            log('WARN', `refusing ${peer}: a client is already connected`);
            socket.close(1013, 'bridge already has a client');
            return;
        }

        log('INFO', `client ${peer} connected; dialling the baseline server`);
        const tcp = net.createConnection(
            { host: options.baselineHost, port: options.baselinePort });
        tcp.setNoDelay(true);
        const framer = new MessageFramer(options);
        active = { socket, tcp };

        let fromBaseline = 0;
        let toBaseline = 0;
        // Downstream messages waiting to cross the shaped link, in order.
        const queue = [];
        let queued = 0;
        let draining = false;
        const deliver = message => {
            if (socket.readyState === socket.OPEN) socket.send(message);
        };
        const drain = async () => {
            if (draining) return;
            draining = true;
            while (queue.length && active?.socket === socket) {
                const message = queue.shift();
                queued -= message.length;
                // Held until the trace says the link can carry it. That delay
                // backs up into the baseline's own TCP send, which is the
                // pressure its adaptation reacts to -- as it would behind tc.
                await follower.shaper.take(message.length);
                deliver(message);
                if (tcp.isPaused() && queued < SHAPED_QUEUE_LOW_BYTES) tcp.resume();
            }
            draining = false;
        };

        const shutdown = (reason, code = 1000) => {
            if (active?.socket !== socket) return;
            active = null;
            log('INFO', `closing (${reason}); ${fromBaseline} messages down, `
                + `${toBaseline} up`);
            try { tcp.destroy(); } catch (_) { /* already gone */ }
            try { socket.close(code, reason.slice(0, 120)); } catch (_) { /* ditto */ }
        };

        tcp.on('connect', () => {
            log('INFO', 'baseline connected');
        });

        tcp.on('data', chunk => {
            let messages;
            try {
                messages = framer.push(chunk);
            } catch (err) {
                log('ERROR', `framing failed: ${err.message}`);
                shutdown('framing error', 1011);
                return;
            }
            for (const message of messages) {
                fromBaseline++;
                if (options.verbose) {
                    log('DEBUG', `down #${fromBaseline} type=`
                        + `${looksLikeV4ds(message) ? message.readUInt8(6) : '?'}`
                        + ` ${message.length}B`);
                }
                if (follower) { queue.push(message); queued += message.length; }
                else deliver(message);
            }
            if (follower) {
                if (queued > SHAPED_QUEUE_HIGH_BYTES) tcp.pause();
                drain();
            }
            // Backpressure: if the browser cannot keep up, stop reading from the
            // baseline rather than growing an unbounded buffer in this process.
            if (socket.bufferedAmount > 32 * 1024 * 1024) {
                tcp.pause();
                const resume = () => {
                    if (socket.bufferedAmount < 8 * 1024 * 1024) tcp.resume();
                    else setTimeout(resume, 20);
                };
                setTimeout(resume, 20);
            }
        });

        tcp.on('error', err => {
            log('ERROR', `baseline socket: ${err.message}`);
            shutdown(`baseline error: ${err.message}`, 1011);
        });
        tcp.on('close', () => shutdown('baseline closed the connection'));

        socket.on('message', (data, isBinary) => {
            if (!isBinary) {
                log('WARN', 'ignoring a text frame; V4DS is binary');
                return;
            }
            const payload = Buffer.isBuffer(data) ? data : Buffer.from(data);
            if (!looksLikeV4ds(payload)) {
                log('WARN', `dropping a ${payload.length}B upstream message `
                    + 'without the V4DS magic');
                return;
            }
            toBaseline++;
            if (options.verbose) {
                log('DEBUG', `up #${toBaseline} type=${payload.readUInt8(6)}`
                    + ` ${payload.length}B`);
            }
            if (!tcp.destroyed) tcp.write(frame(payload));
        });

        socket.on('close', () => shutdown('client disconnected'));
        socket.on('error', err => {
            log('ERROR', `client socket: ${err.message}`);
            shutdown(`client error: ${err.message}`, 1011);
        });
    });

    server.on('error', err => log('ERROR', `WebSocket server: ${err.message}`));
    return server;
}

function main(argv) {
    let options;
    try {
        options = parseArgs(argv);
    } catch (err) {
        console.error(`${err.message}\n`);
        console.error(require('fs').readFileSync(__filename, 'utf8')
            .split('\n').slice(2, 44).join('\n'));
        process.exit(2);
    }
    if (options.help || !options.baselinePort) {
        console.log(require('fs').readFileSync(__filename, 'utf8')
            .split('\n').slice(2, 44).join('\n'));
        process.exit(options.help ? 0 : 2);
    }
    const server = createBridge(options);
    for (const signal of ['SIGINT', 'SIGTERM']) {
        process.on(signal, () => {
            log('INFO', `received ${signal}, shutting down`);
            server.close(() => process.exit(0));
        });
    }
}

if (require.main === module) main(process.argv.slice(2));

module.exports = { MessageFramer, frame, createBridge, parseArgs, looksLikeV4ds };
