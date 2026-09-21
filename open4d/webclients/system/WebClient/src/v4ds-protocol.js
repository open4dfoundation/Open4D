'use strict';

/**
 * V4DS wire protocol, browser side.
 *
 * A JavaScript port of the decode half (and the FEEDBACK encode half) of
 * `baselines/DeltaStream/orbitstream/protocol.py`, which is the authority. The
 * five point-cloud baselines — MetaStream, DeltaStream, ViVo, NAVA and LiVo —
 * all speak this over a TCP socket; a browser cannot open one, so
 * `bridge/v4ds-bridge.js` proxies it over a WebSocket.
 *
 * Everything is BIG-ENDIAN (`>` in the Python structs). Getting that wrong does
 * not throw, it produces plausible-looking garbage, so the tests in
 * `tests/test_v4ds_protocol.js` compare against golden byte strings generated
 * by the Python encoder rather than against this file's own output.
 *
 * Message layout, common to all four types:
 *
 *   magic   4 bytes  "V4DS"
 *   version u16      1
 *   type    u8       1 CONNECTION | 2 FRAME | 3 FEEDBACK | 4 LIVO_SEGMENT
 *   flags   u8       FEEDBACK uses bit 0 for "frustum present"
 *
 * The 4-byte big-endian length prefix that precedes each message on the TCP
 * stream is NOT handled here — the bridge strips it and delivers whole
 * messages, so the browser never has to reassemble a stream.
 */

const MAGIC = 0x56344453;          // "V4DS"
const VERSION = 1;
const HEADER_BYTES = 8;

const MessageType = Object.freeze({
    CONNECTION: 1, FRAME: 2, FEEDBACK: 3, LIVO_SEGMENT: 4
});

const StreamMode = Object.freeze({
    1: 'metastream', 2: 'deltastream', 3: 'vivo', 4: 'nava', 5: 'livo',
    metastream: 1, deltastream: 2, vivo: 3, nava: 4, livo: 5
});

const FrameType = Object.freeze({ 1: 'keyframe', 2: 'delta' });

class ProtocolError extends Error {}

/** Sequential big-endian reader over an ArrayBuffer. */
class Reader {
    constructor(buffer, byteOffset = 0, byteLength = undefined) {
        this.view = new DataView(buffer, byteOffset,
            byteLength ?? buffer.byteLength - byteOffset);
        this.offset = 0;
    }

    get remaining() { return this.view.byteLength - this.offset; }

    _need(bytes) {
        if (this.remaining < bytes) {
            throw new ProtocolError(
                `truncated message: need ${bytes}, have ${this.remaining}`);
        }
    }

    u8() { this._need(1); return this.view.getUint8(this.offset++); }

    u16() {
        this._need(2);
        const value = this.view.getUint16(this.offset, false);
        this.offset += 2;
        return value;
    }

    i16() {
        this._need(2);
        const value = this.view.getInt16(this.offset, false);
        this.offset += 2;
        return value;
    }

    u32() {
        this._need(4);
        const value = this.view.getUint32(this.offset, false);
        this.offset += 4;
        return value;
    }

    /**
     * u64 as an exact BigInt.
     *
     * Nanosecond timestamps genuinely need this: epoch nanoseconds are ~1.8e18,
     * two hundred times Number.MAX_SAFE_INTEGER (9.0e15). An earlier version of
     * this reader assumed they stayed inside the exact Number range and threw on
     * every real frame. Whether a given server sends epoch or monotonic
     * nanoseconds is not something the wire format promises, so the decoder must
     * handle the full u64 range.
     */
    u64() {
        this._need(8);
        const value = this.view.getBigUint64(this.offset, false);
        this.offset += 8;
        return value;
    }

    /**
     * u64 as a Number, for fields that are genuinely small — a frame sequence
     * number, a record count. Refuses anything that would lose precision, since
     * for those fields an out-of-range value means a corrupt stream rather than
     * a large legitimate one.
     */
    u64AsNumber(field = 'value') {
        const value = this.u64();
        if (value > BigInt(Number.MAX_SAFE_INTEGER)) {
            throw new ProtocolError(
                `${field} ${value} exceeds the exact Number range`);
        }
        return Number(value);
    }

