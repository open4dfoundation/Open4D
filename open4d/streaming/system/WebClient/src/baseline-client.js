'use strict';

/**
 * Browser client for the V4DS point-cloud baselines.
 *
 * MetaStream, DeltaStream, ViVo, NAVA and LiVo all speak the same protocol, so
 * one client serves all five; the mode arrives in the CONNECTION header rather
 * than being configured here.
 *
 * This does NOT use `ClientCore`, and that is the right call rather than an
 * omission. ClientCore implements *our* system: an HTTP segment loop, a
 * published ladder, an MCKP selector choosing one representation per object per
 * segment. A baseline pushes frames at its own cadence over a socket and makes
 * its own adaptation decisions server-side. Wrapping that in a segment loop
 * would model the baselines as something they are not, and the comparison would
 * measure the wrapper.
 *
 * What it does:
 *   1. connect to the bridge, which proxies the baseline's TCP socket
 *   2. decode CONNECTION -> calibrations, stream mode, tile catalogue
 *   3. per FRAME: decode every record's Draco point cloud in the worker, fold
 *      it through `ReconstructionState`, hand the world clouds to the renderer
 *   4. send FEEDBACK at the content rate: displayed frame, measured fps,
 *      measured goodput, and the live camera
 */

const {
    decodeConnection, decodeFrame, encodeFeedback, messageType, MessageType
} = require('./v4ds-protocol');
const { ReconstructionState, PointCloud } = require('./point-reconstruction');
const { PointRenderer } = require('./point-renderer');

/** Rolling goodput estimate over the received WebSocket bytes. */
class GoodputMeter {
    constructor({ windowMs = 2000, now = () => performance.now() } = {}) {
        this.windowMs = windowMs;
        this._now = now;
        this._samples = [];   // { at, bytes }
    }

    record(bytes) {
        const at = this._now();
        this._samples.push({ at, bytes });
        const cutoff = at - this.windowMs;
        while (this._samples.length && this._samples[0].at < cutoff) {
            this._samples.shift();
        }
    }

    /** Mbps over the window, or null before there is enough to divide by. */
    get mbps() {
        if (this._samples.length < 2) return null;
        const span = this._samples[this._samples.length - 1].at - this._samples[0].at;
        if (span <= 0) return null;
        const bytes = this._samples.reduce((sum, s) => sum + s.bytes, 0);
        return (bytes * 8) / span / 1000;
    }
}

class BaselineClient {
    /**
     * @param {object} args
     * @param {string} args.bridgeUrl        ws:// address of v4ds-bridge
     * @param {HTMLCanvasElement} args.canvas
     * @param {string} [args.workerUrl]
     * @param {string} [args.vendorBase]
     * @param {number} [args.feedbackIntervalMs]
     * @param {boolean} [args.strictOrder]   throw on a frame gap (default false
     *   in the browser: a lossy link is normal, and resynchronising on the next
     *   keyframe beats aborting the run)
     * @param {(event: object) => void} [args.onEvent]
     */
    constructor({
        bridgeUrl, canvas, workerUrl = '/web/draco-worker.js',
        vendorBase = '/web/vendor/draco', feedbackIntervalMs = 200,
        strictOrder = false, onEvent = null, pointSize = 0.012
    }) {
        this.bridgeUrl = bridgeUrl;
        this.workerUrl = workerUrl;
        this.vendorBase = vendorBase;
        this.feedbackIntervalMs = feedbackIntervalMs;
        this.strictOrder = strictOrder;
        this._onEvent = onEvent;

        this.renderer = new PointRenderer({ canvas, pointSize });
        this.header = null;
        this.state = null;
        this.mode = null;

        this.stats = {
            framesReceived: 0, framesReconstructed: 0, framesDropped: 0,
            bytesReceived: 0, resyncs: 0, decodeFailures: 0,
            lastFrameId: -1, displayedFrameId: -1, measuredFps: 0
        };

        this._socket = null;
        this._worker = null;
        this._workerSeq = 0;
        this._workerWaiters = new Map();
        this._feedbackTimer = null;
        this._goodput = new GoodputMeter();
        this._frameTimestamps = [];
        this._busy = false;
        this._pendingFrame = null;
        this._awaitingKeyframe = false;
    }

    _emit(type, detail = {}) {
        this._onEvent?.({ type, ...detail });
    }

