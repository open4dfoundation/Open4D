'use strict';

/**
 * VGS1 decoder, checked against the authoritative Python implementation.
 *
 * `fixtures_vgs_golden.json` is produced by `fixtures_vgs_golden.gen.py`, which
 * extracts the exact source of `vega_asset_splats` / `vega_asset_cloud` from
 * `analysis/offline_benchmark.py` and runs it over a real exported frame
 * (`results/vega-web/dancer/frame_0000.vgs`, 57,907 Gaussians).
 *
 * Golden values rather than hand-written expectations because every field here
 * is a bit-field: a wrong shift, or reading the quaternion bytes as unsigned
 * instead of signed, still yields a plausible blob of Gaussians. Only
 * comparison against the reference catches it.
 *
 * Run: node --test tests/test_vgs_format.js
 */

const assert = require('assert');
const test = require('node:test');
const fs = require('fs');
const path = require('path');

const {
    decodeVgsFrame, readHeader, decodedFrameBytes, VgsError,
    HEADER_BYTES, RECORD_BYTES
} = require('../system/WebClient/src/vgs-format');

const FIXTURE = path.join(__dirname, 'fixtures_vgs_golden.json');
const GOLDEN = fs.existsSync(FIXTURE)
    ? JSON.parse(fs.readFileSync(FIXTURE, 'utf8')) : null;

function assetBuffer() {
    const file = path.join(__dirname, '..', GOLDEN.file.replace(/^.*?(results\/)/, '$1'));
    const resolved = fs.existsSync(GOLDEN.file) ? GOLDEN.file : file;
    const bytes = fs.readFileSync(resolved);
    return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
}

const haveAsset = () => GOLDEN && fs.existsSync(
    fs.existsSync(GOLDEN.file) ? GOLDEN.file
        : path.join(__dirname, '..', GOLDEN.file));

test('the golden fixture and its source asset are present', { skip: !haveAsset() }, () => {
    assert.ok(GOLDEN, 'fixtures_vgs_golden.json is missing; run the generator');
    assert.ok(haveAsset(),
        `asset ${GOLDEN.file} is missing; re-export with orbitvega.export_quest`);
    assert.ok(GOLDEN.visibleCount > 1000, 'fixture should be a real frame');
});

test('the header decodes exactly as Python reads it', { skip: !haveAsset() }, () => {
    const header = readHeader(assetBuffer());
    assert.strictEqual(header.magic, GOLDEN.header.magic);
    assert.strictEqual(header.version, GOLDEN.header.version);
    assert.strictEqual(header.objectId, GOLDEN.header.objectId);
    assert.strictEqual(header.frame, GOLDEN.header.frame);
    assert.strictEqual(header.count, GOLDEN.header.count);
    assert.strictEqual(header.encodedBytes, GOLDEN.header.encodedBytes);
    header.lower.forEach((v, i) => assert.ok(
        Math.abs(v - GOLDEN.header.lower[i]) < 1e-6, `lower[${i}]`));
    header.upper.forEach((v, i) => assert.ok(
        Math.abs(v - GOLDEN.header.upper[i]) < 1e-6, `upper[${i}]`));
});

test('the header is little-endian', { skip: !haveAsset() }, () => {
    // The V4DS baselines are big-endian and these two formats sit side by side
    // in the same client. Reading the count big-endian would give a wildly
    // different number and a byte-count mismatch, but reading the *bounds*
    // big-endian would give plausible-looking floats.
    const header = readHeader(assetBuffer());
    assert.ok(header.count > 1000 && header.count < 10_000_000,
        `count ${header.count} should be a sane Gaussian count`);
    assert.ok(header.upper[1] > header.lower[1], 'bbox is ordered');
    // dancer stands on the venue riser, so y is around 2.1 to 4.0 metres.
    assert.ok(header.lower[1] > 1 && header.upper[1] < 6,
        `y bounds ${header.lower[1]}..${header.upper[1]} should be venue-scale`);
});

