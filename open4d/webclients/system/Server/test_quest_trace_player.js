#!/usr/bin/env node
const assert = require('assert');
const {
  TBF_BURST_FLOOR_BYTES,
  tbfArgs,
  tcByteCount,
  tbfIsWedged,
} = require('./quest-trace-player');

function argumentAfter(args, name) {
  const index = args.indexOf(name);
  assert.notStrictEqual(index, -1, `missing ${name}`);
  return args[index + 1];
}

const normalBurst = Number(argumentAfter(tbfArgs(200), 'burst').replace(/b$/, ''));
assert.strictEqual(normalBurst, TBF_BURST_FLOOR_BYTES);
assert(normalBurst >= 2 * 65536, 'bucket must fit a normal GSO skb with headroom');

const veryFastBurst = Number(argumentAfter(tbfArgs(2000), 'burst').replace(/b$/, ''));
assert.strictEqual(veryFastBurst, 250000, 'one-millisecond rate burst still applies above floor');

assert.strictEqual(tcByteCount('64', 'K'), 65536);
assert.strictEqual(tcByteCount('1.5', 'M'), 1572864);

const before = { bytes: 761000000, overlimits: 1000 };
assert.strictEqual(tbfIsWedged(before, { bytes: 761000000, overlimits: 1001 }), true);
assert.strictEqual(tbfIsWedged(before, { bytes: 761000001, overlimits: 1001 }), false);
assert.strictEqual(tbfIsWedged(before, { bytes: 761000000, overlimits: 1000 }), false);

console.log('quest trace player tests passed');
