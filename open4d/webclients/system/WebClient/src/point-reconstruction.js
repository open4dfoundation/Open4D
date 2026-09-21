'use strict';

/**
 * MetaStream / DeltaStream point-cloud reconstruction.
 *
 * A faithful port of `ReconstructionState` in
 * `baselines/DeltaStream/orbitstream/reconstruction.py`, which is the
 * authority. `tests/test_point_reconstruction.js` compares this against that
 * implementation's output on the same input, because the delta model is fiddly
 * enough that "looks about right on screen" is not evidence.
 *
 * The streamed points are in CAMERA space. The server encodes
 * `cloud.positions` unchanged and only uses `camera_to_world` for tile
 * assignment, so placing the points is the client's job — see `worldClouds()`.
 *
 * The delta model, per (object, camera) stream:
 *
 *   keyframe  the payload IS the whole cloud; replace.
 *   delta     project the previous cloud back into the source image to recover
 *             each point's block, then
 *               1. copy the points inside each motion's source block and
 *                  translate them by that motion's delta,
 *               2. drop every point whose block appears in removal_blocks,
 *               3. append the residual payload.
 *             A motion source may overlap a removal block; the translated copy
 *             survives, matching the reference desktop client.
 */

/** Camera-space cloud: interleaved xyz floats and rgb bytes. */
class PointCloud {
    constructor(positions, colors) {
        this.positions = positions;                  // Float32Array, 3N
        this.colors = colors;                        // Uint8Array, 3N
    }

    static empty() {
        return new PointCloud(new Float32Array(0), new Uint8Array(0));
    }

    get pointCount() { return this.positions.length / 3; }
}

/** Concatenate clouds in order. */
function concatClouds(clouds) {
    const total = clouds.reduce((sum, c) => sum + c.pointCount, 0);
    if (total === 0) return PointCloud.empty();
    const positions = new Float32Array(total * 3);
    const colors = new Uint8Array(total * 3);
    let offset = 0;
    for (const cloud of clouds) {
        positions.set(cloud.positions, offset);
        colors.set(cloud.colors, offset);
        offset += cloud.positions.length;
    }
    return new PointCloud(positions, colors);
}

class ReconstructionState {
    /** @param {object} header decoded CONNECTION message */
    constructor(header) {
        this.header = header;
        this.calibrations = new Map();
        for (const object of header.objects) {
            for (const camera of object.cameras) {
                this.calibrations.set(`${object.objectId}:${camera.cameraId}`,
                    { ...camera, objectId: object.objectId });
            }
        }
        this._cameraClouds = new Map();   // "obj:cam" -> PointCloud
        this.lastFrameId = -1;
    }

    reset() {
        this._cameraClouds.clear();
        this.lastFrameId = -1;
    }

    /**
     * Fold one FRAME into the state and return the per-object world clouds.
     *
     * @param {object} frame decoded FRAME message
     * @param {(draco: Uint8Array) => PointCloud} decodeDraco
     * @param {{strictOrder?: boolean}} [options]
     *   strictOrder mirrors the Python implementation, which refuses a gap in
     *   the frame sequence. A browser over a real link may legitimately miss a
     *   frame, in which case the stream cannot be reconstructed and the caller
     *   must resynchronise on the next keyframe rather than render nonsense.
     */
    apply(frame, decodeDraco, { strictOrder = true } = {}) {
        if (strictOrder && frame.frameId !== this.lastFrameId + 1) {
            throw new Error(
                `dependency frame out of order: expected ${this.lastFrameId + 1}, `
                + `received ${frame.frameId}`);
        }
        if (frame.frameId === 0 && frame.frameType !== 'keyframe') {
            throw new Error('a run must begin with a keyframe');
        }

        const seen = new Set();
        for (const record of frame.records) {
            const key = `${record.objectId}:${record.cameraId}`;
            if (seen.has(key)) throw new Error(`duplicate record for stream ${key}`);
            if (!this.calibrations.has(key)) {
                throw new Error(`unknown stream ${key}`);
            }
            seen.add(key);

            const residual = record.draco && record.draco.length
                ? decodeDraco(record.draco) : PointCloud.empty();
            if (residual.pointCount !== record.pointCount) {
                throw new Error(
                    `decoded point count differs for ${key}: `
                    + `${residual.pointCount} != ${record.pointCount}`);
            }

            if (frame.frameType === 'keyframe') {
                if (record.removalBlocks.length || record.motions.length) {
                    throw new Error('keyframes cannot contain delta operations');
                }
                this._cameraClouds.set(key, residual);
            } else {
                if (!this._cameraClouds.has(key)) {
                    throw new Error(`delta precedes keyframe for stream ${key}`);
                }
                this._cameraClouds.set(key, this._applyDelta(
                    this._cameraClouds.get(key), residual, record));
            }
        }

        this.lastFrameId = frame.frameId;
        return this.worldClouds();
    }

    /** Streams the header declares but this frame omitted. */
    missingStreams(seenKeys) {
        return [...this.calibrations.keys()].filter(key => !seenKeys.has(key));
    }

