'use strict';

const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const { normalizeName, placements, stagePose, TrajectoryPlayer, TrajectoryRecorder,
        readPose, applyPose } = require('../system/WebClient/src/study/camera');
const { StudyDriver } = require('../system/WebClient/src/study/driver');

const LAYOUT = JSON.parse(fs.readFileSync(path.join(__dirname, '../scene_layout.json'), 'utf8'));

/** Just enough of three.js's camera and OrbitControls for poses to round-trip. */
class Vec {
    constructor(x = 0, y = 0, z = 0) { this.x = x; this.y = y; this.z = z; }
    set(x, y, z) { this.x = x; this.y = y; this.z = z; return this; }
    toArray() { return [this.x, this.y, this.z]; }
}
function fakeCamera() {
    return {
        camera: { position: new Vec(0, 1.6, 4), up: new Vec(0, 1, 0), fov: 60,
                  updateProjectionMatrix() { this.projections = (this.projections || 0) + 1; } },
        controls: { target: new Vec(0, 1, 0) }
    };
}

function angleBetween(a, b) {
    const dot = a.reduce((sum, v, i) => sum + v * b[i], 0);
    return Math.acos(Math.min(1, dot / (Math.hypot(...a) * Math.hypot(...b))));
}

test('layout and corpus names match despite their punctuation', () => {
    assert.strictEqual(normalizeName('UMA_0'), normalizeName('UMA0'));
    assert.deepStrictEqual(placements(LAYOUT, ['UMA0', 'uma_3']).map(p => p.name), ['UMA_0', 'UMA_3']);
});

test('with no subset, the start frames what stands on the stage', () => {
    const names = placements(LAYOUT).map(p => p.name).sort();
    assert.deepStrictEqual(names, ['UMA_0', 'UMA_1', 'UMA_2', 'UMA_3', 'UMA_4']);
    for (const placed of placements(LAYOUT)) assert.strictEqual(placed.y, 1.55);
});

test('the start pose sees every object, from in front of the stage', () => {
    const aspect = 16 / 9;
    const pose = stagePose(LAYOUT, [], { fovDegrees: 60, aspect });
    assert.ok(pose.position[2] > pose.target[2], 'the eye is on the +Z side the stage faces');
    assert.ok(pose.position[1] > pose.target[1], 'and above, looking down at the deck');
    // In the frustum, not merely a cone: horizontal and vertical checked apart.
    const forward = normalizeV(pose.target.map((v, i) => v - pose.position[i]));
    const right = normalizeV(crossV(forward, [0, 1, 0]));
    const up = crossV(right, forward);
    const tanV = Math.tan((60 * Math.PI) / 360);
    const tanH = aspect * tanV;
    for (const placed of placements(LAYOUT)) {
        for (const y of [placed.y, placed.y + 1.85]) {
            const d = [placed.x - pose.position[0], y - pose.position[1], placed.z - pose.position[2]];
            const depth = dotV(d, forward);
            assert.ok(Math.abs(dotV(d, right)) <= depth * tanH, `${placed.name} off the side`);
            assert.ok(Math.abs(dotV(d, up)) <= depth * tanV, `${placed.name} at y=${y} off the top or bottom`);
        }
    }
});

test('the box fit stands closer than a bounding sphere would', () => {
    const pose = stagePose(LAYOUT, [], { fovDegrees: 60, aspect: 16 / 9 });
    const distance = Math.hypot(...pose.position.map((v, i) => v - pose.target[i]));
    // The stage objects span about ten metres; a sphere fitted to the vertical
    // FOV would stand the camera about 11 m back. The box fit must beat it.
    assert.ok(distance < 9, `camera ${distance.toFixed(2)} m from the target`);
});

