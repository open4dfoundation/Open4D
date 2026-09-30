'use strict';

/**
 * One trial's clock, camera and measurements, identical for every method page.
 *
 * The page calls `apply()` once per render loop, just before it updates its
 * orbit controls, and tells it what the loop did: whether content time
 * advanced, and how many objects got a new frame. From that alone the driver
 *
 *   - holds the start pose until the first content is visible, then runs the
 *     trial clock from that instant -- *playback* time, so a method that takes
 *     longer to start does not arrive late to the path, as the Quest's offline
 *     benchmark aligns it;
 *   - replays the shared path at that playback time, or records the one the
 *     participant is driving;
 *   - counts presentation slots against the fixed content rate, and ends the
 *     trial when the clock reaches the trial length.
 *
 * The measurements are defined here once and computed the same way for every
 * method, which is what makes them comparable. Each page's own metrics are
 * kept beside these as `native`, not mixed in: they are defined differently,
 * and averaging across definitions produces a number that means nothing.
 */

const { TrajectoryPlayer, TrajectoryRecorder, readPose, applyPose } = require('./camera');

class StudyDriver {
    /**
     * @param {object} args
     * @param {'replay'|'record'} args.mode
     * @param {object} args.startPose where the camera sits until content shows
     * @param {object} [args.trajectory] required to replay
     * @param {number} args.durationSeconds
     * @param {number} [args.fps=30] content rate
     * @param {string} [args.method]
     * @param {number} [args.createdAt] ms; the start of the startup delay
     * @param {(summary: object) => void} [args.onEnded]
     * @param {number} [args.waitForObjects] recording only: hold the start
     *   until this many objects are visible, so the participant chooses a path
     *   through the whole stage rather than through whatever loaded first
     * @param {number} [args.waitCapMs=60000] give up waiting after this long
     */
    constructor({ mode, startPose, trajectory, durationSeconds, fps = 30,
                  method = null, createdAt = 0, onEnded = null,
                  waitForObjects = 0, waitCapMs = 60000 }) {
        if (!['replay', 'record'].includes(mode)) {
            throw new Error(`study mode must be "replay" or "record", got ${mode}`);
        }
        if (mode === 'replay' && !trajectory) throw new Error('replay needs a trajectory');
        if (!(durationSeconds > 0)) throw new Error('trial length must be positive');
        this.mode = mode;
        this.startPose = startPose;
        this.durationSeconds = durationSeconds;
        this.fps = fps;
        this.method = method;
        this.onEnded = onEnded;
        this.player = mode === 'replay' ? new TrajectoryPlayer(trajectory) : null;
        this.recorder = mode === 'record'
            ? new TrajectoryRecorder({ recordedWith: method, objects: startPose.objects || [] })
            : null;

        this.createdAt = createdAt;
        this.waitForObjects = mode === 'record' ? waitForObjects : 0;
        this.waitCapMs = waitCapMs;
        this.firstSeenAt = null;
        this.visible = 0;
        this.startedPartial = false;
        this.firstContentAt = null;
        this.ended = false;
        this.t = 0;
        // Slot accounting, after the first content only; startup is its own number.
        this.slots = 0;
        this.advancedSlots = 0;
        this.allAdvancedSlots = 0;
        this.repeatedSlots = 0;
        this.renderDroppedSlots = 0;
        this.freezes = 0;
        this.longestFreezeSlots = 0;
        this._freezeRun = 0;
    }

    /**
     * Whether the page should let the participant move the camera: only while
     * recording, and only once content is visible. Before that the camera is
     * held at the start pose -- a drag during startup would otherwise begin the
     * shared path somewhere other than the start every method is given.
     */
    get interactive() { return this.mode === 'record' && this.firstContentAt !== null; }

    /**
     * @param {object} camera three.js PerspectiveCamera
     * @param {object} controls OrbitControls
     * @param {object} info
     * @param {number} info.now ms, the render loop's timestamp
     * @param {boolean} info.advanced content time moved on this loop
     * @param {number} [info.steps=1] content frames it moved by
     * @param {number} info.presented objects that got a new frame
     * @param {number} info.objects objects in the scene
     * @param {number} [info.visible] objects on screen, new frame or held
     */
    apply(camera, controls, { now, advanced, steps = 1, presented, objects, visible = presented }) {
        if (this.ended) return;
        this.visible = visible;
        if (this.firstContentAt === null) {
            // Held every loop, not placed once: startup can be long, and nothing
            // may move the camera off the start before the clock begins.
            applyPose(camera, controls,
                this.mode === 'replay' ? this.player.poseAt(0) : this.startPose);
            if (!(advanced && presented > 0)) return;
            if (this.firstSeenAt === null) this.firstSeenAt = now;
            if (this.waitForObjects && visible < this.waitForObjects) {
                if (now - this.firstSeenAt < this.waitCapMs) return;
                // Recorded anyway rather than hanging the session; the path is
                // still a path, and the flag says it was chosen over part of it.
                this.startedPartial = true;
            }
            this.firstContentAt = now;
        }
        this.t = (now - this.firstContentAt) / 1000;

        if (this.mode === 'replay') {
            applyPose(camera, controls, this.player.poseAt(this.t));
        } else {
            this.recorder.sample(this.t, readPose(camera, controls));
        }

        if (advanced) {
            this.slots += 1;
            this.renderDroppedSlots += Math.max(0, steps - 1);
            if (presented > 0) {
                this.advancedSlots += 1;
                if (objects > 0 && presented >= objects) this.allAdvancedSlots += 1;
                if (this._freezeRun > 0) this._endFreeze();
            } else {
                this.repeatedSlots += 1;
                if (this._freezeRun === 0) this.freezes += 1;
                this._freezeRun += 1;
            }
        }

        if (this.t >= this.durationSeconds) {
            if (this._freezeRun > 0) this._endFreeze();
            this.ended = true;
            this.onEnded?.(this.summary());
        }
    }

    _endFreeze() {
        this.longestFreezeSlots = Math.max(this.longestFreezeSlots, this._freezeRun);
        this._freezeRun = 0;
    }

    /** The common, method-independent measurements for this trial. */
    summary() {
        const seconds = Math.min(this.t, this.durationSeconds);
        const slotMs = 1000 / this.fps;
        const ratio = (part, whole) => (whole > 0 ? Number((part / whole).toFixed(4)) : null);
        return {
            method: this.method,
            mode: this.mode,
            completed: this.ended,
            startupDelayMs: this.firstContentAt === null
                ? null : Math.round(this.firstContentAt - this.createdAt),
            playbackSeconds: Number(seconds.toFixed(3)),
            expectedSlots: Math.round(this.durationSeconds * this.fps),
            presentationSlots: this.slots,
            contentAdvanceFps: seconds > 0 ? Number((this.advancedSlots / seconds).toFixed(2)) : null,
            allObjectAdvanceFps: seconds > 0
                ? Number((this.allAdvancedSlots / seconds).toFixed(2)) : null,
            repeatedSlots: this.repeatedSlots,
            freezeRatio: ratio(this.repeatedSlots, this.slots),
            freezes: this.freezes,
            longestFreezeMs: Math.round(Math.max(this.longestFreezeSlots, this._freezeRun) * slotMs),
            renderDroppedSlots: this.renderDroppedSlots,
            ...(this.waitForObjects ? { recordedWithPartialStage: this.startedPartial } : {})
        };
    }

    trajectory() { return this.recorder ? this.recorder.toJSON() : null; }
}

module.exports = { StudyDriver };
