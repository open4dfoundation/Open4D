'use strict';

/**
 * Tests for the browser renderer's decoded-clip cache.
 *
 * This module exists because of a measured failure in the desktop client:
 * re-decoding 60 Draco frames plus a texture for every object of every segment
 * blew the 2 s segment budget, so objects never became playable and were
 * reported missing. The browser has less decode headroom, so these behaviours
 * are pinned rather than assumed.
 *
 * Run: node --test tests/test_decode_cache.js
 */

const assert = require('assert');
const test = require('node:test');

const { DecodeCache, clipKey } = require('../system/WebClient/src/decode-cache');

function clock(start = 0) {
    let t = start;
    return { now: () => ++t, peek: () => t };
}

function decoder(bytes = 1000, label = 'clip') {
    let calls = 0;
    return {
        get calls() { return calls; },
        fn: async () => { calls++; return { clip: { label, calls }, bytes }; }
    };
}

test('a clip is decoded once and then served from cache', async () => {
    const cache = new DecodeCache({ now: clock().now });
    const d = decoder();

    const first = await cache.get('dancer', 'r_res960', d.fn);
    assert.strictEqual(first.cacheHit, false);
    const second = await cache.get('dancer', 'r_res960', d.fn);
    assert.strictEqual(second.cacheHit, true);
    assert.strictEqual(d.calls, 1, 'the second request did not re-decode');
    assert.deepStrictEqual(cache.stats.hits, 1);
});

test('(objectName, repId) is the complete key', async () => {
    // Media is a fixed frame loop shared by every logical segment, so a clip
    // decoded during one segment stays valid in every later one. If the segment
    // id leaked into the key, every segment would re-decode.
    const cache = new DecodeCache({ now: clock().now });
    const d = decoder();
    await cache.get('dancer', 'rep-a', d.fn);
    await cache.get('dancer', 'rep-a', d.fn);
    await cache.get('dancer', 'rep-b', d.fn);
    await cache.get('mitch', 'rep-a', d.fn);
    assert.strictEqual(d.calls, 3, 'one decode per distinct (object, rep)');
    assert.strictEqual(clipKey('dancer', 'rep-a'), 'dancer::rep-a');
});

test('concurrent requests for one clip share a single decode', async () => {
    // Two segments selecting the same representation must not each decode 60
    // frames; this is what `decodeShared` reports in the desktop telemetry.
    let release;
    const gate = new Promise(resolve => { release = resolve; });
    let calls = 0;
    const decode = async () => {
        calls++;
        await gate;
        return { clip: { label: 'shared' }, bytes: 100 };
    };

    const cache = new DecodeCache({ now: clock().now });
    const a = cache.get('dancer', 'rep', decode);
    const b = cache.get('dancer', 'rep', decode);
    const c = cache.get('dancer', 'rep', decode);
    release();
    const [ra, rb, rc] = await Promise.all([a, b, c]);

    assert.strictEqual(calls, 1, 'exactly one decode ran');
    assert.strictEqual(ra.decodeShared, false, 'the first caller owns the decode');
    assert.ok(rb.decodeShared && rc.decodeShared);
    assert.strictEqual(ra.clip, rb.clip, 'all callers get the same clip');
    assert.strictEqual(cache.stats.shared, 2);
});

test('a failed decode is not cached and does not wedge the key', async () => {
    const cache = new DecodeCache({ now: clock().now });
    let attempt = 0;
    const decode = async () => {
        attempt++;
        if (attempt === 1) throw new Error('draco decode failed');
        return { clip: { label: 'ok' }, bytes: 10 };
    };

    await assert.rejects(() => cache.get('dancer', 'rep', decode));
    assert.strictEqual(cache.size, 0, 'nothing cached from a failed decode');

    const retry = await cache.get('dancer', 'rep', decode);
    assert.strictEqual(retry.clip.label, 'ok', 'the key is retryable');
});

