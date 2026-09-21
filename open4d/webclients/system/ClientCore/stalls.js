'use strict';

/**
 * Stall accounting.
 *
 * Extracted from system/Client/client.js (`getSegmentStallStatus`,
 * `processStallEvents`).
 *
 * Two distinct notions of "not playing" live here and must not be merged:
 *
 *   MISSING  unintentional starvation — a real stall, playback halted.
 *   FROZEN   a deliberate policy decision under bandwidth deficit: the object
 *            keeps showing its last frame while the rest of the scene plays.
 *
 * Frozen time is tracked separately and penalised less (see STALL_COSTS in
 * ClientCore/abr.js), so collapsing the two would misreport the system's
 * behaviour as worse than it is.
 */

/**
 * Per-segment stall data. RESETS the manager's counters, so call exactly once
 * per segment tick and before anything else that reads them.
 *
 * The manager returns `{ totalSegmentStallDuration, totalSegmentStallCount,
 * stallingCount, perObject: { obj: {stallTime, stallCount, ...} } }`.
 * Iterating the top-level object instead of `perObject` used to sum undefined
 * into NaN.
 */
function collectSegmentStalls(bufferManager) {
    const s = bufferManager.getAndResetSegmentStalls();

    const perObject = {};
    for (const [objName, stats] of Object.entries(s.perObject || {})) {
        perObject[objName] = {
            segmentStallDuration: stats.stallTime,
            segmentStallCount: stats.stallCount,
            isCurrentlyStalling: stats.isCurrentlyStalling,
            ongoingStallDuration: stats.currentOngoingStallDuration,
            segmentFrozenDuration: stats.frozenTime || 0,
            isFrozen: stats.isFrozen || false
        };
    }

    return {
        totalSegmentStallDuration: s.totalSegmentStallDuration || 0,
        totalSegmentStallCount: s.totalSegmentStallCount || 0,
        stallingCount: s.stallingCount || 0,
        perObject
    };
}

/**
 * Fold playback-tick stall events into the metrics record. Mutates `metrics`.
 *
 * @param {object} metrics
 * @param {Array<object>} events from bufferManager.onPlaybackTick
 * @param {number} timestamp ms since run start
 * @param {{info: Function, warn: Function, debug: Function}} log
 */
function applyStallEvents(metrics, events, timestamp, log) {
    for (const event of events) {
        if (!event) continue;

        const objName = event.objectName;

        if (event.type === 'start') {
            log.warn('STALL', `Stall start: ${objName}`, {
                buffer: event.bufferBefore?.toFixed(3),
                reason: event.reason || 'unknown'
            });

            if (!metrics.objectStalls[objName]) metrics.objectStalls[objName] = [];
            metrics.objectStalls[objName].push({
                startTime: timestamp, endTime: null, duration: 0
            });

        } else if (event.type === 'end') {
            const duration = event.stallDuration || 0;
            log.info('STALL', `Stall end: ${objName}`, {
                durationSec: duration.toFixed(3)
            });

            const objStalls = metrics.objectStalls[objName];
            if (objStalls && objStalls.length > 0) {
                const last = objStalls[objStalls.length - 1];
                if (last.endTime === null) {
                    last.endTime = timestamp;
                    last.duration = duration;
                }
            }

            metrics.summary.rebuffers++;
            metrics.summary.totalStallDuration += duration * 1000;

        } else if (event.type === 'ongoing') {
            log.debug('STALL', `Stall ongoing: ${objName}`, {
                durationSec: event.stallDuration?.toFixed(1)
            });
        }
    }
}

module.exports = { collectSegmentStalls, applyStallEvents };
