'use strict';

/**
 * The run's metrics record and its builders.
 *
 * Extracted from system/Client/client.js (`metrics`, `recordSegmentMetrics`,
 * `recordBitrateSelection`, and the summary fill-in at the end of
 * `finishStream`). Pure: every builder is a function of its arguments, so the
 * serialized shape can be tested without running a stream.
 *
 * The shape is a published interface, not an internal detail —
 * system/Client/calculate-qoe.js, plot-qoe.py and the server's /api/results
 * consumer all read these fields by name. Renaming one silently empties a
 * column in the QoE report rather than raising anything.
 */

/**
 * @param {object} args
 * @param {string} args.clientMode 'interactive' | 'simulated'
 * @param {number} args.startTime run epoch, from platform.clock.now()
 * @param {Array} args.bandwidthHistory the estimator's OWN array, shared by
 *   reference so its appends land here without re-assignment
 */
function createMetricsRecord({ clientMode, startTime, bandwidthHistory }) {
    return {
        startTime,
        clientMode,
        broadcastId: null,
        segments: [],
        stalls: [],
        objectStalls: {},
        summary: {
            totalSegments: 0,
            rebuffers: 0,
            totalStallDuration: 0,
            perObjectStalls: {},
            fallbackCount: 0,
            lateDownloads: 0,
            missedSegments: 0
        },
        bitrateRequestCounts: {},
        bitrateRequestCountsPerObject: {},
        bufferHistory: [],
        bandwidthHistory,
        decodeEvents: [],
        renderSummary: {
            framesPresented: 0,
            framesDropped: 0,
            objectDecodeFailures: 0,
            earlyWindowClose: false
        }
    };
}

/**
 * Count one requested representation, both cumulatively and for the current
 * reporting interval. `intervalCounts` is mutated and periodically drained by
 * the /api/bitrate-counts-interval reporter.
 */
function recordBitrateSelection(metrics, intervalCounts, objectName, repId) {
    if (!metrics.bitrateRequestCountsPerObject[objectName]) {
        metrics.bitrateRequestCountsPerObject[objectName] = {};
    }
    metrics.bitrateRequestCountsPerObject[objectName][repId] =
        (metrics.bitrateRequestCountsPerObject[objectName][repId] || 0) + 1;
    metrics.bitrateRequestCounts[repId] = (metrics.bitrateRequestCounts[repId] || 0) + 1;

    if (!intervalCounts[objectName]) {
        intervalCounts[objectName] = {};
    }
    intervalCounts[objectName][repId] = (intervalCounts[objectName][repId] || 0) + 1;
}

/**
 * Per-object decisions for QoE.
 *
 * calculate-qoe.js reads `seg.objectQualities`: downloaded objects carry the
 * selected rep quality; frozen objects carry the LAST SHOWN quality with
 * bufferState FROZEN once their buffer runs dry — a frozen object is still
 * showing a frame, so scoring it as zero quality would double-count the freeze
 * penalty.
 */
function buildObjectQualities({ selection, bufferBefore, prevQualityByObj }) {
    const prioByObj = Object.fromEntries(
        (selection.objectSelection?.priorities || []).map(p => [p.objectName, p]));
    const objectQualities = [];

    for (const [objName, rep] of Object.entries(selection.combo)) {
        const bufObj = bufferBefore.objects.find(o => o.objectName === objName) || {};
        const p = prioByObj[objName] || {};
        objectQualities.push({
            objectName: objName,
            decision: 'download',
            repId: rep.id,
            quality: rep.predicted.quality,
            bitrate: rep.predicted.bitrate_mbps,
            weight: selection.metadata?.weights?.[objName] ?? null,
            priority: p.viewpointPriority ?? 3,
            inFOV: p.inFOV !== false,
            bufferState: bufObj.state ?? 'OK',
            bufferLevel: bufObj.level ?? 0
        });
    }
    for (const s of (selection.skippedObjects || [])) {
        const bufObj = bufferBefore.objects.find(o => o.objectName === s.objectName) || {};
        const p = prioByObj[s.objectName] || {};
        objectQualities.push({
            objectName: s.objectName,
            decision: s.frozen ? 'freeze' : 'skip',
            reason: s.reason,
            repId: null,
            quality: prevQualityByObj.get(s.objectName) ?? s.quality ?? 0,
            bitrate: 0,
            weight: s.weight ?? null,
            priority: p.viewpointPriority ?? 3,
            inFOV: p.inFOV !== false,
            bufferState: bufObj.state ?? 'OK',
            bufferLevel: bufObj.level ?? 0
        });
    }
    return objectQualities;
}

