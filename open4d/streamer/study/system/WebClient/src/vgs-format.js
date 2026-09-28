'use strict';

/**
 * VGS1 decoder: Vega's portable quantised Gaussian format.
 *
 * A JavaScript port of `vega_asset_splats` in `analysis/offline_benchmark.py`,
 * which is the authority. `tests/test_vgs_format.js` compares this against that
 * function's output on a real exported frame.
 *
 * The format exists because Vega's research bitstream is PyTorch tensors plus
 * tiny-cuda neural colour tables, which no thin client can read.
 * `baselines/Vega/orbitvega/export_quest.py` reconstructs each frame, bakes a
 * view-independent colour sampled from the calibrated source views, and
 * quantises each Gaussian into five uint32 words.
 *
 * Consequence worth stating plainly: colour here is BAKED, not view-dependent.
 * The Quest client can run the real neural colour path via WebGPU-less Vulkan
 * compute; a browser cannot, so what this renders is Vega's geometry and
 * opacity exactly, with an approximated colour. That is the same approximation
 * the offline evaluator uses, so it is comparable with those numbers — but it
 * is not the paper's full view-dependent appearance.
 *
 * Layout, LITTLE-endian (note: the V4DS baselines are big-endian; these two
 * formats disagree and mixing them up produces plausible garbage):
 *
 *   header, 48 bytes: '<4sHHII6fQ'
 *     magic     4s   "VGS1"
 *     version   u16  1
 *     objectId  u16
 *     frame     u32
 *     count     u32  number of Gaussians
 *     lower     3xf32  bounding box minimum
 *     upper     3xf32  bounding box maximum
 *     encoded   u64  original bitstream bytes for this frame (reporting only)
 *
 *   then `count` records of five u32 words:
 *     w0  bits  0..15  position x, UNORM16 within [lower, upper]
 *         bits 16..31  position y
 *     w1  bits  0..15  position z
 *         bits 16..23  scale x, byte -> log scale
 *         bits 24..31  scale y
 *     w2  bits  0..7   scale z
 *         bits  8..15  quaternion x, int8/127
 *         bits 16..23  quaternion y
 *         bits 24..31  quaternion z
 *     w3  bits  0..7   quaternion w
 *         bits  8..15  colour r
 *         bits 16..23  colour g
 *         bits 24..31  colour b
 *     w4  bits  0..7   opacity, byte/255
 *
 * Positions are bbox-relative UNORM16 rather than absolute fp16 on purpose:
 * both occupy six bytes, but the relative encoding avoids jumping across fine
 * hash-grid cells and producing false colour bands.
 */

const MAGIC = 'VGS1';
const HEADER_BYTES = 48;
const RECORD_WORDS = 5;
const RECORD_BYTES = RECORD_WORDS * 4;
const DEFAULT_SCALE_LOG_MIN = -12.0;
const DEFAULT_SCALE_LOG_MAX = 2.0;
/** Opacity below one quantisation step means the splat was pruned. */
const VISIBILITY_THRESHOLD = 1 / 255;

class VgsError extends Error {}

/** Read and validate the 48-byte header. */
function readHeader(buffer) {
    if (buffer.byteLength < HEADER_BYTES) {
        throw new VgsError(`truncated Vega asset: ${buffer.byteLength} bytes`);
    }
    const view = new DataView(buffer);
    const magic = String.fromCharCode(
        view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3));
    if (magic !== MAGIC) {
        throw new VgsError(`unsupported Vega asset: magic "${magic}"`);
    }
    const version = view.getUint16(4, true);
    if (version !== 1) {
        throw new VgsError(`unsupported Vega asset version ${version}`);
    }
    const objectId = view.getUint16(6, true);
    const frame = view.getUint32(8, true);
    const count = view.getUint32(12, true);
    if (count <= 0) throw new VgsError('Vega asset declares no Gaussians');

    const lower = [view.getFloat32(16, true), view.getFloat32(20, true),
                   view.getFloat32(24, true)];
    const upper = [view.getFloat32(28, true), view.getFloat32(32, true),
                   view.getFloat32(36, true)];
    const encodedBytes = Number(view.getBigUint64(40, true));

    const expected = HEADER_BYTES + count * RECORD_BYTES;
    if (buffer.byteLength !== expected) {
        throw new VgsError(
            `Vega asset byte count disagrees with header: ${buffer.byteLength} != ${expected}`);
    }
    return { magic, version, objectId, frame, count, lower, upper, encodedBytes };
}