    async start() {
        this.renderer.start();
        this._worker = new Worker(this.workerUrl);
        this._worker.onmessage = event => {
            const waiter = this._workerWaiters.get(event.data.id);
            this._workerWaiters.delete(event.data.id);
            if (!waiter) return;
            if (event.data.error) waiter.reject(new Error(event.data.error));
            else waiter.resolve(event.data.frames);
        };
        this._worker.onerror = err =>
            this._emit('error', { message: `draco worker: ${err.message}` });

        await this._connect();
        this._feedbackTimer = setInterval(
            () => this._sendFeedback(), this.feedbackIntervalMs);
    }

    _connect() {
        return new Promise((resolve, reject) => {
            const socket = new WebSocket(this.bridgeUrl);
            socket.binaryType = 'arraybuffer';
            this._socket = socket;

            socket.onopen = () => {
                this._emit('open', { url: this.bridgeUrl });
                resolve();
            };
            socket.onerror = () => {
                const message = `could not reach the bridge at ${this.bridgeUrl}`;
                this._emit('error', { message });
                reject(new Error(message));
            };
            socket.onclose = event => this._emit('closed', {
                code: event.code,
                reason: event.reason || '(no reason given)'
            });
            socket.onmessage = event => this._onMessage(event.data);
        });
    }

    async _onMessage(buffer) {
        this.stats.bytesReceived += buffer.byteLength;
        this._goodput.record(buffer.byteLength);

        let type;
        try {
            type = messageType(buffer);
        } catch (err) {
            this._emit('error', { message: `bad message: ${err.message}` });
            return;
        }

        if (type === MessageType.CONNECTION) {
            this._onConnectionHeader(buffer);
            return;
        }
        if (type === MessageType.FRAME) {
            // Only one frame is reconstructed at a time; a newer frame replaces
            // any frame still waiting, because showing the freshest content
            // matters more than showing every frame of a backlog.
            if (this._busy) {
                if (this._pendingFrame) this.stats.framesDropped++;
                this._pendingFrame = buffer;
                return;
            }
            await this._drainFrames(buffer);
            return;
        }
        if (type === MessageType.LIVO_SEGMENT) {
            // LiVo ships HEVC colour+depth rather than point clouds; it needs
            // the texture decoder and an unprojection step, not this path.
            this._emit('unsupported', {
                message: 'LiVo segments need RGB-D unprojection, not implemented'
            });
            return;
        }
        this._emit('error', { message: `unexpected message type ${type}` });
    }

    _onConnectionHeader(buffer) {
        try {
            this.header = decodeConnection(buffer);
        } catch (err) {
            this._emit('error', { message: `bad CONNECTION: ${err.message}` });
            return;
        }
        this.mode = this.header.mode;
        this.state = new ReconstructionState(this.header);
        this._emit('header', {
            mode: this.header.mode,
            objects: this.header.objects.map(o => ({
                objectId: o.objectId, name: o.name,
                cameras: o.cameras.length, loopFrames: o.loopFrames
            })),
            fps: this.header.fps,
            source: `${this.header.width}x${this.header.height}`,
            blockSize: this.header.blockSize,
            tiled: Boolean(this.header.tileAbr)
        });
    }

    async _drainFrames(first) {
        this._busy = true;
        let buffer = first;
        try {
            while (buffer) {
                await this._handleFrame(buffer);
                buffer = this._pendingFrame;
                this._pendingFrame = null;
            }
        } finally {
            this._busy = false;
        }
    }

    async _handleFrame(buffer) {
        if (!this.state) {
            this._emit('error', { message: 'FRAME arrived before CONNECTION' });
            return;
        }
        let frame;
        try {
            frame = decodeFrame(buffer);
        } catch (err) {
            this._emit('error', { message: `bad FRAME: ${err.message}` });
            return;
        }
        this.stats.framesReceived++;
        this.stats.lastFrameId = frame.frameId;

        // After a gap the delta chain is broken; wait for a keyframe rather
        // than compounding the error into visible corruption.
        if (this._awaitingKeyframe) {
            if (frame.frameType !== 'keyframe') return;
            this._awaitingKeyframe = false;
            this.state.reset();
        }

        const clouds = await this._decodeRecords(frame);
        if (!clouds) return;

        try {
            const worlds = this.state.apply(frame, blob => {
                const cloud = clouds.get(blobKey(blob));
                return cloud || PointCloud.empty();
            }, { strictOrder: this.strictOrder });
            this.renderer.update(worlds);
            this.stats.framesReconstructed++;
            this.stats.displayedFrameId = frame.frameId;
            this._recordPresentation();
        } catch (err) {
            this.stats.resyncs++;
            this._awaitingKeyframe = true;
            this._emit('resync', { message: err.message, frameId: frame.frameId });
        }
    }