    _applyDelta(previous, residual, record) {
        const calibration = this.calibrations.get(
            `${record.objectId}:${record.cameraId}`);
        const { fx, fy, cx, cy } = calibration;
        const blockSize = this.header.blockSize;
        const blocksPerRow = Math.floor(this.header.width / blockSize);
        const count = previous.pointCount;
        const points = previous.positions;

        // Project each previous point back into its source image to recover the
        // block it belongs to. A point behind the camera or outside the frame
        // has no block, and must be excluded rather than clamped.
        const u = new Float32Array(count);
        const v = new Float32Array(count);
        const inImage = new Uint8Array(count);
        const blockIndices = new Int32Array(count).fill(-1);

        for (let i = 0; i < count; i++) {
            const z = points[i * 3 + 2];
            if (!(z > 0)) { u[i] = -1; v[i] = -1; continue; }
            const pu = (points[i * 3] / z) * fx + cx;
            const pv = (points[i * 3 + 1] / z) * fy + cy;
            u[i] = pu;
            v[i] = pv;
            if (pu >= 0 && pv >= 0 && pu < this.header.width
                && pv < this.header.height) {
                inImage[i] = 1;
                blockIndices[i] = Math.floor(pv / blockSize) * blocksPerRow
                    + Math.floor(pu / blockSize);
            }
        }

        // 1. Motion: translated copies of the points in each source block.
        const moved = [];
        for (const motion of record.motions) {
            const x0 = motion.sourceX - blockSize / 2;
            const y0 = motion.sourceY - blockSize / 2;
            const selected = [];
            for (let i = 0; i < count; i++) {
                if (!inImage[i]) continue;
                if (u[i] >= x0 && u[i] < x0 + blockSize
                    && v[i] >= y0 && v[i] < y0 + blockSize) {
                    selected.push(i);
                }
            }
            if (selected.length === 0) continue;
            const positions = new Float32Array(selected.length * 3);
            const colors = new Uint8Array(selected.length * 3);
            selected.forEach((source, target) => {
                positions[target * 3] = points[source * 3] + motion.deltaX;
                positions[target * 3 + 1] = points[source * 3 + 1] + motion.deltaY;
                positions[target * 3 + 2] = points[source * 3 + 2] + motion.deltaZ;
                colors[target * 3] = previous.colors[source * 3];
                colors[target * 3 + 1] = previous.colors[source * 3 + 1];
                colors[target * 3 + 2] = previous.colors[source * 3 + 2];
            });
            moved.push(new PointCloud(positions, colors));
        }

        // 2. Removal: drop points whose block was republished.
        let kept;
        if (record.removalBlocks.length) {
            const removed = new Set(Array.from(record.removalBlocks));
            const keepIndices = [];
            for (let i = 0; i < count; i++) {
                if (!removed.has(blockIndices[i])) keepIndices.push(i);
            }
            const positions = new Float32Array(keepIndices.length * 3);
            const colors = new Uint8Array(keepIndices.length * 3);
            keepIndices.forEach((source, target) => {
                positions.set(points.subarray(source * 3, source * 3 + 3), target * 3);
                colors.set(previous.colors.subarray(source * 3, source * 3 + 3),
                    target * 3);
            });
            kept = new PointCloud(positions, colors);
        } else {
            kept = previous;
        }

        // 3. Order matches the Python implementation: moved, residual, kept.
        const parts = [...moved];
        if (residual.pointCount) parts.push(residual);
        if (kept.pointCount) parts.push(kept);
        return concatClouds(parts);
    }

    /**
     * Per-object clouds in world space.
     *
     * `cameraToWorldRowMajor` is row-major (the Python encoder writes
     * `for row in matrix for v in row`), so the rotation is rows 0..2 columns
     * 0..2 and the translation is column 3 — indices 3, 7, 11.
     */
    worldClouds() {
        const byObject = new Map();
        for (const [key, cloud] of this._cameraClouds) {
            const calibration = this.calibrations.get(key);
            const m = calibration.cameraToWorldRowMajor;
            const count = cloud.pointCount;
            const positions = new Float32Array(count * 3);
            for (let i = 0; i < count; i++) {
                const x = cloud.positions[i * 3];
                const y = cloud.positions[i * 3 + 1];
                const z = cloud.positions[i * 3 + 2];
                positions[i * 3] = m[0] * x + m[1] * y + m[2] * z + m[3];
                positions[i * 3 + 1] = m[4] * x + m[5] * y + m[6] * z + m[7];
                positions[i * 3 + 2] = m[8] * x + m[9] * y + m[10] * z + m[11];
            }
            const transformed = new PointCloud(positions, cloud.colors);
            const objectId = calibration.objectId;
            byObject.set(objectId, byObject.has(objectId)
                ? concatClouds([byObject.get(objectId), transformed])
                : transformed);
        }
        return byObject;
    }

    get streamCount() { return this._cameraClouds.size; }
}

module.exports = { ReconstructionState, PointCloud, concatClouds };
