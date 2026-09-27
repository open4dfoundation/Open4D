'use strict';

/**
 * Camera poses for the browser study: the fixed start, and the shared path.
 *
 * Every method page draws with a three.js PerspectiveCamera and OrbitControls in
 * the same world frame -- metres, Y-up, right-handed, the frame
 * scene_layout.json defines for the composed ORBIT scene. So a pose is stored
 * as that camera's own state (eye, target, up, vertical FOV) and every page
 * applies it verbatim. No conversion between methods is needed, which is also
 * why none is attempted here: a per-method transform would be a place for the
 * "same trajectory" to silently stop being the same.
 *
 * Pure: no DOM, no three.js import. `readPose`/`applyPose` duck-type the
 * camera and controls, so the Node tests exercise the same code the page runs.
 */

const HUMAN_HEIGHT_M = 1.85;
const PLACEMENT_PAD_M = 0.6;
const DEFAULT_FOV_DEG = 60;
/** How far above the target the start eye sits, looking down at the stage. */
const START_ELEVATION_DEG = 12;

/** "UMA_0" and "UMA0" name the same object; the layout and corpus disagree. */
function normalizeName(name) {
    return String(name || '').toLowerCase().replace(/[^a-z0-9]/g, '');
}

/**
 * Where each named object stands, from the layout.
 *
 * Returns floor-centre positions. The corpus is baked into these coordinates,
 * so this is where the geometry will appear -- known before any of it has
 * downloaded, which is the point: a start pose framed from arrived content
 * would differ by method and by network, because what has arrived does.
 */
function placements(layout, objectNames = []) {
    const surfaces = layout?.surfaces || {};
    const objects = Object.values(layout?.objects || {});
    const wanted = new Set(objectNames.map(normalizeName));
    const chosen = wanted.size
        ? objects.filter(object => wanted.has(normalizeName(object.name)))
        : objects.filter(object => object.surface === 'stage');
    return chosen.map(object => ({
        name: object.name,
        x: Number(object.x) || 0,
        y: Number(surfaces[object.surface]?.y) || 0,
        z: Number(object.z) || 0
    }));
}

/**
 * One pose that shows every placed object: in front of the stage (it faces
 * +Z), slightly above, looking at the centre of their bounding box, and as
 * close as it can be while every corner of that box is inside the frustum.
 *
 * The box is fitted against the horizontal and vertical fields of view
 * separately, not as a bounding sphere. Performers spread across a stage make
 * a box about ten metres wide and two tall; a sphere round that is fitted to
 * the narrower (vertical) angle and stands the camera twice as far back as it
 * needs to be, leaving the people a thin strip across a mostly empty frame.
 *
 * @param {object} layout scene_layout.json
 * @param {string[]} [objectNames] the session's objects; empty means the stage
 * @param {object} [options]
 * @param {number} [options.fovDegrees=60] vertical
 * @param {number} [options.aspect=16/9] width / height
 * @param {number} [options.margin=1.1] slack around the box, >= 1
 */
function stagePose(layout, objectNames = [], {
    fovDegrees = DEFAULT_FOV_DEG, aspect = 16 / 9, margin = 1.1
} = {}) {
    const placed = placements(layout, objectNames);
    if (!placed.length) {
        throw new Error(`none of [${objectNames.join(', ')}] is in the scene layout`);
    }
    const min = [Infinity, Infinity, Infinity];
    const max = [-Infinity, -Infinity, -Infinity];
    for (const { x, y, z } of placed) {
        min[0] = Math.min(min[0], x - PLACEMENT_PAD_M);
        max[0] = Math.max(max[0], x + PLACEMENT_PAD_M);
        min[1] = Math.min(min[1], y);
        max[1] = Math.max(max[1], y + HUMAN_HEIGHT_M);
        min[2] = Math.min(min[2], z - PLACEMENT_PAD_M);
        max[2] = Math.max(max[2], z + PLACEMENT_PAD_M);
    }
    const target = min.map((low, axis) => (low + max[axis]) / 2);
    const elevation = (START_ELEVATION_DEG * Math.PI) / 180;
    // Unit vector from the target back towards the eye.
    const back = [0, Math.sin(elevation), Math.cos(elevation)];
    const tanV = Math.tan((fovDegrees * Math.PI) / 360) / margin;
    const tanH = aspect * Math.tan((fovDegrees * Math.PI) / 360) / margin;
    // Camera basis for that direction: forward = -back, right = forward x up.
    const forward = back.map(v => -v);
    const right = normalize(cross(forward, [0, 1, 0]));
    const up = cross(right, forward);
    const corners = [];
    for (const x of [min[0], max[0]]) for (const y of [min[1], max[1]]) for (const z of [min[2], max[2]]) {
        corners.push([x, y, z]);
    }
    const inside = distance => {
        const eye = target.map((v, axis) => v + back[axis] * distance);
        return corners.every(corner => {
            const d = corner.map((v, axis) => v - eye[axis]);
            const depth = dot(d, forward);
            return depth > 0.1
                && Math.abs(dot(d, right)) <= depth * tanH
                && Math.abs(dot(d, up)) <= depth * tanV;
        });
    };
    // Fitting is monotone in distance -- backing away along the view axis never
    // pushes a corner out -- so bisect for the nearest distance that fits.
    let low = 0.1;
    let high = 1;
    while (!inside(high)) high *= 2;
    for (let step = 0; step < 50; step++) {
        const mid = (low + high) / 2;
        if (inside(mid)) high = mid; else low = mid;
    }
    return {
        position: target.map((v, axis) => v + back[axis] * high),
        target,
        up: [0, 1, 0],
        fovDegrees,
        objects: placed.map(object => object.name)
    };
}