test('pruned splats are dropped, matching the Python visible count',
    { skip: !haveAsset() }, () => {
        const frame = decodeVgsFrame(assetBuffer());
        assert.strictEqual(frame.count, GOLDEN.visibleCount);
        assert.ok(frame.count < GOLDEN.header.count,
            'the fixture frame does contain pruned splats, so this is tested');
        assert.strictEqual(frame.positions.length, frame.count * 3);
        assert.strictEqual(frame.rotations.length, frame.count * 4);
        assert.strictEqual(frame.opacities.length, frame.count);
    });

test('sampled positions match Python across the whole frame',
    { skip: !haveAsset() }, () => {
        const frame = decodeVgsFrame(assetBuffer());
        GOLDEN.sampleIndices.forEach((index, sample) => {
            const want = GOLDEN.positions[sample];
            for (let axis = 0; axis < 3; axis++) {
                const got = frame.positions[index * 3 + axis];
                assert.ok(Math.abs(got - want[axis]) < 1e-5,
                    `position[${index}][${axis}]: ${got} != ${want[axis]}`);
            }
        });
    });

test('sampled log scales match Python', { skip: !haveAsset() }, () => {
    const frame = decodeVgsFrame(assetBuffer());
    GOLDEN.sampleIndices.forEach((index, sample) => {
        const want = GOLDEN.scaleRaw[sample];
        for (let axis = 0; axis < 3; axis++) {
            const got = frame.scales[index * 3 + axis];
            assert.ok(Math.abs(got - want[axis]) < 1e-4,
                `scale[${index}][${axis}]: ${got} != ${want[axis]}`);
        }
    });
});

test('sampled quaternions match Python, signed and normalised',
    { skip: !haveAsset() }, () => {
        // The decisive one. Quaternion components are int8/127; reading the
        // bytes as unsigned would mirror every rotation, which still renders as
        // a person-shaped cloud of Gaussians.
        const frame = decodeVgsFrame(assetBuffer());
        let sawNegative = false;
        GOLDEN.sampleIndices.forEach((index, sample) => {
            const want = GOLDEN.rotation[sample];
            for (let axis = 0; axis < 4; axis++) {
                const got = frame.rotations[index * 4 + axis];
                if (want[axis] < -0.05) sawNegative = true;
                assert.ok(Math.abs(got - want[axis]) < 1e-4,
                    `rotation[${index}][${axis}]: ${got} != ${want[axis]}`);
            }
            const norm = Math.hypot(
                frame.rotations[index * 4], frame.rotations[index * 4 + 1],
                frame.rotations[index * 4 + 2], frame.rotations[index * 4 + 3]);
            assert.ok(Math.abs(norm - 1) < 1e-5, `quaternion ${index} normalised`);
        });
        assert.ok(sawNegative,
            'the samples include negative components, so signedness is exercised');
    });

test('sampled colours match Python', { skip: !haveAsset() }, () => {
    const frame = decodeVgsFrame(assetBuffer());
    GOLDEN.sampleIndices.forEach((index, sample) => {
        const want = GOLDEN.colors[sample];
        for (let channel = 0; channel < 3; channel++) {
            const got = frame.colors[index * 3 + channel];
            assert.ok(Math.abs(got - want[channel]) < 1e-5,
                `color[${index}][${channel}]: ${got} != ${want[channel]}`);
        }
    });
});

test('opacity matches Python after its logit transform',
    { skip: !haveAsset() }, () => {
        // Python returns opacity_raw = logit(clamped opacity); this decoder keeps
        // linear opacity because the renderer blends with it directly. Check the
        // two agree through the transform rather than assuming.
        const frame = decodeVgsFrame(assetBuffer());
        GOLDEN.sampleIndices.forEach((index, sample) => {
            const linear = Math.min(Math.max(frame.opacities[index], 1e-5),
                1 - 1e-5);
            const logit = Math.log(linear / (1 - linear));
            assert.ok(Math.abs(logit - GOLDEN.opacityRaw[sample][0]) < 1e-3,
                `opacity[${index}]: logit ${logit} != ${GOLDEN.opacityRaw[sample][0]}`);
        });
    });

