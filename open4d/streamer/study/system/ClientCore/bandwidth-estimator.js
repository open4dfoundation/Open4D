'use strict';

/** Return a causal harmonic-mean throughput estimate from recent samples. */
function harmonicBandwidthEstimate(samples, windowSize = 2) {
    if (!Number.isInteger(windowSize) || windowSize <= 0) {
        throw new RangeError('windowSize must be a positive integer');
    }
    if (!Array.isArray(samples) || samples.length === 0) {
        throw new RangeError('at least one bandwidth sample is required');
    }
    const recent = samples.slice(-windowSize);
    if (recent.some(value => !Number.isFinite(value) || value <= 0)) {
        throw new RangeError('bandwidth samples must be finite and positive');
    }
    return recent.length / recent.reduce((sum, value) => sum + 1 / value, 0);
}

module.exports = { harmonicBandwidthEstimate };
