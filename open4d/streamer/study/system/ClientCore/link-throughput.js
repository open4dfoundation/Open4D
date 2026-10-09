'use strict';

/**
 * Aggregate link throughput measurement.
 *
 * Extracted verbatim from system/Client/client.js. Per-download measurement is
 * biased low whenever segment downloads overlap (each sees a share of the
 * link); instead accumulate bytes and busy time across the link's busy periods
 * and emit a sample once enough busy time has accumulated (short bursts from
 * fast links accumulate across downloads rather than being discarded).
 *
 * Progressive byte accounting: bytes are credited as each FILE lands, not in
 * one lump when the whole segment download finishes. Busy time accumulates
 * continuously, so crediting bytes only at segment completion made the
 * numerator and denominator cover different windows. When a long download
 * completed while the next was still in flight, the rolling-window branch
 * divided ONE segment's bytes by a window during which the link had also been
 * serving the concurrent download, whose bytes had not landed yet. Measured a
 * ~30% under-read (e.g. 58.18 MiB / 3638 ms = 134 Mbps on a ~190 Mbps link),
 * which then tripped the drop guard in BandwidthEstimator.update and replaced a
 * 245 Mbps estimate with 134 Mbps on a link that had not slowed at all. In one
 * 84-sample run the guard fired on 13% of samples at a median 0.70x the
 * harmonic mean — a systematic bias, triggered whenever a download overran the
 * segment interval (29 late downloads in that run).
 *
 * `segmentIntervalMs` is a FUNCTION, not a number: the stream config arrives
 * from the server after this meter is constructed, so reading a captured value
 * would freeze the rolling-window threshold at its pre-config state.
 */

const LINK_MIN_SAMPLE_MS = 100;      // enough busy time for a sample...
const LINK_MIN_SAMPLE_BYTES = 2e6;   // ...or enough bytes (fast links finish
                                     // a whole download in tens of ms)

class LinkThroughputMeter {
    /**
     * @param {object} deps
     * @param {() => number} deps.segmentIntervalMs live segment interval, ms
     * @param {(bytes: number, busyMs: number) => void} deps.onSample emit a sample
     * @param {() => number} deps.now  platform clock (`platform.clock.now`).
     *   REQUIRED, deliberately: with a Date.now default, a test driving a
     *   virtual clock that forgot to pass this would silently measure wall
     *   time and appear to pass.
     */
    constructor({ segmentIntervalMs, onSample, now }) {
        if (typeof segmentIntervalMs !== 'function') {
            throw new TypeError('segmentIntervalMs must be a function');
        }
        if (typeof onSample !== 'function') {
            throw new TypeError('onSample must be a function');
        }
        if (typeof now !== 'function') {
            throw new TypeError('now must be a function (pass platform.clock.now)');
        }
        this._segmentIntervalMs = segmentIntervalMs;
        this._onSample = onSample;
        this._now = now;

        this.activeCount = 0;
        this.busyStartMs = 0;
        this.bytesAccum = 0;
        this.busyMsAccum = 0;
    }

    started() {
        if (this.activeCount === 0) {
            this.busyStartMs = this._now();
        }
        this.activeCount++;
    }

    bytesDelivered(bytes) {
        if (bytes > 0) this.bytesAccum += bytes;
    }

    finished() {
        this.activeCount = Math.max(0, this.activeCount - 1);
        const now = this._now();

        if (this.activeCount === 0) {
            this.busyMsAccum += now - this.busyStartMs;
        } else if (now - this.busyStartMs >= this._segmentIntervalMs()) {
            // sustained load: roll the window so we still sample periodically
            this.busyMsAccum += now - this.busyStartMs;
            this.busyStartMs = now;
        } else {
            return; // other downloads still running inside the current window
        }

        const enough = this.busyMsAccum >= LINK_MIN_SAMPLE_MS ||
            (this.bytesAccum >= LINK_MIN_SAMPLE_BYTES && this.busyMsAccum >= 10);
        if (enough && this.bytesAccum > 0) {
            this._onSample(this.bytesAccum, this.busyMsAccum);
            this.bytesAccum = 0;
            this.busyMsAccum = 0;
        }
    }
}

module.exports = { LinkThroughputMeter, LINK_MIN_SAMPLE_MS, LINK_MIN_SAMPLE_BYTES };
