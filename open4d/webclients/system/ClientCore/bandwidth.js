'use strict';

/**
 * Bandwidth estimation and the client's spend budget.
 *
 * Extracted verbatim from system/Client/client.js. Platform-free: the caller
 * injects a clock, a logger, and a `signals()` view of delivery health, so this
 * module works unchanged in Node and in a browser.
 */

const { harmonicBandwidthEstimate } = require('./bandwidth-estimator');

const BANDWIDTH_SAMPLE_WINDOW = 5;
const BANDWIDTH_ESTIMATOR_WINDOW = 2;

/**
 * The client commits only a fraction of its bandwidth ESTIMATE each segment.
 *
 * LIVE system: segments are produced one per segment interval, so a client can
 * never buffer ahead more than ~one segment. VOD-style absolute thresholds
 * ("buffer <= 2s is critical") are therefore always true here and were used to
 * halve the budget permanently. Instead, scale on delivery health: are we
 * actually failing to keep up?
 *   - an object starving unintentionally (MISSING; deliberate freezes excluded),
 *   - a download backlog at the in-flight cap,
 *   - or the last segment download arriving late
 * all mean the link is behind -> leave recovery headroom. Otherwise use a
 * standard safety margin (the estimate is already min-biased).
 *
 * Mirrors vstream/config.py CLIENT_BUDGET_MULTIPLIER{,_STRUGGLING}: the server
 * needs the same number to decide whether the ladder's published floor is
 * affordable, so the two must be changed together.
 *
 * @returns {{budget: number, multiplier: number, reason: string, struggles: string[]}}
 */
function computeBudget(estimate, signals, multiplier, multiplierStruggling) {
    const struggles = [];
    if (signals.missingCount > 0) struggles.push('missing-objects');
    if (signals.inFlightCount >= signals.maxInFlight) struggles.push('download-backlog');
    if (signals.lastDownloadLate) struggles.push('late-download');

    const chosen = struggles.length > 0 ? multiplierStruggling : multiplier;
    return {
        budget: estimate * chosen,
        multiplier: chosen,
        reason: struggles.length > 0 ? struggles.join('+') : 'normal',
        struggles
    };
}

class BandwidthEstimator {
    /**
     * @param {object} deps
     * @param {() => number} deps.elapsedMs ms since run start, for history stamps
     * @param {() => Iterable<[number, {startTime: number, bytesExpected: number}]>} deps.inFlightEntries
     * @param {() => {missingCount: number, inFlightCount: number, maxInFlight: number, lastDownloadLate: boolean}} deps.signals
     * @param {object} [deps.logger]
     * @param {() => number} deps.now platform clock (`platform.clock.now`).
     *   REQUIRED for the same reason as LinkThroughputMeter's.
     * @param {number} [deps.initialEstimate]
     * @param {number} [deps.multiplier]
     * @param {number} [deps.multiplierStruggling]
     */
    constructor({
        elapsedMs,
        inFlightEntries,
        signals,
        logger = null,
        now,
        initialEstimate = 5,
        multiplier = 1,
        multiplierStruggling = 1
    }) {
        if (typeof now !== 'function') {
            throw new TypeError('now must be a function (pass platform.clock.now)');
        }
        this._elapsedMs = elapsedMs;
        this._inFlightEntries = inFlightEntries;
        this._signals = signals;
        this._logger = logger;
        this._now = now;
        this._multiplier = multiplier;
        this._multiplierStruggling = multiplierStruggling;

        this.estimate = initialEstimate;
        this.samples = [];
        /**
         * Owned here but shared by reference with the run's metrics record, so
         * the serialized output is the same array this module appends to.
         */
        this.history = [];
    }

    /** Fold one aggregate throughput sample into the estimate. */
    update(downloadSizeBytes, downloadTimeMs) {
        if (!downloadSizeBytes || downloadSizeBytes <= 0
            || !downloadTimeMs || downloadTimeMs <= 0) {
            return;
        }

        const measuredMbps = (downloadSizeBytes * 8) / downloadTimeMs / 1000;

        if (!isFinite(measuredMbps) || measuredMbps <= 0) {
            return;
        }

        this.samples.push(measuredMbps);
        if (this.samples.length > BANDWIDTH_SAMPLE_WINDOW) {
            this.samples.shift();
        }

        // Two-sample harmonic mean: causal and responsive, while still weighting
        // a low sample more heavily than an arithmetic average. Offline replay of
        // all six archived GTA-VI trials reduced next-sample MAPE from 30.7% (the
        // old minimum-of-two rule) to 24.6%. The ABR's separate budget multiplier
        // keeps headroom; estimator bias and budget safety must not be applied
        // twice.
        this.estimate = harmonicBandwidthEstimate(this.samples, BANDWIDTH_ESTIMATOR_WINDOW);

        this.history.push({
            timestamp: this._elapsedMs(),
            measured: measuredMbps,
            estimated: this.estimate,
            samples: [...this.samples]
        });

        this._logger?.debug?.('BANDWIDTH', 'Updated estimate', {
            measured: measuredMbps.toFixed(2),
            harmonic: this.estimate.toFixed(2),
            estimated: this.estimate.toFixed(2)
        });
    }

    /**
     * Current estimate, degraded by any in-flight download that is visibly
     * slower than the standing estimate. Only consulted before the first real
     * sample lands, otherwise the samples speak for themselves.
     */
    current() {
        if (this.samples.length === 0) {
            const now = this._now();
            for (const [segId, info] of this._inFlightEntries()) {
                const elapsedSec = (now - info.startTime) / 1000;
                if (elapsedSec > 5 && info.bytesExpected > 0) {
                    const estimatedMbps = (info.bytesExpected * 8) / (elapsedSec * 1000) / 1000;
                    if (estimatedMbps < this.estimate) {
                        this._logger?.warn?.('BANDWIDTH',
                            'In-flight download slow, reducing estimate', {
                                segId,
                                elapsedSec: elapsedSec.toFixed(1),
                                oldEstimate: this.estimate.toFixed(2),
                                newEstimate: Math.max(1, estimatedMbps).toFixed(2)
                            });
                        this.estimate = Math.max(1, estimatedMbps);
                    }
                }
            }
        }

        return this.estimate;
    }

    /** Mbps this segment is allowed to spend. */
    budget() {
        const currentBW = this.current();
        const signals = this._signals();
        const result = computeBudget(
            currentBW, signals, this._multiplier, this._multiplierStruggling);

        this._logger?.debug?.('BUDGET', 'Calculated', {
            estimatedBW: currentBW.toFixed(2),
            multiplier: result.multiplier.toFixed(2),
            budget: result.budget.toFixed(2),
            reason: result.reason,
            inFlight: signals.inFlightCount
        });

        return result.budget;
    }
}

module.exports = {
    BandwidthEstimator,
    computeBudget,
    BANDWIDTH_SAMPLE_WINDOW,
    BANDWIDTH_ESTIMATOR_WINDOW
};