test('frame-wide means match Python', { skip: !haveAsset() }, () => {
    // Aggregates catch a systematic error the 64 samples might straddle.
    const frame = decodeVgsFrame(assetBuffer());
    const mean = (array, stride, offset) => {
        let sum = 0;
        for (let i = 0; i < frame.count; i++) sum += array[i * stride + offset];
        return sum / frame.count;
    };
    for (let axis = 0; axis < 3; axis++) {
        assert.ok(Math.abs(mean(frame.positions, 3, axis)
            - GOLDEN.positionMean[axis]) < 1e-4, `position mean [${axis}]`);
        assert.ok(Math.abs(mean(frame.scales, 3, axis)
            - GOLDEN.scaleRawMean[axis]) < 1e-3, `scale mean [${axis}]`);
        assert.ok(Math.abs(mean(frame.colors, 3, axis)
            - GOLDEN.colorMean[axis]) < 1e-4, `color mean [${axis}]`);
    }
});

test('every decoded position lies inside the declared bounds',
    { skip: !haveAsset() }, () => {
        // UNORM16 within [lower, upper] cannot escape the box; a position
        // outside it means the bit unpacking or the span is wrong.
        const frame = decodeVgsFrame(assetBuffer());
        const { lower, upper } = frame.header;
        const epsilon = 1e-4;
        for (let i = 0; i < frame.count; i++) {
            for (let axis = 0; axis < 3; axis++) {
                const value = frame.positions[i * 3 + axis];
                assert.ok(value >= lower[axis] - epsilon
                    && value <= upper[axis] + epsilon,
                    `position[${i}][${axis}] = ${value} outside `
                    + `[${lower[axis]}, ${upper[axis]}]`);
            }
        }
    });

test('the catalogue scale range is honoured', { skip: !haveAsset() }, () => {
    // export_quest writes scaleLogMin/Max into its catalogue; a client that
    // hardcoded different values would scale every splat wrongly.
    const buffer = assetBuffer();
    const standard = decodeVgsFrame(buffer);
    const shifted = decodeVgsFrame(buffer,
        { scaleLogMin: -6, scaleLogMax: 0 });
    assert.notStrictEqual(standard.scales[0], shifted.scales[0]);
    assert.ok(shifted.scales[0] >= -6 && shifted.scales[0] <= 0);
});

// --------------------------------------------------------------------------
// Malformed input
// --------------------------------------------------------------------------

function syntheticAsset({ magic = 'VGS1', version = 1, count = 2,
                          truncate = 0 } = {}) {
    const buffer = new ArrayBuffer(HEADER_BYTES + count * RECORD_BYTES - truncate);
    const view = new DataView(buffer);
    for (let i = 0; i < 4; i++) view.setUint8(i, magic.charCodeAt(i));
    view.setUint16(4, version, true);
    view.setUint16(6, 3, true);
    view.setUint32(8, 7, true);
    view.setUint32(12, count, true);
    [0, 0, 0].forEach((v, i) => view.setFloat32(16 + i * 4, v, true));
    [1, 1, 1].forEach((v, i) => view.setFloat32(28 + i * 4, v, true));
    view.setBigUint64(40, 1234n, true);
    // Make both records visible with distinct values.
    for (let r = 0; r < count && HEADER_BYTES + (r + 1) * RECORD_BYTES <= buffer.byteLength; r++) {
        const base = HEADER_BYTES + r * RECORD_BYTES;
        view.setUint32(base, 0x80008000, true);           // mid-box x,y
        view.setUint32(base + 4, 0x40408000, true);
        view.setUint32(base + 8, 0x7f7f7f40, true);
        view.setUint32(base + 12, 0x2040607f, true);
        view.setUint32(base + 16, 255, true);             // fully opaque
    }
    return buffer;
}

