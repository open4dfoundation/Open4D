'use strict';

/**
 * Tests for the browser client's camera-pose conversion.
 *
 * These matter more than their size suggests. The pose the client POSTs is
 * written to disk verbatim and read by `o3d.io.read_pinhole_camera_parameters`;
 * if the schema, the axis convention or the units are wrong, the ladder cannot
 * use the pose and `normalize_weights` returns EQUAL weights. Nothing crashes —
 * the system just silently stops being viewpoint-aware, which is the whole
 * contribution being measured. So the reference values below are taken from a
 * real captured viewpoint file rather than derived from the code under test.
 *
 * Run: node --test tests/test_camera_pose.js
 */

const assert = require('assert');
const test = require('node:test');
const fs = require('fs');
const path = require('path');

const THREE = require(path.join(
    __dirname, '..', 'system/WebClient/node_modules/three'));
const {
    toOpen3DCameraParameters, fromThreeCamera, focalLengthPx, principalPoint,
    projectToCameraSpace
} = require('../system/WebClient/src/camera-pose');

const CAPTURED = path.join(
    __dirname, '..', 'system/Client/viewpoints/view_00.json');

function identityView() {
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
}

// --------------------------------------------------------------------------
// Schema — it has to be byte-compatible with what Open3D reads.
// --------------------------------------------------------------------------

test('the output schema matches a real captured viewpoint file', () => {
    const captured = JSON.parse(fs.readFileSync(CAPTURED, 'utf8'));
    const produced = toOpen3DCameraParameters({
        viewMatrix: identityView(), fovDegrees: 60, width: 1920, height: 1920
    });

    assert.deepStrictEqual(Object.keys(produced).sort(),
        Object.keys(captured).sort(), 'same top-level keys');
    assert.deepStrictEqual(Object.keys(produced.intrinsic).sort(),
        Object.keys(captured.intrinsic).sort(), 'same intrinsic keys');
    assert.strictEqual(produced.class_name, 'PinholeCameraParameters');
    assert.strictEqual(produced.extrinsic.length, 16);
    assert.strictEqual(produced.intrinsic.intrinsic_matrix.length, 9);
    assert.strictEqual(produced.version_major, captured.version_major);
    assert.strictEqual(produced.version_minor, captured.version_minor);
});

test('the output survives a JSON round trip with no loss of shape', () => {
    const produced = toOpen3DCameraParameters({
        viewMatrix: identityView(), fovDegrees: 60, width: 1280, height: 720
    });
    assert.deepStrictEqual(JSON.parse(JSON.stringify(produced)), produced);
});

// --------------------------------------------------------------------------
// Intrinsics — checked against the captured file's actual numbers.
// --------------------------------------------------------------------------

test('focal length reproduces the captured corpus value exactly', () => {
    // view_00.json: 1920x1920, fx = fy = 1662.7687752661222. That is a 60
    // degree vertical FOV, so the conversion must land on the same number.
    const captured = JSON.parse(fs.readFileSync(CAPTURED, 'utf8'));
    const capturedFocal = captured.intrinsic.intrinsic_matrix[0];
    assert.ok(Math.abs(focalLengthPx(60, 1920) - capturedFocal) < 1e-9,
        `expected ${capturedFocal}, got ${focalLengthPx(60, 1920)}`);
});

test('the principal point sits at the pixel-grid centre, as Open3D expects', () => {
    const captured = JSON.parse(fs.readFileSync(CAPTURED, 'utf8'));
    assert.strictEqual(captured.intrinsic.intrinsic_matrix[6], 959.5,
        'sanity: the corpus uses (W-1)/2');
    assert.strictEqual(principalPoint(1920), 959.5);
    assert.strictEqual(principalPoint(1280), 639.5);
});

test('the intrinsic matrix is laid out column-major', () => {
    const { intrinsic } = toOpen3DCameraParameters({
        viewMatrix: identityView(), fovDegrees: 60, width: 1280, height: 720
    });
    const K = intrinsic.intrinsic_matrix;
    const fy = focalLengthPx(60, 720);
    // [fx, 0, 0, 0, fy, 0, cx, cy, 1]
    assert.ok(Math.abs(K[0] - fy) < 1e-9, 'fx');
    assert.strictEqual(K[1], 0);
    assert.strictEqual(K[2], 0);
    assert.strictEqual(K[3], 0);
    assert.ok(Math.abs(K[4] - fy) < 1e-9, 'fy');
    assert.strictEqual(K[5], 0);
    assert.strictEqual(K[6], 639.5, 'cx');
    assert.strictEqual(K[7], 359.5, 'cy');
    assert.strictEqual(K[8], 1);
});