function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
function cross(a, b) {
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}
function normalize(v) {
    const length = Math.hypot(...v);
    return v.map(component => component / length);
}

function lerp(a, b, f) { return a + (b - a) * f; }
function lerpVector(a, b, f) { return a.map((value, axis) => lerp(value, b[axis], f)); }

/** Replays a recorded path: the pose at any playback time. */
class TrajectoryPlayer {
    constructor(trajectory) {
        const samples = trajectory?.samples;
        if (!Array.isArray(samples) || samples.length < 1) {
            throw new Error('a trajectory needs at least one sample');
        }
        this.samples = samples;
        this.duration = samples[samples.length - 1].t;
    }

    /**
     * Linear between samples, held at either end. Position and target are
     * interpolated separately rather than as an orientation, which is exact for
     * an orbit camera -- its orientation is defined by those two points.
     */
    poseAt(t) {
        const samples = this.samples;
        if (t <= samples[0].t) return { ...samples[0] };
        const last = samples[samples.length - 1];
        if (t >= last.t) return { ...last };
        let low = 0;
        let high = samples.length - 1;
        while (high - low > 1) {
            const mid = (low + high) >> 1;
            if (samples[mid].t <= t) low = mid; else high = mid;
        }
        const a = samples[low];
        const b = samples[high];
        const f = b.t > a.t ? (t - a.t) / (b.t - a.t) : 0;
        return {
            t,
            position: lerpVector(a.position, b.position, f),
            target: lerpVector(a.target, b.target, f),
            up: lerpVector(a.up, b.up, f),
            fovDegrees: lerp(a.fovDegrees, b.fovDegrees, f)
        };
    }
}

/** Records the camera as the participant drives it. */
class TrajectoryRecorder {
    /**
     * @param {object} [options]
     * @param {number} [options.interval=1/30] minimum seconds between samples
     * @param {string} [options.recordedWith] the method page it came from
     * @param {string[]} [options.objects]
     */
    constructor({ interval = 1 / 30, recordedWith = null, objects = [] } = {}) {
        this.interval = interval;
        this.recordedWith = recordedWith;
        this.objects = objects;
        this.samples = [];
        this._lastT = null;
    }

    /**
     * Spacing is measured from the last sample's unrounded time. Measuring from
     * the stored, rounded one drops a sample whenever the rounding went up --
     * every other one at 30 Hz -- and the 10% slack keeps ordinary frame
     * jitter from doing the same.
     */
    sample(t, pose) {
        if (this._lastT !== null && t - this._lastT < this.interval * 0.9) return false;
        this._lastT = t;
        this.samples.push({
            t: Number(t.toFixed(4)),
            position: pose.position.map(v => Number(v.toFixed(5))),
            target: pose.target.map(v => Number(v.toFixed(5))),
            up: (pose.up || [0, 1, 0]).map(v => Number(v.toFixed(5))),
            fovDegrees: Number(pose.fovDegrees.toFixed(3))
        });
        return true;
    }

    toJSON() {
        return {
            schemaVersion: 1,
            coordinateSpace: 'three-world-metres-y-up',
            recordedWith: this.recordedWith,
            objects: this.objects,
            samples: this.samples
        };
    }
}

/** The pose a three.js camera plus OrbitControls is showing. */
function readPose(camera, controls) {
    return {
        position: camera.position.toArray(),
        target: controls.target.toArray(),
        up: camera.up.toArray(),
        fovDegrees: camera.fov
    };
}

/** Put a three.js camera plus OrbitControls exactly at `pose`. */
function applyPose(camera, controls, pose) {
    camera.position.set(...pose.position);
    controls.target.set(...pose.target);
    camera.up.set(...pose.up);
    if (camera.fov !== pose.fovDegrees) {
        camera.fov = pose.fovDegrees;
        camera.updateProjectionMatrix();
    }
}

module.exports = {
    normalizeName, placements, stagePose,
    TrajectoryPlayer, TrajectoryRecorder, readPose, applyPose,
    HUMAN_HEIGHT_M, DEFAULT_FOV_DEG
};