/**
 * Decode a `.vgs` frame into renderable splat attributes.
 *
 * Splats whose opacity quantised to zero are dropped, matching the Python
 * decoder: they were pruned during encoding and drawing them would add cost
 * for no pixels.
 *
 * @param {ArrayBuffer} buffer
 * @param {object} [options]
 * @param {number} [options.scaleLogMin] from the export catalogue
 * @param {number} [options.scaleLogMax]
 * @returns {{count: number, positions: Float32Array, scales: Float32Array,
 *   rotations: Float32Array, opacities: Float32Array, colors: Float32Array,
 *   header: object}}
 *   `scales` are LOG scales (exponentiate for world size), `rotations` are
 *   normalised xyzw quaternions, `opacities` are linear 0..1, `colors` are
 *   linear 0..1 RGB.
 */
function decodeVgsFrame(buffer, {
    scaleLogMin = DEFAULT_SCALE_LOG_MIN,
    scaleLogMax = DEFAULT_SCALE_LOG_MAX
} = {}) {
    const header = readHeader(buffer);
    const { count, lower, upper } = header;
    const words = new Uint32Array(buffer, HEADER_BYTES, count * RECORD_WORDS);
    const scaleRange = scaleLogMax - scaleLogMin;

    const spanX = upper[0] - lower[0];
    const spanY = upper[1] - lower[1];
    const spanZ = upper[2] - lower[2];

    // Two passes: count the visible splats, then fill exactly-sized buffers.
    // One oversized allocation plus a copy would double peak memory for a
    // 58k-Gaussian frame, and these are decoded per frame at 30 fps.
    let visible = 0;
    for (let i = 0; i < count; i++) {
        if ((words[i * RECORD_WORDS + 4] & 0xff) >= 1) visible++;
    }

    const positions = new Float32Array(visible * 3);
    const scales = new Float32Array(visible * 3);
    const rotations = new Float32Array(visible * 4);
    const opacities = new Float32Array(visible);
    const colors = new Float32Array(visible * 3);

    let out = 0;
    for (let i = 0; i < count; i++) {
        const base = i * RECORD_WORDS;
        const w0 = words[base];
        const w1 = words[base + 1];
        const w2 = words[base + 2];
        const w3 = words[base + 3];
        const w4 = words[base + 4];

        const opacityByte = w4 & 0xff;
        if (opacityByte < 1) continue;   // pruned during encoding

        const qx = w0 & 0xffff;
        const qy = w0 >>> 16;
        const qz = w1 & 0xffff;
        positions[out * 3] = lower[0] + (qx / 65535) * spanX;
        positions[out * 3 + 1] = lower[1] + (qy / 65535) * spanY;
        positions[out * 3 + 2] = lower[2] + (qz / 65535) * spanZ;

        const sx = (w1 >>> 16) & 0xff;
        const sy = (w1 >>> 24) & 0xff;
        const sz = w2 & 0xff;
        scales[out * 3] = scaleLogMin + (sx / 255) * scaleRange;
        scales[out * 3 + 1] = scaleLogMin + (sy / 255) * scaleRange;
        scales[out * 3 + 2] = scaleLogMin + (sz / 255) * scaleRange;

        // Quaternion components are SIGNED bytes over 127. Reading them as
        // unsigned would mirror every rotation in a way that still looks like a
        // plausible blob.
        const rx = asInt8((w2 >>> 8) & 0xff) / 127;
        const ry = asInt8((w2 >>> 16) & 0xff) / 127;
        const rz = asInt8((w2 >>> 24) & 0xff) / 127;
        const rw = asInt8(w3 & 0xff) / 127;
        const norm = Math.max(Math.hypot(rx, ry, rz, rw), 1e-8);
        rotations[out * 4] = rx / norm;
        rotations[out * 4 + 1] = ry / norm;
        rotations[out * 4 + 2] = rz / norm;
        rotations[out * 4 + 3] = rw / norm;

        opacities[out] = opacityByte / 255;
        colors[out * 3] = ((w3 >>> 8) & 0xff) / 255;
        colors[out * 3 + 1] = ((w3 >>> 16) & 0xff) / 255;
        colors[out * 3 + 2] = ((w3 >>> 24) & 0xff) / 255;
        out++;
    }

    return {
        count: visible, positions, scales, rotations, opacities, colors, header
    };
}

function asInt8(byte) { return byte < 128 ? byte : byte - 256; }

/** Bytes a decoded frame occupies, for a cache budget. */
function decodedFrameBytes(frame) {
    return frame.positions.byteLength + frame.scales.byteLength
        + frame.rotations.byteLength + frame.opacities.byteLength
        + frame.colors.byteLength;
}

module.exports = {
    decodeVgsFrame, readHeader, decodedFrameBytes, VgsError,
    MAGIC, HEADER_BYTES, RECORD_WORDS, RECORD_BYTES,
    DEFAULT_SCALE_LOG_MIN, DEFAULT_SCALE_LOG_MAX, VISIBILITY_THRESHOLD
};