test('square pixels: fx equals fy even on a non-square viewport', () => {
    // Three.js takes a VERTICAL fov and derives the horizontal extent from the
    // aspect ratio, so a 16:9 canvas still has square pixels.
    const { intrinsic } = toOpen3DCameraParameters({
        viewMatrix: identityView(), fovDegrees: 60, width: 1920, height: 1080
    });
    assert.strictEqual(intrinsic.intrinsic_matrix[0], intrinsic.intrinsic_matrix[4]);
    assert.strictEqual(intrinsic.width, 1920);
    assert.strictEqual(intrinsic.height, 1080);
});

// --------------------------------------------------------------------------
// Axis convention — the sign error that would be invisible in production.
// --------------------------------------------------------------------------

test('the extrinsic negates the Y and Z rows, matching the captured corpus', () => {
    const produced = toOpen3DCameraParameters({
        viewMatrix: identityView(), fovDegrees: 60, width: 1920, height: 1920
    });
    // diag(1, -1, -1, 1)
    assert.strictEqual(produced.extrinsic[0], 1);
    assert.strictEqual(produced.extrinsic[5], -1);
    assert.strictEqual(produced.extrinsic[10], -1);
    assert.strictEqual(produced.extrinsic[15], 1);

    const captured = JSON.parse(fs.readFileSync(CAPTURED, 'utf8'));
    // The corpus diagonal is roughly (+1, -1, -1); assert the SIGNS agree,
    // which is the property that matters.
    assert.ok(captured.extrinsic[0] > 0);
    assert.ok(captured.extrinsic[5] < 0);
    assert.ok(captured.extrinsic[10] < 0);
});

test('a point the camera looks at lands at positive depth', () => {
    // The decisive test. Open3D is OpenCV-style (+Z forward), so anything in
    // front of the camera must have positive z in camera space. A flipped sign
    // would put the whole scene behind the camera, every raycast would miss,
    // and the ladder would fall back to uniform weights without any error.
    const camera = new THREE.PerspectiveCamera(60, 16 / 9, 0.05, 500);
    camera.position.set(0, 1.6, 4);
    camera.lookAt(0, 1.0, 0);

    const parameters = fromThreeCamera(camera, { width: 1920, height: 1080 });
    const [, , depth] = projectToCameraSpace(parameters, [0, 1.0, 0]);
    assert.ok(depth > 0, `the look-at target must be in front, got z=${depth}`);

    // And something behind the camera must be negative.
    const [, , behind] = projectToCameraSpace(parameters, [0, 1.6, 10]);
    assert.ok(behind < 0, `a point behind the camera must be negative, got ${behind}`);
});

test('the look-at target projects to the principal point', () => {
    // If the rotation were transposed rather than inverted, depth could still
    // come out positive while the image was wrong. Checking that the target
    // lands dead centre pins the rotation itself.
    const camera = new THREE.PerspectiveCamera(60, 1, 0.05, 500);
    camera.position.set(2, 1.5, 3);
    camera.lookAt(-1, 0.5, 0.5);

    const width = 1024;
    const height = 1024;
    const parameters = fromThreeCamera(camera, { width, height });
    const [x, y, z] = projectToCameraSpace(parameters, [-1, 0.5, 0.5]);
    const K = parameters.intrinsic.intrinsic_matrix;
    const u = (K[0] * x) / z + K[6];
    const v = (K[4] * y) / z + K[7];

    assert.ok(Math.abs(u - principalPoint(width)) < 1e-6,
        `u should be the principal point, got ${u}`);
    assert.ok(Math.abs(v - principalPoint(height)) < 1e-6,
        `v should be the principal point, got ${v}`);
});

test('a point to the camera right projects right of centre', () => {
    // Pins the handedness: a mirrored X would be geometrically consistent but
    // would weight the wrong objects.
    const camera = new THREE.PerspectiveCamera(60, 1, 0.05, 500);
    camera.position.set(0, 0, 5);
    camera.lookAt(0, 0, 0);

    const parameters = fromThreeCamera(camera, { width: 1024, height: 1024 });
    const [x, y, z] = projectToCameraSpace(parameters, [1, 0, 0]);
    const K = parameters.intrinsic.intrinsic_matrix;
    const u = (K[0] * x) / z + K[6];
    assert.ok(u > principalPoint(1024),
        'world +X is to the right of a camera looking down -Z');
    void y;
});

// --------------------------------------------------------------------------
// Units — the failure config.py explicitly warns about.
// --------------------------------------------------------------------------

