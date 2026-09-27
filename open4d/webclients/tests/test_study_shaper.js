'use strict';

const assert = require('node:assert');
const test = require('node:test');
const { PassThrough } = require('node:stream');

const { parseTrace, windowTrace, rateAt, summarize } =
    require('../system/Server/study/trace');
const { TraceShaper, burstBytes, BURST_FLOOR_BYTES } =
    require('../system/Server/study/shaper');

/** A clock that advances only when something sleeps: exact and instant. */
function virtualClock(start = 1000) {
    const clock = { t: start };
    clock.now = () => clock.t;
    clock.sleep = async seconds => { clock.t += seconds; };
    return clock;
}

const MB = 1e6;

test('a trace parses as the Quest trace player reads it', () => {
    const points = parseTrace('time,bandwidth\n5,10\n6,20\n\n8,5\n');
    assert.deepStrictEqual(points, [
        { time: 0, bandwidth: 10 }, { time: 1, bandwidth: 20 }, { time: 3, bandwidth: 5 }
    ]);
});

test('a malformed row is refused rather than skipped', () => {
    assert.throws(() => parseTrace('time,bandwidth\n0,10\nzero,20\n'), /line 3/);
});

test('time running backwards is refused', () => {
    assert.throws(() => parseTrace('0,10\n5,10\n3,10\n'), /backwards/);
});

test('a non-positive rate is refused', () => {
    assert.throws(() => parseTrace('0,10\n1,0\n'), /invalid trace value/);
});

test('a window keeps the rate in force at its start', () => {
    const points = parseTrace('0,10\n1,20\n2,30\n3,40\n');
    // 1.5 s falls inside the 20 Mbps step, so the window starts at 20, not 30.
    assert.deepStrictEqual(windowTrace(points, { start: 1.5, end: 3 }), [
        { time: 0, bandwidth: 20 }, { time: 0.5, bandwidth: 30 }
    ]);
});

test('rateAt is a step function', () => {
    const points = parseTrace('0,10\n2,20\n');
    assert.strictEqual(rateAt(points, 1.99), 10);
    assert.strictEqual(rateAt(points, 2), 20);
    assert.strictEqual(summarize(points).maxMbps, 20);
});

test('the burst matches the tbf the trace player installs', () => {
    assert.strictEqual(burstBytes(10), BURST_FLOOR_BYTES);
    assert.strictEqual(burstBytes(2000), 2000 * 125);
});

test('an unarmed shaper passes everything at once', async () => {
    const clock = virtualClock();
    const shaper = new TraceShaper(clock);
    await shaper.take(50 * MB);
    assert.strictEqual(clock.t, 1000);
    assert.strictEqual(shaper.status().shaped, false);
});

test('a transfer takes as long as the rate says, less the initial burst', async () => {
    const clock = virtualClock();
    const shaper = new TraceShaper(clock);
    shaper.arm(parseTrace('0,8\n'));                 // 8 Mbps = 1 MB/s
    await shaper.take(10 * MB);
    const expected = (10 * MB - BURST_FLOOR_BYTES) / 1e6;
    assert.ok(Math.abs((clock.t - 1000) - expected) < 1e-6,
        `took ${clock.t - 1000} s, expected ${expected}`);
});

test('a rate change mid-transfer is integrated, not averaged', async () => {
    const clock = virtualClock();
    const shaper = new TraceShaper(clock);
    // 1 MB/s for 2 s, then 4 MB/s.
    shaper.arm(parseTrace('0,8\n2,32\n'));
    await shaper.take(2 * MB + BURST_FLOOR_BYTES + 4 * MB);
    // 2 MB in the first two seconds (plus the burst), then 4 MB at 4 MB/s.
    assert.ok(Math.abs((clock.t - 1000) - 3) < 1e-6, `took ${clock.t - 1000} s`);
});

test('the trace is rebased to the trial start', () => {
    const clock = virtualClock(500);
    const shaper = new TraceShaper(clock);
    shaper.arm(parseTrace('0,10\n5,20\n'));
    clock.t = 504.9;
    assert.strictEqual(shaper.rateMbps(), 10);
    clock.t = 505;
    assert.strictEqual(shaper.rateMbps(), 20);
});

test('hold keeps the last rate after the trace ends; without it the link frees up', () => {
    const clock = virtualClock();
    const held = new TraceShaper(clock);
    held.arm(parseTrace('0,10\n2,20\n'), { hold: true });
    const freed = new TraceShaper(clock);
    freed.arm(parseTrace('0,10\n2,20\n'), { hold: false });
    clock.t += 60;
    assert.strictEqual(held.rateMbps(), 20);
    assert.strictEqual(freed.rateMbps(), null);
});

test('concurrent takers share one link, first come first served', async () => {
    const clock = virtualClock();
    const shaper = new TraceShaper(clock);
    shaper.arm(parseTrace('0,8\n'));
    const finished = [];
    await Promise.all([
        shaper.take(2 * MB).then(() => finished.push(['a', clock.t])),
        shaper.take(2 * MB).then(() => finished.push(['b', clock.t]))
    ]);
    assert.deepStrictEqual(finished.map(([name]) => name), ['a', 'b']);
    // Together they needed 4 MB at 1 MB/s, less one burst: sharing, not doubling.
    const total = finished[1][1] - 1000;
    assert.ok(Math.abs(total - (4 * MB - BURST_FLOOR_BYTES) / 1e6) < 1e-6,
        `both done after ${total} s`);
});

test('disarming releases whoever is waiting', async () => {
    const clock = virtualClock();
    const shaper = new TraceShaper({
        now: clock.now,
        sleep: async seconds => {
            clock.t += seconds;
            if (clock.t - 1000 > 1) shaper.disarm();
        }
    });
    shaper.arm(parseTrace('0,1\n'));                 // 125 kB/s: 100 MB is minutes
    await shaper.take(100 * MB);
    assert.ok(clock.t - 1000 < 2, 'the waiter was released when the trial ended');
});

test('re-arming forgets the previous trial', async () => {
    const clock = virtualClock();
    const shaper = new TraceShaper(clock);
    shaper.arm(parseTrace('0,8\n'));
    await shaper.take(5 * MB);
    shaper.arm(parseTrace('0,8\n'));
    assert.strictEqual(shaper.status().deliveredBytes, 0);
    assert.strictEqual(shaper.status().elapsedSeconds, 0);
});

test('pace() carries the bytes intact, and at the shaped rate', async () => {
    const clock = virtualClock();
    const shaper = new TraceShaper(clock);
    shaper.arm(parseTrace('0,8\n'));
    const source = new PassThrough();
    const paced = source.pipe(shaper.pace());
    const received = [];
    paced.on('data', chunk => received.push(chunk));
    const done = new Promise(resolve => paced.on('end', resolve));
    const payload = Buffer.alloc(3 * MB, 7);
    source.end(payload);
    await done;
    assert.ok(Buffer.concat(received).equals(payload));
    const expected = (3 * MB - BURST_FLOOR_BYTES) / 1e6;
    assert.ok(Math.abs((clock.t - 1000) - expected) < 1e-6, `took ${clock.t - 1000} s`);
});