    f32() {
        this._need(4);
        const value = this.view.getFloat32(this.offset, false);
        this.offset += 4;
        return value;
    }

    f32s(count) {
        const out = new Array(count);
        for (let i = 0; i < count; i++) out[i] = this.f32();
        return out;
    }

    /** Length-prefixed UTF-8 string (u16 length). */
    string() {
        const length = this.u16();
        this._need(length);
        const bytes = new Uint8Array(
            this.view.buffer, this.view.byteOffset + this.offset, length);
        this.offset += length;
        return new TextDecoder().decode(bytes);
    }

    /** Raw bytes, copied so the caller can transfer or retain them safely. */
    bytes(length) {
        this._need(length);
        const out = new Uint8Array(
            this.view.buffer.slice(this.view.byteOffset + this.offset,
                this.view.byteOffset + this.offset + length));
        this.offset += length;
        return out;
    }

    finish() {
        if (this.remaining !== 0) {
            throw new ProtocolError(`${this.remaining} trailing bytes`);
        }
    }
}

/** Validate the 8-byte common header and return its flags. */
function readCommon(reader, expectedType, acceptedFlags = 0) {
    const magic = reader.u32();
    if (magic !== MAGIC) {
        throw new ProtocolError(
            `bad magic 0x${magic.toString(16)} (expected "V4DS")`);
    }
    const version = reader.u16();
    if (version !== VERSION) {
        throw new ProtocolError(`unsupported protocol version ${version}`);
    }
    const type = reader.u8();
    if (type !== expectedType) {
        throw new ProtocolError(
            `expected message type ${expectedType}, got ${type}`);
    }
    const flags = reader.u8();
    if ((flags & ~acceptedFlags) !== 0) {
        throw new ProtocolError(`unsupported flags ${flags}`);
    }
    return flags;
}

/** Peek at a message's type without consuming it. */
function messageType(buffer) {
    if (buffer.byteLength < HEADER_BYTES) {
        throw new ProtocolError('message shorter than the common header');
    }
    const view = new DataView(
        buffer instanceof ArrayBuffer ? buffer : buffer.buffer,
        buffer instanceof ArrayBuffer ? 0 : buffer.byteOffset);
    if (view.getUint32(0, false) !== MAGIC) {
        throw new ProtocolError('bad magic');
    }
    return view.getUint8(6);
}

// --------------------------------------------------------------------------
// CONNECTION
// --------------------------------------------------------------------------

/**
 * The stream header: mode, source video geometry, and every object's camera
 * calibration. ViVo and NAVA additionally carry a tile/ABR catalogue.
 *
 * `cameraToWorld` is 16 floats in ROW-major order — the Python encoder writes
 * `for row in camera_to_world for v in row`. Three.js `Matrix4.fromArray` wants
 * column-major, so use `Matrix4.set(...)` or transpose; feeding it directly is
 * a silently wrong transform.
 */
function decodeConnection(buffer) {
    const reader = new Reader(
        buffer instanceof ArrayBuffer ? buffer : buffer.buffer,
        buffer instanceof ArrayBuffer ? 0 : buffer.byteOffset,
        buffer.byteLength);
    readCommon(reader, MessageType.CONNECTION);

    const modeId = reader.u8();
    const mode = StreamMode[modeId];
    if (!mode) throw new ProtocolError(`unknown stream mode ${modeId}`);

    const width = reader.u16();
    const height = reader.u16();
    const fpsNum = reader.u16();
    const fpsDen = reader.u16();
    const blockSize = reader.u16();
    const objectCount = reader.u16();
    const calibrationHash = reader.string();

    const objects = [];
    for (let i = 0; i < objectCount; i++) {
        const objectId = reader.u16();
        const loopFrames = reader.u32();
        const cameraCount = reader.u16();
        const name = reader.string();
        const cameras = [];
        for (let c = 0; c < cameraCount; c++) {
            const cameraId = reader.u16();
            const [fx, fy, cx, cy] = reader.f32s(4);
            cameras.push({
                cameraId, fx, fy, cx, cy,
                cameraToWorldRowMajor: reader.f32s(16)
            });
        }
        objects.push({ objectId, name, loopFrames, cameras });
    }

    let livoMaxDepthMm = null;
    if (mode === 'livo') livoMaxDepthMm = reader.u16();

    let tileAbr = null;
    if (reader.remaining > 0) {
        const grid = reader.u8();
        const representationCount = reader.u8();
        const abrObjectCount = reader.u16();
        const representationRatios = reader.f32s(representationCount);
        const abrObjects = [];
        for (let i = 0; i < abrObjectCount; i++) {
            const objectId = reader.u16();
            const boundsMin = reader.f32s(3);
            const boundsMax = reader.f32s(3);
            const tileCount = reader.u16();
            const tiles = [];
            for (let t = 0; t < tileCount; t++) {
                const tileId = reader.u16();
                const pointCount = reader.u32();
                const representationBytes = [];
                for (let r = 0; r < representationCount; r++) {
                    representationBytes.push(reader.u32());
                }
                tiles.push({ tileId, pointCount, representationBytes });
            }
            abrObjects.push({ objectId, boundsMin, boundsMax, tiles });
        }
        tileAbr = { grid, representationRatios, objects: abrObjects };
    }

    reader.finish();
    return {
        mode, width, height, fpsNum, fpsDen, blockSize, calibrationHash,
        objects, tileAbr, livoMaxDepthMm,
        fps: fpsDen ? fpsNum / fpsDen : 30
    };
}

