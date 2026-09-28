'use strict';

/**
 * MetaStream / DeltaStream reconstruction, checked against Python.
 *
 * `fixtures_recon_golden.json` was produced by running the authoritative
 * `ReconstructionState` from `baselines/DeltaStream/orbitstream/reconstruction.py`
 * over a synthetic keyframe plus two delta frames that exercise motion blocks,
 * removal blocks and residual append together — including a camera with a real
 * rotation so the camera-to-world step is covered.
 *
 * Comparing against that output matters because the delta model is easy to get
 * subtly wrong: an off-by-half on a motion block's origin, a removal applied
 * before motion instead of after, or a transposed camera matrix all produce a
 * cloud that still looks like a person. Point-for-point equality does not.
 *
 * Run: node --test tests/test_point_reconstruction.js
 */

const assert = require('assert');
const test = require('node:test');
const fs = require('fs');
const path = require('path');

const {
    ReconstructionState, PointCloud, concatClouds
} = require('../system/WebClient/src/point-reconstruction');

const GOLDEN = JSON.parse(fs.readFileSync(
    path.join(__dirname, 'fixtures_recon_golden.json'), 'utf8'));

const cloudFrom = entry => new PointCloud(
    new Float32Array(entry.positions), new Uint8Array(entry.colors));

function buildInputs() {
    const header = {
        width: GOLDEN.header.width,
        height: GOLDEN.header.height,
        blockSize: GOLDEN.header.blockSize,
        objects: GOLDEN.header.objects
    };
    const payloads = new Map(
        Object.entries(GOLDEN.payloads).map(([tag, e]) => [tag, cloudFrom(e)]));
    const decode = blob => {
        const tag = new TextDecoder().decode(blob);
        if (!tag) return PointCloud.empty();
        if (!payloads.has(tag)) throw new Error(`unknown payload ${tag}`);
        return payloads.get(tag);
    };
    const frames = GOLDEN.frames.map(f => ({
        frameId: f.frameId,
        frameType: f.frameType,
        records: f.records.map(r => ({
            objectId: r.objectId,
            cameraId: r.cameraId,
            draco: new TextEncoder().encode(r.payloadTag),
            removalBlocks: new Uint32Array(r.removalBlocks),
            motions: r.motions,
            pointCount: r.pointCount
        }))
    }));
    return { header, decode, frames };
}

test('the fixture describes a sequence that actually exercises the model', () => {
    const motions = GOLDEN.frames.flatMap(f => f.records.flatMap(r => r.motions));
    const removals = GOLDEN.frames.flatMap(f => f.records.flatMap(r => r.removalBlocks));
    assert.ok(motions.length >= 2, 'motion blocks present');
    assert.ok(removals.length >= 4, 'removal blocks present');
    assert.ok(GOLDEN.frames.some(f => f.frameType === 'keyframe'));
    assert.ok(GOLDEN.frames.filter(f => f.frameType === 'delta').length >= 2);
});

test('every frame reconstructs point-for-point identically to Python', () => {
    const { header, decode, frames } = buildInputs();
    const state = new ReconstructionState(header);

    frames.forEach((frame, index) => {
        const worlds = state.apply(frame, decode);
        const expected = GOLDEN.expected[index];

        assert.deepStrictEqual([...worlds.keys()].map(String).sort(),
            Object.keys(expected).sort(), `frame ${index}: object set`);

        for (const [objectId, want] of Object.entries(expected)) {
            const got = worlds.get(Number(objectId));
            assert.strictEqual(got.pointCount, want.count,
                `frame ${index} object ${objectId}: point count`);

            const gotPositions = Array.from(got.positions)
                .map(v => Number(v.toFixed(5)));
            // Float32 arithmetic in two languages: compare with a tolerance
            // rather than demanding identical last bits.
            assert.strictEqual(gotPositions.length, want.positions.length);
            for (let i = 0; i < gotPositions.length; i++) {
                assert.ok(Math.abs(gotPositions[i] - want.positions[i]) < 1e-4,
                    `frame ${index} object ${objectId} position[${i}]: `
                    + `${gotPositions[i]} != ${want.positions[i]}`);
            }
            assert.deepStrictEqual(Array.from(got.colors), want.colors,
                `frame ${index} object ${objectId}: colors must be exact`);
        }
    });
});

