'use strict';

/**
 * Keep a shaper in another process on the study server's trace.
 *
 * The point-cloud baselines do not come through the Node server: their bytes
 * go Python server -> TCP -> v4ds-bridge.js -> WebSocket -> browser. So the
 * bridge shapes them itself, and this is how it knows what to shape to. It
 * polls /api/study/shaping and arms a local TraceShaper with the *same* trace
 * from the *same* wall-clock instant the server armed, so a baseline trial and
 * a mesh trial see an identical link.
 *
 * Polled rather than pushed, because the bridge is a separate short-lived
 * process with no channel to the server otherwise, and half a second of
 * arming lag does not shift the trace: the rate is a function of wall-clock
 * time since the server's start, not of when the bridge heard about it.
 */

const { TraceShaper } = require('./shaper');

class ShapingFollower {
    /**
     * @param {object} args
     * @param {string} args.serverUrl the study server, e.g. http://127.0.0.1:3000
     * @param {TraceShaper} [args.shaper]
     * @param {number} [args.intervalMs=500]
     * @param {typeof fetch} [args.fetchImpl]
     * @param {(level: string, message: string) => void} [args.log]
     */
    constructor({ serverUrl, shaper = new TraceShaper(), intervalMs = 500,
                  fetchImpl = globalThis.fetch, log = () => {} }) {
        this.serverUrl = String(serverUrl).replace(/\/+$/, '');
        this.shaper = shaper;
        this.intervalMs = intervalMs;
        this._fetch = fetchImpl;
        this._log = log;
        this._startedAt = null;
        this._timer = null;
        this._failures = 0;
    }

    start() {
        if (this._timer) return this;
        const tick = () => this.poll().catch(() => {});
        tick();
        this._timer = setInterval(tick, this.intervalMs);
        this._timer.unref?.();
        return this;
    }

    stop() {
        if (this._timer) clearInterval(this._timer);
        this._timer = null;
        this.shaper.disarm();
        this._startedAt = null;
    }

    /** One look at the server's link, and the local shaper brought into line. */
    async poll() {
        let status;
        try {
            const response = await this._fetch(`${this.serverUrl}/api/study/shaping`,
                { cache: 'no-store' });
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            status = await response.json();
            this._failures = 0;
        } catch (error) {
            // Losing the server mid-trial must not freeze the link at whatever
            // it last was: after a few misses, stop shaping and say so.
            this._failures += 1;
            if (this._failures === 3 && this.shaper.armed) {
                this._log('WARN', `study server unreachable (${error.message}); unshaping`);
                this.shaper.disarm();
                this._startedAt = null;
            }
            throw error;
        }
        const armed = status.startedAt !== null && Array.isArray(status.points)
            && status.points.length > 0;
        if (armed && status.startedAt !== this._startedAt) {
            this.shaper.arm(status.points, {
                hold: status.hold !== false, startedAt: status.startedAt,
                label: status.label ?? null
            });
            this._startedAt = status.startedAt;
            this._log('INFO', `following study trial ${status.label ?? ''} `
                + `(${status.rateMbps ?? '?'} Mbps now)`);
        } else if (!armed && this.shaper.armed) {
            this.shaper.disarm();
            this._startedAt = null;
            this._log('INFO', 'study trial over; link unshaped');
        }
        return status;
    }
}

module.exports = { ShapingFollower };