// --------------------------------------------------------------------------
// FRAME
// --------------------------------------------------------------------------

/**
 * One presentation frame: per object/camera, a Draco point-cloud payload plus
 * DeltaStream's removal blocks and motion vectors.
 *
 * The Draco payload is a POINT CLOUD (`draco_encoder -point_cloud -qp 11
 * -qg 8`), not a mesh — the decode worker must use `DecodeBufferToPointCloud`.
 * MetaStream sends a full cloud every frame; DeltaStream sends keyframes every
 * fifth frame and residuals between.
 */
function decodeFrame(buffer, { maxRecords = 4096 } = {}) {
    const reader = new Reader(
        buffer instanceof ArrayBuffer ? buffer : buffer.buffer,
        buffer instanceof ArrayBuffer ? 0 : buffer.byteOffset,
        buffer.byteLength);
    readCommon(reader, MessageType.FRAME);

    const frameTypeId = reader.u8();
    const frameType = FrameType[frameTypeId];
    if (!frameType) throw new ProtocolError(`unknown frame type ${frameTypeId}`);

    const frameId = reader.u64AsNumber('frameId');
    // Exact BigInts: these are nanoseconds and routinely exceed 2^53.
    const sourceTimestampNs = reader.u64();
    const encodeFinishedNs = reader.u64();
    const recordCount = reader.u16();
    if (recordCount > maxRecords) {
        throw new ProtocolError(`too many frame records: ${recordCount}`);
    }

    const records = [];
    for (let i = 0; i < recordCount; i++) {
        const objectId = reader.u16();
        const cameraId = reader.u16();
        const dracoBytes = reader.u32();
        const removalCount = reader.u32();
        const motionCount = reader.u32();
        const pointCount = reader.u32();
        if (removalCount > 1_000_000 || motionCount > 1_000_000) {
            throw new ProtocolError('unreasonable delta record count');
        }
        const draco = reader.bytes(dracoBytes);
        const removalBlocks = new Uint32Array(removalCount);
        for (let r = 0; r < removalCount; r++) removalBlocks[r] = reader.u32();
        const motions = [];
        for (let m = 0; m < motionCount; m++) {
            motions.push({
                blockIndex: reader.u32(),
                deltaX: reader.f32(),
                deltaY: reader.f32(),
                deltaZ: reader.f32(),
                sourceX: reader.i16(),
                sourceY: reader.i16()
            });
        }
        records.push({
            objectId, cameraId, draco, removalBlocks, motions, pointCount
        });
    }

    reader.finish();
    return {
        frameId,
        // BigInt nanoseconds, plus millisecond Numbers for latency arithmetic.
        // A millisecond is far coarser than the ~256 ns quantisation a Number
        // would impose at 1.8e18, so nothing is lost that anyone measures.
        sourceTimestampNs, encodeFinishedNs,
        sourceTimestampMs: Number(sourceTimestampNs / 1000000n),
        encodeFinishedMs: Number(encodeFinishedNs / 1000000n),
        frameType, records
    };
}