function dotV(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
function crossV(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
function normalizeV(v) { const l = Math.hypot(...v); return v.map(c => c / l); }

test('the start pose does not depend on what has downloaded', () => {
    // It is a function of the layout alone, so every method starts identically.
    assert.deepStrictEqual(stagePose(LAYOUT, ['UMA0', 'UMA1']), stagePose(LAYOUT, ['UMA1', 'UMA0']));
});

test('an object missing from the layout is an error, not a blank view', () => {
    assert.throws(() => stagePose(LAYOUT, ['nobody']), /not in the scene layout|none of/);
});

test('a path is interpolated between samples and held at its ends', () => {
    const player = new TrajectoryPlayer({ samples: [
        { t: 0, position: [0, 0, 10], target: [0, 0, 0], up: [0, 1, 0], fovDegrees: 60 },
        { t: 2, position: [10, 0, 10], target: [0, 2, 0], up: [0, 1, 0], fovDegrees: 40 }
    ] });
    assert.deepStrictEqual(player.poseAt(1).position, [5, 0, 10]);
    assert.deepStrictEqual(player.poseAt(1).target, [0, 1, 0]);
    assert.strictEqual(player.poseAt(1).fovDegrees, 50);
    assert.deepStrictEqual(player.poseAt(-1).position, [0, 0, 10]);
    assert.deepStrictEqual(player.poseAt(99).position, [10, 0, 10]);
});

test('a pose round-trips through the camera exactly', () => {
    const { camera, controls } = fakeCamera();
    const pose = { position: [1, 2, 3], target: [4, 5, 6], up: [0, 1, 0], fovDegrees: 45 };
    applyPose(camera, controls, pose);
    assert.deepStrictEqual(readPose(camera, controls), pose);
});

test('the recorder keeps at most one sample per interval', () => {
    const recorder = new TrajectoryRecorder({ interval: 0.1 });
    const pose = { position: [0, 0, 0], target: [0, 0, -1], up: [0, 1, 0], fovDegrees: 60 };
    for (let t = 0; t < 1; t += 0.01) recorder.sample(t, pose);
    assert.ok(recorder.samples.length >= 10 && recorder.samples.length <= 12,
        `${recorder.samples.length} samples`);
});

// ------------------------------------------------------------- driver ---

function run(driver, frames, { presentedAt = () => 1, objects = 5, stepMs = 1000 / 30, from = 0 } = {}) {
    const { camera, controls } = fakeCamera();
    let now = from;
    for (let frame = 0; frame < frames && !driver.ended; frame++) {
        driver.apply(camera, controls, {
            now, advanced: true, presented: presentedAt(frame), objects
        });
        now += stepMs;
    }
    return { camera, controls };
}

const START = { position: [0, 3, 12], target: [0, 2.5, 3], up: [0, 1, 0], fovDegrees: 60,
                objects: ['UMA_0'] };

test('the clock starts at the first visible content, not at page load', () => {
    const driver = new StudyDriver({ mode: 'record', startPose: START, durationSeconds: 2,
                                     createdAt: 0 });
    // Nothing visible for the first 30 frames (1 s of startup).
    run(driver, 200, { presentedAt: frame => (frame < 30 ? 0 : 1) });
    const summary = driver.summary();
    assert.strictEqual(summary.startupDelayMs, 1000);
    assert.ok(summary.completed);
    assert.ok(Math.abs(summary.playbackSeconds - 2) < 0.05);
});

test('replay drives the camera along the path at playback time', () => {
    const path = { samples: [
        { t: 0, position: [0, 3, 12], target: [0, 2.5, 3], up: [0, 1, 0], fovDegrees: 60 },
        { t: 2, position: [6, 3, 12], target: [0, 2.5, 3], up: [0, 1, 0], fovDegrees: 60 }
    ] };
    const driver = new StudyDriver({ mode: 'replay', startPose: START, trajectory: path,
                                     durationSeconds: 1 });
    const { camera } = run(driver, 31);
    // One second in, halfway along a two-second path.
    assert.ok(Math.abs(camera.position.x - 3) < 0.15, `x=${camera.position.x}`);
});

test('a slot with no new content is a freeze, and runs are measured', () => {
    const driver = new StudyDriver({ mode: 'record', startPose: START, durationSeconds: 2 });
    // Frames 10-19 and 40-44 present nothing.
    const frozen = frame => (frame >= 10 && frame < 20) || (frame >= 40 && frame < 45);
    run(driver, 200, { presentedAt: frame => (frozen(frame) ? 0 : 5) });
    const summary = driver.summary();
    assert.strictEqual(summary.repeatedSlots, 15);
    assert.strictEqual(summary.freezes, 2);
    assert.strictEqual(summary.longestFreezeMs, Math.round(10 * 1000 / 30));
    assert.ok(summary.freezeRatio > 0.2 && summary.freezeRatio < 0.3);
});

test('all-object advance is stricter than any-object advance', () => {
    const driver = new StudyDriver({ mode: 'record', startPose: START, durationSeconds: 1 });
    run(driver, 60, { presentedAt: frame => (frame % 2 ? 5 : 3), objects: 5 });
    const summary = driver.summary();
    assert.ok(summary.allObjectAdvanceFps < summary.contentAdvanceFps);
    assert.ok(Math.abs(summary.allObjectAdvanceFps * 2 - summary.contentAdvanceFps) < 2);
});

test('recording produces a path the replay can use', () => {
    const recording = new StudyDriver({ mode: 'record', startPose: START, durationSeconds: 1 });
    run(recording, 60);
    const path = recording.trajectory();
    assert.strictEqual(path.coordinateSpace, 'three-world-metres-y-up');
    assert.ok(path.samples.length >= 29);
    assert.deepStrictEqual(path.samples[0].position, START.position);
    assert.doesNotThrow(() => new StudyDriver({ mode: 'replay', startPose: START,
                                                trajectory: path, durationSeconds: 1 }));
});

test('onEnded fires once, with the summary', () => {
    let calls = 0;
    const driver = new StudyDriver({ mode: 'record', startPose: START, durationSeconds: 0.5,
                                     onEnded: () => { calls += 1; } });
    run(driver, 100);
    run(driver, 100);
    assert.strictEqual(calls, 1);
});

test('a drag during startup does not move where the recording begins', () => {
    const driver = new StudyDriver({ mode: 'record', startPose: START, durationSeconds: 1 });
    const { camera, controls } = fakeCamera();
    assert.strictEqual(driver.interactive, false, 'no camera input before content');
    let now = 0;
    for (let frame = 0; frame < 60 && !driver.ended; frame++) {
        // The participant drags on every frame, from the start.
        camera.position.set(camera.position.x - 0.5, camera.position.y, camera.position.z);
        driver.apply(camera, controls, { now, advanced: true, presented: frame < 20 ? 0 : 1, objects: 1 });
        now += 1000 / 30;
    }
    assert.deepStrictEqual(driver.trajectory().samples[0].position, START.position);
    assert.strictEqual(driver.interactive, true, 'input is allowed once content is visible');
});

test('recording holds the start until the whole stage is visible', () => {
    const driver = new StudyDriver({ mode: 'record', startPose: START, durationSeconds: 1,
                                     waitForObjects: 5, createdAt: 0 });
    const { camera, controls } = fakeCamera();
    let now = 0;
    // One performer at first, then all five from frame 90 (3 s in).
    for (let frame = 0; frame < 200 && !driver.ended; frame++) {
        const visible = frame < 90 ? 1 : 5;
        camera.position.set(camera.position.x + 0.3, 1, 1);        // the participant tugs at it
        driver.apply(camera, controls, { now, advanced: true, presented: visible,
                                         objects: visible, visible });
        if (frame === 60) {
            assert.strictEqual(driver.firstContentAt, null, 'not recording on a partial stage');
            assert.strictEqual(driver.interactive, false, 'and input stays locked');
        }
        now += 1000 / 30;
    }
    assert.strictEqual(driver.summary().startupDelayMs, 3000);
    assert.strictEqual(driver.summary().recordedWithPartialStage, false);
    assert.deepStrictEqual(driver.trajectory().samples[0].position, START.position);
});

test('the wait gives up at its cap and says so, rather than hanging the session', () => {
    const driver = new StudyDriver({ mode: 'record', startPose: START, durationSeconds: 1,
                                     waitForObjects: 5, waitCapMs: 2000 });
    run(driver, 200, { presentedAt: () => 2, objects: 2 });
    assert.ok(driver.ended);
    assert.strictEqual(driver.summary().recordedWithPartialStage, true);
});

test('a trial does not wait for the stage: its startup is what is measured', () => {
    const path = { samples: [
        { t: 0, position: [0, 3, 12], target: [0, 2.5, 3], up: [0, 1, 0], fovDegrees: 60 },
        { t: 1, position: [0, 3, 12], target: [0, 2.5, 3], up: [0, 1, 0], fovDegrees: 60 }] };
    const driver = new StudyDriver({ mode: 'replay', startPose: START, trajectory: path,
                                     durationSeconds: 0.5, waitForObjects: 5, createdAt: 0 });
    run(driver, 100, { presentedAt: () => 1, objects: 1 });
    assert.ok(driver.ended);
    assert.ok(!('recordedWithPartialStage' in driver.summary()));
});