    /**
     * Decode every record's Draco payload in one worker round trip.
     *
     * `ReconstructionState.apply` wants a synchronous decoder, so the payloads
     * are decoded up front and looked up by identity. One round trip per frame
     * also beats one per record: a nine-object scene with four cameras is 36
     * payloads, and 36 postMessage round trips would not fit in a frame budget.
     */
    async _decodeRecords(frame) {
        const withPayload = frame.records.filter(r => r.draco && r.draco.length);
        if (withPayload.length === 0) return new Map();

        // COPY, do not transfer. Transferring `record.draco.buffer` detaches it,
        // after which `record.draco.length` reads 0 — and
        // ReconstructionState.apply uses exactly that length to decide whether a
        // record has a payload. It would silently treat every record as empty
        // and then fail the point-count check. A memcpy of a few hundred KB is
        // negligible beside the Draco decode it feeds.
        const buffers = withPayload.map(r => r.draco.slice().buffer);
        const id = ++this._workerSeq;
        let decoded;
        try {
            decoded = await new Promise((resolve, reject) => {
                this._workerWaiters.set(id, { resolve, reject });
                this._worker.postMessage(
                    { id, buffers, vendorBase: this.vendorBase }, buffers);
            });
        } catch (err) {
            this.stats.decodeFailures++;
            this._emit('error', { message: `draco decode: ${err.message}` });
            return null;
        }

        const clouds = new Map();
        withPayload.forEach((record, index) => {
            const frameData = decoded[index];
            if (!frameData || frameData.kind !== 'cloud') {
                this.stats.decodeFailures++;
                clouds.set(blobKey(record.draco), PointCloud.empty());
                return;
            }
            clouds.set(blobKey(record.draco), new PointCloud(
                frameData.positions,
                frameData.colors || new Uint8Array(frameData.positions.length)));
        });
        return clouds;
    }

    _recordPresentation() {
        const now = performance.now();
        this._frameTimestamps.push(now);
        while (this._frameTimestamps.length
               && this._frameTimestamps[0] < now - 2000) {
            this._frameTimestamps.shift();
        }
        if (this._frameTimestamps.length >= 2) {
            const span = now - this._frameTimestamps[0];
            this.stats.measuredFps =
                ((this._frameTimestamps.length - 1) * 1000) / span;
        }
    }

    _sendFeedback() {
        if (!this._socket || this._socket.readyState !== WebSocket.OPEN) return;
        if (!this.header) return;
        const viewer = this.renderer.viewerState();
        const bandwidth = this._goodput.mbps;

        // The frustum tier requires the extended tier, and both are all-or-
        // nothing; sending a partial one is a protocol error the server rejects.
        const payload = {
            displayedFrameId: Math.max(0, this.stats.displayedFrameId),
            measuredFps: this.stats.measuredFps || this.header.fps,
            repeatedFrames: this.stats.framesDropped
        };
        if (bandwidth !== null) {
            Object.assign(payload, {
                viewPosition: viewer.position,
                viewForward: viewer.forward,
                bandwidthMbps: bandwidth,
                viewUp: viewer.up,
                verticalFovDegrees: viewer.verticalFovDegrees,
                viewAspect: viewer.aspect,
                viewNear: viewer.near,
                viewFar: viewer.far
            });
        }
        try {
            this._socket.send(encodeFeedback(payload));
        } catch (err) {
            this._emit('error', { message: `feedback: ${err.message}` });
        }
    }

    stop() {
        if (this._feedbackTimer) clearInterval(this._feedbackTimer);
        this._feedbackTimer = null;
        try { this._socket?.close(1000, 'client stopped'); } catch (_) { /* closed */ }
        this._worker?.terminate();
        this._worker = null;
        this.renderer.stop();
    }

    inspect() {
        return {
            mode: this.mode,
            header: this.header && {
                objects: this.header.objects.length,
                fps: this.header.fps,
                tiled: Boolean(this.header.tileAbr)
            },
            stats: { ...this.stats, goodputMbps: this._goodput.mbps },
            renderer: this.renderer.stats,
            viewer: this.header ? this.renderer.viewerState() : null,
            awaitingKeyframe: this._awaitingKeyframe
        };
    }
}

/** Identity key for a payload, so the sync decoder callback can look it up. */
let blobCounter = 0;
const blobKeys = new WeakMap();
function blobKey(blob) {
    if (!blobKeys.has(blob)) blobKeys.set(blob, ++blobCounter);
    return blobKeys.get(blob);
}

module.exports = { BaselineClient, GoodputMeter };