// --------------------------------------------------------------------------
// FEEDBACK
// --------------------------------------------------------------------------

/** Big-endian writer. */
class Writer {
    constructor() { this.bytes = []; }
    u8(v) { this.bytes.push(v & 0xff); return this; }
    u16(v) { this.bytes.push((v >>> 8) & 0xff, v & 0xff); return this; }
    u32(v) {
        this.bytes.push((v >>> 24) & 0xff, (v >>> 16) & 0xff,
                        (v >>> 8) & 0xff, v & 0xff);
        return this;
    }
    u64(v) {
        const big = BigInt(v);
        for (let shift = 56n; shift >= 0n; shift -= 8n) {
            this.bytes.push(Number((big >> shift) & 0xffn));
        }
        return this;
    }
    f32(v) {
        const buffer = new ArrayBuffer(4);
        new DataView(buffer).setFloat32(0, v, false);
        this.bytes.push(...new Uint8Array(buffer));
        return this;
    }
    f32s(values) { for (const v of values) this.f32(v); return this; }
    toUint8Array() { return new Uint8Array(this.bytes); }
}

/**
 * Viewer feedback: what was displayed, how fast, and where the camera is.
 *
 * Three nested tiers, and the Python encoder rejects partial ones:
 *   base      displayedFrameId, measuredFps, repeatedFrames
 *   extended  + viewPosition, viewForward, bandwidthMbps  (all or none)
 *   frustum   + viewUp, verticalFovDegrees, viewAspect, viewNear, viewFar
 *             (all or none, and requires extended; sets flag bit 0)
 *
 * Tile selections require the extended tier. `selectionsPresent` distinguishes
 * "no decision yet" from "a decision with zero visible tiles" — without it an
 * old selection stays active indefinitely.
 */
function encodeFeedback({
    displayedFrameId, measuredFps, repeatedFrames = 0,
    viewPosition = null, viewForward = null, bandwidthMbps = null,
    viewUp = null, verticalFovDegrees = null, viewAspect = null,
    viewNear = null, viewFar = null,
    selections = [], selectionsPresent = false
}) {
    const frustum = [viewUp, verticalFovDegrees, viewAspect, viewNear, viewFar];
    const frustumPresent = frustum.some(v => v !== null && v !== undefined);
    if (frustumPresent && frustum.some(v => v === null || v === undefined)) {
        throw new ProtocolError('complete viewer frustum must be supplied together');
    }
    const extended = [viewPosition, viewForward, bandwidthMbps];
    const extendedPresent = extended.some(v => v !== null && v !== undefined);
    if (frustumPresent && !extendedPresent) {
        throw new ProtocolError('viewer frustum requires extended feedback');
    }
    if (extendedPresent && extended.some(v => v === null || v === undefined)) {
        throw new ProtocolError(
            'view position, forward, and bandwidth must be supplied together');
    }
    const hasSelections = selectionsPresent || selections.length > 0;
    if (hasSelections && !extendedPresent) {
        throw new ProtocolError('tile selections require extended feedback');
    }

    const writer = new Writer();
    writer.u32(MAGIC).u16(VERSION).u8(MessageType.FEEDBACK)
          .u8(frustumPresent ? 1 : 0);
    writer.u64(displayedFrameId).f32(measuredFps).u32(repeatedFrames);

    if (extendedPresent) {
        writer.f32s(viewPosition).f32s(viewForward).f32(bandwidthMbps);
        if (frustumPresent) {
            writer.f32s(viewUp)
                  .f32(verticalFovDegrees).f32(viewAspect)
                  .f32(viewNear).f32(viewFar);
        }
        if (hasSelections) {
            writer.u16(selections.length);
            for (const s of selections) {
                writer.u16(s.objectId).u16(s.tileId).u8(s.representationId);
            }
        }
    }
    return writer.toUint8Array();
}

module.exports = {
    MAGIC, VERSION, HEADER_BYTES, MessageType, StreamMode, FrameType,
    ProtocolError, Reader, Writer,
    messageType, decodeConnection, decodeFrame, encodeFeedback, readCommon
};
