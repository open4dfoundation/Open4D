'use strict';

const assert = require('assert');
const {
    harmonicBandwidthEstimate,
} = require('../system/ClientCore/bandwidth-estimator');

assert.strictEqual(harmonicBandwidthEstimate([80]), 80);
assert.ok(Math.abs(harmonicBandwidthEstimate([100, 200]) - 133.3333333333) < 1e-8);
assert.ok(Math.abs(harmonicBandwidthEstimate([100, 200, 400]) - 266.6666666667) < 1e-8);
assert.ok(Math.abs(harmonicBandwidthEstimate([100, 200, 400], 3) - 171.4285714286) < 1e-8);
assert.throws(() => harmonicBandwidthEstimate([], 2), /at least one/);
assert.throws(() => harmonicBandwidthEstimate([100, 0], 2), /finite and positive/);

console.log('bandwidth estimator tests passed');