/** One row of metrics.segments. */
function buildSegmentRecord({
    segmentId, selection, bufferBefore, budget, timestamp, segmentStalls,
    summary, playbackTime, estimatedBandwidth, inFlightCount, prevQualityByObj
}) {
    return {
        segmentId,
        timestamp,
        playbackTime,
        selectedBitrate: selection.totalBitrate,
        budget,
        estimatedBandwidth,
        minBufferLevel: bufferBefore.minBufferLevel,
        avgBufferLevel: bufferBefore.avgBufferLevel,
        missingCount: bufferBefore.missingCount,
        frozenCount: (selection.frozenObjects || []).length,
        frozenObjects: selection.frozenObjects || [],
        deficit: selection.deficit || false,
        inFlightDownloads: inFlightCount,
        objectQualities: buildObjectQualities({
            selection, bufferBefore, prevQualityByObj
        }),

        // PER-SEGMENT stall data (not cumulative)
        segmentStallDurationSec: segmentStalls.totalSegmentStallDuration,
        segmentStallCount: segmentStalls.totalSegmentStallCount,
        stallingObjectCount: segmentStalls.stallingCount,
        perObjectSegmentStalls: segmentStalls.perObject,

        // CUMULATIVE totals (for reference)
        cumulativeTotalStallSec: summary.objects.reduce(
            (sum, o) => sum + o.totalStallTime, 0),
        cumulativeTotalFrozenSec: summary.objects.reduce(
            (sum, o) => sum + (o.totalFrozenTime || 0), 0)
    };
}

/** One row of metrics.bufferHistory. */
function buildBufferHistoryRecord({
    timestamp, playbackTime, bufferBefore, segmentStalls
}) {
    return {
        timestamp,
        playbackTime,
        avgLevel: bufferBefore.avgBufferLevel,
        minLevel: bufferBefore.minBufferLevel,
        missingCount: bufferBefore.missingCount,
        stallingCount: segmentStalls.stallingCount,
        segmentStallDurationSec: segmentStalls.totalSegmentStallDuration,
        // calculate-qoe.js's analyzeBufferHistory expects this; without it the
        // whole QoE report threw and no *_qoe.txt was ever produced.
        objects: bufferBefore.objects.map(o => ({
            objectName: o.objectName,
            level: o.level,
            state: o.state
        }))
    };
}

/**
 * Fill in metrics.summary at the end of a run. Mutates and returns `metrics`.
 *
 * @param {object} args
 * @param {object} args.metrics
 * @param {object} args.bufferManager the ABR's buffer manager
 * @param {number} args.totalSegments segments actually ticked
 * @param {number} args.totalPlaybackTime seconds
 * @param {number} args.totalWallTime seconds
 * @param {number} args.estimatedBandwidth final estimate
 * @param {number[]} args.bandwidthSamples final sample window
 * @param {string|null} args.renderTraceFile
 * @param {string|null} args.decodeEventFile
 */
function finalizeMetrics({
    metrics, bufferManager, totalSegments, totalPlaybackTime, totalWallTime,
    estimatedBandwidth, bandwidthSamples, renderTraceFile, decodeEventFile
}) {
    const finalStallMetrics = bufferManager.getTotalStallMetrics();

    metrics.summary.totalSegments = totalSegments;
    metrics.summary.totalPlaybackTime = totalPlaybackTime;
    metrics.summary.totalWallTime = totalWallTime;
    metrics.summary.totalStallDuration = finalStallMetrics.totalStallTime * 1000;
    metrics.summary.rebuffers = finalStallMetrics.totalStallEvents;
    metrics.summary.finalEstimatedBandwidth = estimatedBandwidth;
    metrics.summary.bandwidthSamples = [...bandwidthSamples];
    metrics.summary.renderTraceFile = renderTraceFile;
    metrics.summary.decodeEventFile = decodeEventFile;

    const frozenMetrics = bufferManager.getTotalFrozenMetrics();
    metrics.summary.totalFrozenTime = frozenMetrics.sumFrozenTime;
    metrics.summary.frozenSegments = frozenMetrics.frozenSegments;
    metrics.summary.perObjectFrozen = frozenMetrics.perObject;

    for (const [objName, buffer] of bufferManager.buffers.entries()) {
        const status = buffer.getStatus();
        metrics.summary.perObjectStalls[objName] = {
            count: buffer.stallEvents.length,
            totalDuration: status.totalStallTime,
            totalFrozenTime: status.totalFrozenTime,
            frozenSegments: status.frozenSegments,
            finalState: status.state
        };
    }

    return { metrics, finalStallMetrics, frozenMetrics };
}

module.exports = {
    createMetricsRecord,
    recordBitrateSelection,
    buildObjectQualities,
    buildSegmentRecord,
    buildBufferHistoryRecord,
    finalizeMetrics
};