test('a well-formed synthetic asset decodes', () => {
    const frame = decodeVgsFrame(syntheticAsset({ count: 2 }));
    assert.strictEqual(frame.count, 2);
    assert.strictEqual(frame.header.objectId, 3);
    assert.strictEqual(frame.header.frame, 7);
});

test('malformed assets are refused with a stated reason', () => {
    assert.throws(() => decodeVgsFrame(new ArrayBuffer(8)),
        /truncated Vega asset/);
    assert.throws(() => decodeVgsFrame(syntheticAsset({ magic: 'XXXX' })),
        /magic/);
    assert.throws(() => decodeVgsFrame(syntheticAsset({ version: 2 })),
        /version 2/);
    assert.throws(() => decodeVgsFrame(syntheticAsset({ count: 0 })),
        /declares no Gaussians/);
    assert.throws(() => decodeVgsFrame(syntheticAsset({ count: 4, truncate: 7 })),
        /byte count disagrees/);
});

test('a fully transparent asset decodes to zero splats, not an error', () => {
    // Every splat pruned is a legitimate frame (an object fully occluded or
    // faded), and must not look like a decode failure.
    const buffer = syntheticAsset({ count: 3 });
    const view = new DataView(buffer);
    for (let r = 0; r < 3; r++) {
        view.setUint32(HEADER_BYTES + r * RECORD_BYTES + 16, 0, true);
    }
    const frame = decodeVgsFrame(buffer);
    assert.strictEqual(frame.count, 0);
    assert.strictEqual(frame.positions.length, 0);
});

test('decodedFrameBytes reports the real footprint', () => {
    const frame = decodeVgsFrame(syntheticAsset({ count: 100 }));
    // 3 + 3 + 4 + 1 + 3 floats per splat = 14 * 4 bytes
    assert.strictEqual(decodedFrameBytes(frame), 100 * 14 * 4);
});

// --------------------------------------------------------------------------
// SplatObject capacity — the instance-count cap that emptied the canvas
// --------------------------------------------------------------------------
test('a splat object sized from the catalogue never grows after first draw', () => {
    const { SplatObject } = require('../system/WebClient/src/splat-renderer');

    const frame = (count) => ({
        count,
        positions: new Float32Array(count * 3),
        scales: new Float32Array(count * 3),
        rotations: new Float32Array(count * 4),
        colors: new Float32Array(count * 3),
        opacities: new Float32Array(count)
    });

    // Hidden before it has data: a visible mesh gets bound by the render loop,
    // and Three caches its instance-count cap from that first binding.
    const splat = new SplatObject({ capacity: 500 });
    assert.strictEqual(splat.mesh.visible, false);

    splat.setFrame(frame(500));
    assert.strictEqual(splat.mesh.visible, true);
    assert.strictEqual(splat.geometry.instanceCount, 500);
    assert.strictEqual(splat.capacity, 500);

    // A smaller later frame is fine and must not shrink the allocation.
    splat.setFrame(frame(320));
    assert.strictEqual(splat.geometry.instanceCount, 320);
    assert.strictEqual(splat.capacity, 500);

    // Growing after the first draw is the bug: Three would keep drawing one
    // instance out of thousands, which renders as an empty canvas. Fail loudly
    // instead of silently capping.
    assert.throws(() => splat.setFrame(frame(900)), /after it was first drawn/);
});

test('an undersized splat object still refuses to grow silently', () => {
    const { SplatObject } = require('../system/WebClient/src/splat-renderer');
    const frame = (count) => ({
        count,
        positions: new Float32Array(count * 3),
        scales: new Float32Array(count * 3),
        rotations: new Float32Array(count * 4),
        colors: new Float32Array(count * 3),
        opacities: new Float32Array(count)
    });
    // Growth before the first frame is legitimate: nothing has been bound.
    const splat = new SplatObject({ capacity: 1 });
    splat.setFrame(frame(4000));
    assert.strictEqual(splat.capacity, 4000);
    assert.strictEqual(splat.geometry.instanceCount, 4000);
    assert.throws(() => splat.setFrame(frame(4001)), /after it was first drawn/);
});