test('a keyframe replaces the cloud rather than accumulating', () => {
    const { header, decode, frames } = buildInputs();
    const state = new ReconstructionState(header);
    state.apply(frames[0], decode);
    const first = state.worldClouds().get(1).pointCount;
    // Re-apply the same keyframe as frame 0 after a reset.
    state.reset();
    state.apply(frames[0], decode);
    assert.strictEqual(state.worldClouds().get(1).pointCount, first,
        'replaying a keyframe must not double the cloud');
});

test('camera-to-world is applied as row-major with translation in column 3', () => {
    // Camera 1 in the fixture has camera_to_world = ((0,0,1,1),(0,1,0,2),
    // (-1,0,0,3),(0,0,0,1)). A camera-space point (0,0,1) must land at
    // (1+1, 2+0, 3+0) = (2,2,3). Transposing the matrix would give (1,2,2).
    const header = {
        width: 64, height: 48, blockSize: 16,
        objects: [{
            objectId: 1, name: 'x', loopFrames: 1,
            cameras: [{
                cameraId: 1, fx: 50, fy: 50, cx: 32, cy: 24,
                cameraToWorldRowMajor: [0, 0, 1, 1, 0, 1, 0, 2, -1, 0, 0, 3, 0, 0, 0, 1]
            }]
        }]
    };
    const state = new ReconstructionState(header);
    const cloud = new PointCloud(new Float32Array([0, 0, 1]),
        new Uint8Array([10, 20, 30]));
    state.apply({
        frameId: 0, frameType: 'keyframe',
        records: [{
            objectId: 1, cameraId: 1, draco: new Uint8Array([1]),
            removalBlocks: new Uint32Array(0), motions: [], pointCount: 1
        }]
    }, () => cloud);

    const world = state.worldClouds().get(1);
    assert.deepStrictEqual(
        Array.from(world.positions).map(v => Number(v.toFixed(5))), [2, 2, 3]);
});

test('removal drops exactly the points in the published blocks', () => {
    const header = {
        width: 64, height: 48, blockSize: 16,
        objects: [{
            objectId: 1, name: 'x', loopFrames: 1,
            cameras: [{
                cameraId: 0, fx: 50, fy: 50, cx: 32, cy: 24,
                cameraToWorldRowMajor: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
            }]
        }]
    };
    // Two points: one at image (8,8) -> block 0, one at (40,8) -> block 2.
    // blocksPerRow = 64/16 = 4, so block = floor(v/16)*4 + floor(u/16).
    const toCamera = (u, v, z) => [(u - 32) * z / 50, (v - 24) * z / 50, z];
    const positions = new Float32Array([...toCamera(8, 8, 1), ...toCamera(40, 8, 1)]);
    const keyframe = new PointCloud(positions,
        new Uint8Array([1, 1, 1, 2, 2, 2]));

    const state = new ReconstructionState(header);
    state.apply({
        frameId: 0, frameType: 'keyframe',
        records: [{ objectId: 1, cameraId: 0, draco: new Uint8Array([1]),
                    removalBlocks: new Uint32Array(0), motions: [], pointCount: 2 }]
    }, () => keyframe);

    state.apply({
        frameId: 1, frameType: 'delta',
        records: [{ objectId: 1, cameraId: 0, draco: new Uint8Array(0),
                    removalBlocks: new Uint32Array([0]), motions: [],
                    pointCount: 0 }]
    }, () => PointCloud.empty());

    const world = state.worldClouds().get(1);
    assert.strictEqual(world.pointCount, 1, 'block 0 was dropped');
    assert.deepStrictEqual(Array.from(world.colors), [2, 2, 2],
        'the surviving point is the one in block 2');
});

