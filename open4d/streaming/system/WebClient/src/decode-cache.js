'use strict';

/**
 * Decoded-clip cache for the browser renderer.
 *
 * The browser analogue of system/Client/decode_cache.py, and it exists for the
 * same measured reason. The desktop client originally re-ran the Draco decoder
 * over 60 frames plus a texture decode for EVERY object of EVERY segment — an
 * order of magnitude more work than a 2 s segment budget allows. Objects then
 * never became playable and were reported missing. The browser has strictly
 * less decode headroom, so this is built in from the start rather than added
 * after the first starved run.
 *
 * `(objectName, repId)` is a COMPLETE cache key. Media is a fixed
 * FRAMES_PER_SEGMENT-frame loop published once under
 * `files/media/<obj>/<rep_id>/` and shared by every logical segment
 * (ladderlib.build_mpd writes `source_segment: 0, loop: true`), so a clip
 * decoded during segment 3 is still correct in segment 88.
 *
 * What is deliberately NOT cached: the downloads. The segment must keep
 * generating real network load or the shaped-bandwidth experiment stops meaning
 * anything. Only the decode is amortized.
 *
 * Eviction is by byte budget, least-recently-used first, because decoded frames
 * are what actually exhaust a browser tab: a 1920-wide texture frame is
 * w*h*1.5 = 5.5 MB however it is stored, so one 60-frame clip for five objects
 * is 1.66 GB. `vstream/config.py` caps published texture width for the same
 * reason.
 */

const DEFAULT_BUDGET_BYTES = 512 * 1024 * 1024;

/** `(objectName, repId)` -> cache key. */
function clipKey(objectName, repId) {
    return `${objectName}::${repId}`;
}

class DecodeCache {
    /**
     * @param {object} [options]
     * @param {number} [options.budgetBytes] evict once decoded bytes exceed this
     * @param {(clip: object) => void} [options.onEvict] release GPU resources
     * @param {() => number} [options.now] clock, for LRU ordering and tests
     */
    constructor({
        budgetBytes = DEFAULT_BUDGET_BYTES,
        onEvict = null,
        now = () => Date.now()
    } = {}) {
        this.budgetBytes = budgetBytes;
        this._onEvict = onEvict;
        this._now = now;
        /** key -> { clip, bytes, lastUsed, pinned } */
        this._entries = new Map();
        /** key -> Promise, so concurrent requests share one decode. */
        this._inFlight = new Map();
        this.stats = { hits: 0, misses: 0, shared: 0, evictions: 0 };
    }

    get bytes() {
        let total = 0;
        for (const entry of this._entries.values()) total += entry.bytes;
        return total;
    }

    get size() { return this._entries.size; }

    has(objectName, repId) { return this._entries.has(clipKey(objectName, repId)); }

    /**
     * Fetch a decoded clip, decoding it at most once.
     *
     * Concurrent callers for the same key await the SAME decode rather than
     * starting a second one. Without this, two segments selecting the same
     * representation would each decode 60 frames — the exact duplication that
     * `decodeShared` reports in the desktop client's telemetry.
     *
     * @param {string} objectName
     * @param {string} repId
     * @param {() => Promise<{clip: object, bytes: number}>} decode
     * @returns {Promise<{clip: object, cacheHit: boolean, decodeShared: boolean}>}
     */
    async get(objectName, repId, decode) {
        const key = clipKey(objectName, repId);

        const entry = this._entries.get(key);
        if (entry) {
            entry.lastUsed = this._now();
            this.stats.hits++;
            return { clip: entry.clip, cacheHit: true, decodeShared: false };
        }

        const pending = this._inFlight.get(key);
        if (pending) {
            this.stats.shared++;
            const clip = await pending;
            return { clip, cacheHit: false, decodeShared: true };
        }

        this.stats.misses++;
        const work = (async () => {
            const { clip, bytes } = await decode();
            this._entries.set(key, {
                clip, bytes, lastUsed: this._now(), pinned: false
            });
            this._evictToBudget();
            return clip;
        })();
        this._inFlight.set(key, work);
        try {
            const clip = await work;
            return { clip, cacheHit: false, decodeShared: false };
        } finally {
            this._inFlight.delete(key);
        }
    }

    /**
     * Protect a clip from eviction while it is on screen.
     *
     * Without pinning, a large scene can evict the clip the renderer is in the
     * middle of presenting, which shows up as a frame reverting mid-playback
     * rather than as an error.
     */
    pin(objectName, repId) {
        const entry = this._entries.get(clipKey(objectName, repId));
        if (entry) entry.pinned = true;
    }

    unpin(objectName, repId) {
        const entry = this._entries.get(clipKey(objectName, repId));
        if (entry) entry.pinned = false;
    }

    /** Unpin every clip of an object except the one now in use. */
    pinOnly(objectName, repId) {
        for (const [key, entry] of this._entries) {
            if (key.startsWith(`${objectName}::`)) {
                entry.pinned = (key === clipKey(objectName, repId));
            }
        }
    }

    _evictToBudget() {
        if (this.bytes <= this.budgetBytes) return;
        // Least-recently-used first, pinned clips last-resort only.
        const candidates = [...this._entries.entries()]
            .filter(([, entry]) => !entry.pinned)
            .sort((a, b) => a[1].lastUsed - b[1].lastUsed);

        for (const [key, entry] of candidates) {
            if (this.bytes <= this.budgetBytes) break;
            this._entries.delete(key);
            this.stats.evictions++;
            this._onEvict?.(entry.clip);
        }
    }

    /** Drop everything, releasing GPU resources. */
    clear() {
        for (const entry of this._entries.values()) this._onEvict?.(entry.clip);
        this._entries.clear();
    }
}

module.exports = { DecodeCache, clipKey, DEFAULT_BUDGET_BYTES };
