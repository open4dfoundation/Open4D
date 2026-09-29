'use strict';

/**
 * A trace-driven bottleneck, applied to responses rather than to the NIC.
 *
 * `quest-trace-player.js` shapes with `tc`: a root token bucket on the egress
 * interface, which is the real kernel queue and the most faithful option. It
 * also needs root, and it shapes every flow on the host -- on a shared box that
 * is every other user's traffic and the viewer's own connection to the page.
 * A study where the participant uploads a trace from a browser cannot run
 * `sudo`, so this does the same job one layer up: a token bucket that the
 * bytes of every shaped response pass through, following the trace from the
 * moment a trial is armed.
 *
 * What it keeps from the tc path, so the two are comparable:
 *
 *   - the rate is the trace's step function, rebased to the trial start;
 *   - the burst is `max(128 KiB, 1 ms of rate)`, the tbf bucket the trace
 *     player installs;
 *   - one bucket per process, served first-in first-out, so concurrent
 *     requests contend for one link rather than each getting their own.
 *
 * What it gives up: there is no kernel queue underneath, so TCP's congestion
 * control never sees loss or queueing delay -- the sender is simply paced. For
 * a segment client that times whole downloads that is the quantity it
 * measures; for anything that reacts to RTT it is not the same experiment.
 *
 * Time is wall-clock seconds (`Date.now`), not a process-local clock, so that
 * a second process -- the ViVo/NAVA WebSocket bridge -- can follow the same
 * trace from the same armed instant.
 */

const { Transform } = require('stream');
const { rateAt, traceDuration } = require('./trace');

const BURST_FLOOR_BYTES = 128 * 1024;
/** Longest single sleep, so disarming or re-arming takes effect promptly. */
const MAX_SLEEP_SECONDS = 0.05;
/** Shortest, so a wait always moves the clock. */
const MIN_SLEEP_SECONDS = 1e-6;
const TOKEN_EPSILON = 1;

function burstBytes(mbps) {
    return Math.max(BURST_FLOOR_BYTES, Math.ceil(mbps * 125));
}

function bytesPerSecond(mbps) {
    return (mbps * 1e6) / 8;
}

const wallClock = () => Date.now() / 1000;
const realSleep = seconds => new Promise(resolve => setTimeout(resolve, seconds * 1000));

class TraceShaper {
    /**
     * @param {object} [deps]
     * @param {() => number} [deps.now] seconds; injectable so a test can drive it
     * @param {(seconds: number) => Promise<void>} [deps.sleep]
     */
    constructor({ now = wallClock, sleep = realSleep } = {}) {
        this._now = now;
        this._sleep = sleep;
        this._queue = Promise.resolve();
        this.disarm();
    }

    /**
     * Start following `points` now (or at `startedAt`).
     *
     * @param {{time: number, bandwidth: number}[]} points rebased trace
     * @param {object} [options]
     * @param {boolean} [options.hold=true] keep the last rate after the trace
     *   ends, as the study's `--hold` does; otherwise the link goes unshaped
     * @param {number} [options.startedAt] wall-clock seconds
     * @param {string} [options.label] what to call it in `status()`
     */
    arm(points, { hold = true, startedAt = this._now(), label = null } = {}) {
        if (!Array.isArray(points) || !points.length) {
            throw new Error('cannot arm a shaper with an empty trace');
        }
        this._points = points;
        this._hold = hold;
        this._startedAt = startedAt;
        this._label = label;
        // A fresh trial starts with a full bucket, as a freshly installed tbf
        // does, and forgets what the previous trial sent.
        this._tokens = burstBytes(points[0].bandwidth);
        this._at = startedAt;
        this._delivered = 0;
        this._waited = 0;
        this._generation = (this._generation || 0) + 1;
    }

    disarm() {
        this._points = null;
        this._startedAt = null;
        this._label = null;
        this._generation = (this._generation || 0) + 1;
    }

    get armed() { return this._points !== null; }

    /** Seconds since the trial was armed, or null when unarmed. */
    elapsed(at = this._now()) {
        return this.armed ? at - this._startedAt : null;
    }

    /** The rate in force at wall-clock `at`, in Mbps, or null when unshaped. */
    rateMbps(at = this._now()) {
        if (!this.armed) return null;
        const t = Math.max(0, at - this._startedAt);
        if (t > traceDuration(this._points) && !this._hold) return null;
        return rateAt(this._points, t);
    }