test('a motion copy survives even when its source block is also removed', () => {
    // Documented behaviour: "A motion source can overlap a removal block; the
    // translated copy remains, matching the reference desktop client." Dropping
    // it would make moving limbs disappear rather than move.
    const header = {
        width: 64, height: 48, blockSize: 16,
        objects: [{
            objectId: 1, name: 'x', loopFrames: 1,
            cameras: [{
                cameraId: 0, fx: 50, fy: 50, cx: 32, cy: 24,
                cameraToWorldRowMajor: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
            }]
        }]
    };
    const toCamera = (u, v, z) => [(u - 32) * z / 50, (v - 24) * z / 50, z];
    const keyframe = new PointCloud(
        new Float32Array(toCamera(8, 8, 1)), new Uint8Array([7, 7, 7]));

    const state = new ReconstructionState(header);
    state.apply({
        frameId: 0, frameType: 'keyframe',
        records: [{ objectId: 1, cameraId: 0, draco: new Uint8Array([1]),
                    removalBlocks: new Uint32Array(0), motions: [], pointCount: 1 }]
    }, () => keyframe);

    // Motion source centred at (8,8) with block size 16 covers u,v in [0,16).
    state.apply({
        frameId: 1, frameType: 'delta',
        records: [{
            objectId: 1, cameraId: 0, draco: new Uint8Array(0),
            removalBlocks: new Uint32Array([0]),
            motions: [{ blockIndex: 0, deltaX: 1, deltaY: 0, deltaZ: 0,
                        sourceX: 8, sourceY: 8 }],
            pointCount: 0
        }]
    }, () => PointCloud.empty());

    const world = state.worldClouds().get(1);
    assert.strictEqual(world.pointCount, 1,
        'the translated copy remains after its source block is removed');
    assert.ok(Math.abs(world.positions[0] - (keyframe.positions[0] + 1)) < 1e-5,
        'and it has been translated by the motion delta');
});

test('the state refuses inputs it cannot reconstruct from', () => {
    const { header, decode, frames } = buildInputs();

    const outOfOrder = new ReconstructionState(header);
    outOfOrder.apply(frames[0], decode);
    assert.throws(() => outOfOrder.apply(frames[2], decode),
        /out of order/, 'a skipped frame cannot be reconstructed');

    const noKeyframe = new ReconstructionState(header);
    assert.throws(() => noKeyframe.apply(
        { ...frames[1], frameId: 0 }, decode), /must begin with a keyframe/);

    const duplicate = new ReconstructionState(header);
    assert.throws(() => duplicate.apply({
        frameId: 0, frameType: 'keyframe',
        records: [frames[0].records[0], frames[0].records[0]]
    }, decode), /duplicate record/);

    const unknown = new ReconstructionState(header);
    assert.throws(() => unknown.apply({
        frameId: 0, frameType: 'keyframe',
        records: [{ ...frames[0].records[0], objectId: 99 }]
    }, decode), /unknown stream/);
});

test('a point count that disagrees with the payload is rejected', () => {
    // This is the signal that Draco decoding went wrong; rendering the
    // mismatched cloud would corrupt every later delta silently.
    const { header, decode, frames } = buildInputs();
    const state = new ReconstructionState(header);
    const bad = {
        ...frames[0],
        records: [{ ...frames[0].records[0], pointCount: 999 }]
    };
    assert.throws(() => state.apply(bad, decode), /point count differs/);
});

test('strictOrder can be relaxed for a lossy link', () => {
    // A browser may genuinely miss a frame. The caller then has to resynchronise
    // on the next keyframe, but must be able to choose that rather than have the
    // reconstruction throw mid-render.
    const { header, decode, frames } = buildInputs();
    const state = new ReconstructionState(header);
    state.apply(frames[0], decode);
    assert.doesNotThrow(
        () => state.apply(frames[2], decode, { strictOrder: false }));
});

test('concatClouds preserves order and handles the empty case', () => {
    const a = new PointCloud(new Float32Array([1, 2, 3]), new Uint8Array([1, 1, 1]));
    const b = new PointCloud(new Float32Array([4, 5, 6]), new Uint8Array([2, 2, 2]));
    const joined = concatClouds([a, b]);
    assert.deepStrictEqual(Array.from(joined.positions), [1, 2, 3, 4, 5, 6]);
    assert.deepStrictEqual(Array.from(joined.colors), [1, 1, 1, 2, 2, 2]);
    assert.strictEqual(concatClouds([]).pointCount, 0);
    assert.strictEqual(concatClouds([PointCloud.empty()]).pointCount, 0);
});