test('eviction is least-recently-used once the byte budget is exceeded', async () => {
    const evicted = [];
    const cache = new DecodeCache({
        budgetBytes: 250, now: clock().now, onEvict: clip => evicted.push(clip.label)
    });

    await cache.get('a', 'r', decoder(100, 'a').fn);
    await cache.get('b', 'r', decoder(100, 'b').fn);
    // Touch 'a' so 'b' becomes the least recently used.
    await cache.get('a', 'r', decoder(100, 'a').fn);
    await cache.get('c', 'r', decoder(100, 'c').fn);

    assert.deepStrictEqual(evicted, ['b'], 'the coldest clip went first');
    assert.ok(cache.bytes <= 250);
    assert.ok(cache.has('a', 'r') && cache.has('c', 'r'));
});

test('eviction keeps going until the budget is met', async () => {
    const evicted = [];
    const cache = new DecodeCache({
        budgetBytes: 150, now: clock().now, onEvict: clip => evicted.push(clip.label)
    });
    await cache.get('a', 'r', decoder(100, 'a').fn);
    await cache.get('b', 'r', decoder(100, 'b').fn);
    await cache.get('c', 'r', decoder(100, 'c').fn);
    assert.ok(cache.bytes <= 150, `bytes=${cache.bytes}`);
    assert.strictEqual(cache.size, 1, 'only the newest clip survives');
});

test('a pinned clip is not evicted while it is on screen', async () => {
    // Evicting the clip being presented shows up as a frame reverting
    // mid-playback rather than as an error, so it must not happen.
    const cache = new DecodeCache({ budgetBytes: 150, now: clock().now });
    await cache.get('onscreen', 'r', decoder(100, 'onscreen').fn);
    cache.pin('onscreen', 'r');
    await cache.get('cold', 'r', decoder(100, 'cold').fn);

    assert.ok(cache.has('onscreen', 'r'), 'the pinned clip survived');
    assert.ok(!cache.has('cold', 'r'));
});

test('pinOnly moves the pin to the representation now in use', async () => {
    const cache = new DecodeCache({ now: clock().now });
    await cache.get('dancer', 'low', decoder(10, 'low').fn);
    await cache.get('dancer', 'high', decoder(10, 'high').fn);
    cache.pin('dancer', 'low');

    cache.pinOnly('dancer', 'high');
    // Squeeze the budget so only a pinned clip can survive.
    cache.budgetBytes = 10;
    await cache.get('other', 'r', decoder(10, 'other').fn);
    assert.ok(cache.has('dancer', 'high'), 'the new rep is protected');
    assert.ok(!cache.has('dancer', 'low'), 'the superseded rep is evictable again');
});

test('pinning an absent clip is a no-op rather than an error', () => {
    const cache = new DecodeCache({ now: clock().now });
    assert.doesNotThrow(() => cache.pin('ghost', 'r'));
    assert.doesNotThrow(() => cache.unpin('ghost', 'r'));
    assert.doesNotThrow(() => cache.pinOnly('ghost', 'r'));
});

test('clear releases every clip so GPU resources are freed', async () => {
    const released = [];
    const cache = new DecodeCache({
        now: clock().now, onEvict: clip => released.push(clip.label)
    });
    await cache.get('a', 'r', decoder(10, 'a').fn);
    await cache.get('b', 'r', decoder(10, 'b').fn);
    cache.clear();
    assert.strictEqual(cache.size, 0);
    assert.deepStrictEqual(released.sort(), ['a', 'b']);
});

test('a clip larger than the whole budget is still served once', async () => {
    // Degenerate but reachable at 1920-wide textures: the clip must be usable
    // for the frame that needed it rather than evicted before being returned.
    const cache = new DecodeCache({ budgetBytes: 100, now: clock().now });
    const result = await cache.get('huge', 'r', decoder(10_000, 'huge').fn);
    assert.ok(result.clip, 'the caller still gets its clip');
});