    status() {
        const now = this._now();
        const rate = this.rateMbps(now);
        return {
            shaped: rate !== null,
            mechanism: 'in-process',
            label: this._label,
            rateMbps: rate === null ? null : Number(rate.toFixed(2)),
            elapsedSeconds: this.armed ? Number(this.elapsed(now).toFixed(3)) : null,
            traceSeconds: this.armed ? traceDuration(this._points) : null,
            hold: this.armed ? this._hold : null,
            deliveredBytes: this.armed ? this._delivered : 0,
            pacedSeconds: this.armed ? Number(this._waited.toFixed(3)) : 0
        };
    }

    /**
     * Resolve once `bytes` may leave. Callers are served in the order they
     * asked, which is what makes several responses share one link.
     */
    take(bytes) {
        const turn = this._queue.then(() => this._take(bytes));
        // A failed take must not wedge everyone queued behind it.
        this._queue = turn.catch(() => {});
        return turn;
    }

    async _take(bytes) {
        let remaining = bytes;
        while (remaining > 0) {
            if (!this.armed) return;
            const generation = this._generation;
            const now = this._now();
            const rate = this.rateMbps(now);
            if (rate === null) { this._delivered += remaining; return; }
            this._refill(now);
            const piece = Math.min(remaining, burstBytes(rate));
            // Within a byte counts as enough: the deficit is otherwise a
            // rounding residue whose wait is too small to move the clock.
            if (this._tokens + TOKEN_EPSILON >= piece) {
                this._tokens = Math.max(0, this._tokens - piece);
                this._delivered += piece;
                remaining -= piece;
                continue;
            }
            const wait = Math.min(MAX_SLEEP_SECONDS, Math.max(MIN_SLEEP_SECONDS,
                this._secondsUntil(piece - this._tokens, now)));
            this._waited += wait;
            await this._sleep(wait);
            // Re-armed while asleep: the bucket belongs to a new trial now, and
            // bytes owed to the old one are not the new one's to pay.
            if (generation !== this._generation) return;
        }
    }

    /** The rate at trace time `t` (seconds since arming), or null. */
    _rateAtTraceTime(t) {
        if (t > traceDuration(this._points) && !this._hold) return null;
        return rateAt(this._points, Math.max(0, t));
    }

    /**
     * Integrate the trace from the last refill up to wall-clock `to`.
     *
     * In trace time, not wall time. The boundaries are the trace's own sample
     * times; converting each back to wall time and subtracting again does not
     * round-trip in floating point, and a step that lands exactly where it
     * began loops for ever.
     */
    _refill(to) {
        let t = this._at - this._startedAt;
        const end = to - this._startedAt;
        while (t < end) {
            const rate = this._rateAtTraceTime(t);
            if (rate === null) break;
            const next = Math.min(end, this._nextBoundary(t));
            if (!(next > t)) break;
            this._tokens += bytesPerSecond(rate) * (next - t);
            t = next;
        }
        this._at = to;
        const rate = this.rateMbps(to);
        if (rate !== null) this._tokens = Math.min(this._tokens, burstBytes(rate));
    }

    /** Seconds from wall-clock `now` until `deficit` more bytes have accumulated. */
    _secondsUntil(deficit, now) {
        const start = now - this._startedAt;
        let t = start;
        let owed = deficit;
        for (let guard = 0; guard < 100000; guard++) {
            const rate = this._rateAtTraceTime(t);
            if (rate === null) return t - start;
            const perSecond = bytesPerSecond(rate);
            const boundary = this._nextBoundary(t);
            const span = boundary - t;
            if (!Number.isFinite(span) || !(span > 0) || perSecond * span >= owed) {
                return t - start + owed / perSecond;
            }
            owed -= perSecond * span;
            t = boundary;
        }
        return MAX_SLEEP_SECONDS;
    }

    /** Trace time of the next rate change after `t`, or Infinity. */
    _nextBoundary(t) {
        for (const point of this._points) {
            if (point.time > t) return point.time;
        }
        const end = traceDuration(this._points);
        return !this._hold && t < end ? end : Infinity;
    }

    /**
     * A stream stage that holds each chunk until the link can carry it. Chunks
     * are split to the burst size first: a 64 KiB read waiting on a bucket
     * that can only ever hold less would never leave.
     */
    pace() {
        const shaper = this;
        return new Transform({
            transform(chunk, _encoding, done) {
                (async () => {
                    let offset = 0;
                    while (offset < chunk.length) {
                        const rate = shaper.rateMbps();
                        const size = rate === null ? chunk.length - offset : burstBytes(rate);
                        const piece = chunk.subarray(offset, offset + size);
                        await shaper.take(piece.length);
                        this.push(piece);
                        offset += piece.length;
                    }
                })().then(() => done(), done);
            }
        });
    }
}

module.exports = { TraceShaper, burstBytes, BURST_FLOOR_BYTES };