test('translation is scaled into the server world units', () => {
    // config.VIEW_RAYCAST_UNITS_PER_METER = 1000: "Baked OBJ/Draco coordinates
    // are metres, while Open3D camera extrinsics ... are millimetres.
    // Raycasting must put the meshes in the camera coordinate system;
    // otherwise every ray misses and normalize_weights necessarily returns
    // equal values."
    const camera = new THREE.PerspectiveCamera(60, 1, 0.05, 500);
    camera.position.set(0, 1.6, 4);
    camera.lookAt(0, 1.6, 0);

    const parameters = fromThreeCamera(camera, { width: 512, height: 512 });
    const translation = parameters.extrinsic.slice(12, 15);
    const magnitude = Math.hypot(...translation);
    assert.ok(magnitude > 1000,
        `a metres-scale standoff must be thousands of units, got ${magnitude.toFixed(1)}`);

    // |t| = |-R*C| = |C|, i.e. the distance from the WORLD ORIGIN, not from the
    // look-at target. For a camera at (0, 1.6, 4) m that is 4.308 m.
    const expected = Math.hypot(0, 1.6, 4) * 1000;
    assert.ok(Math.abs(magnitude - expected) < 1e-6,
        `expected ${expected.toFixed(1)} mm, got ${magnitude}`);
});

test('translation magnitude is comparable to the captured corpus', () => {
    // A run whose poses are three orders of magnitude smaller than the corpus
    // is the signature of the metres/millimetres mistake.
    const captured = JSON.parse(fs.readFileSync(CAPTURED, 'utf8'));
    const capturedMagnitude = Math.hypot(...captured.extrinsic.slice(12, 15));
    assert.ok(capturedMagnitude > 1000, 'sanity: the corpus is in millimetres');

    const camera = new THREE.PerspectiveCamera(60, 1, 0.05, 500);
    camera.position.set(1.7, 0.4, 6.5);   // roughly the captured standoff
    camera.lookAt(0, 0.4, 0);
    const parameters = fromThreeCamera(camera, { width: 1920, height: 1920 });
    const magnitude = Math.hypot(...parameters.extrinsic.slice(12, 15));

    const ratio = magnitude / capturedMagnitude;
    assert.ok(ratio > 0.5 && ratio < 2,
        `same order of magnitude as the corpus; ratio ${ratio.toFixed(3)}`);
});

test('unitsPerMeter is configurable for a metre-world server', () => {
    const camera = new THREE.PerspectiveCamera(60, 1, 0.05, 500);
    camera.position.set(0, 0, 3);
    camera.lookAt(0, 0, 0);
    const metres = fromThreeCamera(camera,
        { width: 512, height: 512, unitsPerMeter: 1 });
    assert.ok(Math.abs(Math.hypot(...metres.extrinsic.slice(12, 15)) - 3) < 1e-6);
});

test('rotation is NOT scaled by the unit conversion', () => {
    // Only the translation column changes units; scaling the rotation too would
    // produce a matrix that is no longer a rigid transform.
    const camera = new THREE.PerspectiveCamera(60, 1, 0.05, 500);
    camera.position.set(2, 3, 4);
    camera.lookAt(0, 0, 0);
    const parameters = fromThreeCamera(camera, { width: 512, height: 512 });
    const e = parameters.extrinsic;
    for (const column of [0, 1, 2]) {
        const length = Math.hypot(e[column * 4], e[column * 4 + 1], e[column * 4 + 2]);
        assert.ok(Math.abs(length - 1) < 1e-6,
            `rotation column ${column} must stay unit length, got ${length}`);
    }
    assert.strictEqual(e[3], 0);
    assert.strictEqual(e[7], 0);
    assert.strictEqual(e[11], 0);
    assert.strictEqual(e[15], 1);
});

// --------------------------------------------------------------------------
// Input validation
// --------------------------------------------------------------------------

test('bad inputs are rejected rather than silently producing a broken pose', () => {
    const good = {
        viewMatrix: identityView(), fovDegrees: 60, width: 640, height: 480
    };
    assert.throws(() => toOpen3DCameraParameters({ ...good, viewMatrix: 'nope' }),
        /must be an array/);
    assert.throws(() => toOpen3DCameraParameters({ ...good, viewMatrix: [1, 2, 3] }),
        /16 elements/);
    assert.throws(() => toOpen3DCameraParameters({ ...good, width: 0 }),
        /positive/);
    assert.throws(() => toOpen3DCameraParameters({ ...good, fovDegrees: 0 }),
        /out of range/);
    assert.throws(() => toOpen3DCameraParameters({ ...good, fovDegrees: 180 }),
        /out of range/);
});

test('a Float32Array view matrix is accepted, as Three.js supplies', () => {
    assert.doesNotThrow(() => toOpen3DCameraParameters({
        viewMatrix: new Float32Array(identityView()),
        fovDegrees: 60, width: 640, height: 480
    }));
});
