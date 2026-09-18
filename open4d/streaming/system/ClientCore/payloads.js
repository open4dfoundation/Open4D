'use strict';

/**
 * Server request bodies.
 *
 * Extracted from system/Client/client.js (`sendSegmentToServer`,
 * `sendDownloadCompleteToServer`, `sendBitrateCountsToServer`,
 * `enqueueRenderTraceUpload`). Pure builders: the transport is the platform's
 * job, the SHAPE is the core's.
 *
 * These are wire formats read by system/Server/server.js. Field names are
 * load-bearing — `estimatedBandwidth`, `segmentStallDurationSec` and the
 * per-object `decision` strings are all parsed by name on the server and by
 * vstream/evaluation/QoE*.py afterwards.
 */

/** Per-object block of the segment report: downloaded objects. */
function downloadedObjectEntries({ selection, bufferBefore, segmentStalls }) {
    return Object.fromEntries(
        Object.entries(selection.combo).map(([objName, rep]) => {
            const bufferObj = bufferBefore.objects.find(
                o => o.objectName === objName) || {};
            const objStall = segmentStalls.perObject[objName] || {
                segmentStallDuration: 0,
                segmentStallCount: 0
            };
            return [objName, {
                decision: 'download',
                repId: rep.id,
                weight: selection.metadata?.weights?.[objName] ?? null,
                bitrate: rep.predicted.bitrate_mbps,
                quality: rep.predicted.quality,
                bufferLevel: bufferObj.level ?? 0,
                bufferState: bufferObj.state ?? 'UNKNOWN',
                // Per-segment stall data only
                segmentStallDurationSec: objStall.segmentStallDuration,
                segmentStallCount: objStall.segmentStallCount,
                // Keep cumulative for reference
                totalStallDurationSec: bufferObj.totalStallTime ?? 0
            }];
        })
    );
}

/**
 * Per-object block: skipped and frozen objects.
 *
 * `decision` distinguishes a deliberate freeze under bandwidth deficit from a
 * high-buffer skip. Collapsing them would make a policy decision look like a
 * delivery failure in the QoE report.
 */
function skippedObjectEntries({ selection, bufferBefore, segmentStalls }) {
    return Object.fromEntries(
        (selection.skippedObjects || []).map(s => {
            const bufferObj = bufferBefore.objects.find(
                o => o.objectName === s.objectName) || {};
            const objStall = segmentStalls.perObject[s.objectName] || {};
            return [s.objectName, {
                decision: s.frozen ? 'freeze' : 'skip',
                reason: s.reason,
                weight: s.weight,
                repId: null,
                bitrate: 0,
                bufferLevel: bufferObj.level ?? 0,
                bufferState: bufferObj.state ?? 'UNKNOWN',
                segmentStallDurationSec: objStall.segmentStallDuration ?? 0,
                segmentFrozenDurationSec: objStall.segmentFrozenDuration ?? 0,
                totalStallDurationSec: bufferObj.totalStallTime ?? 0,
                totalFrozenTimeSec: bufferObj.totalFrozenTime ?? 0
            }];
        })
    );
}

/**
 * POST /api/segment/:id
 *
 * Sent BEFORE downloads start, so ladder generation for this segment's
 * viewpoint begins immediately rather than after the transfer.
 *
 * @param {object} args
 * @param {boolean} args.isEmpty no manifest yet — report the tick and nothing else
 * @param {object|null} args.data selection/buffer/budget/stalls, null when empty
 * @param {object|null} args.viewpoint included only on ladder-update segments
 */
function buildSegmentPayload({
    broadcastId, segmentId, timestamp, playbackTime, estimatedBandwidth,
    isEmpty, data, viewpoint, bufferLevels
}) {
    const payload = {
        broadcastId,
        segmentId,
        timestamp,
        playbackTime,
        estimatedBandwidth,
        isEmpty
    };
    if (isEmpty) return payload;

    payload.viewpoint = viewpoint;
    payload.selection = {
        segmentId,
        bufferLevels,
        minBufferLevel: data.bufferBefore.minBufferLevel,
        bitrateBudget: data.budget,
        totalBitrate: data.selection.totalBitrate,
        totalQuality: data.selection.totalQuality,
        algorithm: 'mckp-realtime-fixed-bw',
        estimatedBandwidth,
        playbackTime,
        segmentStallDurationSec: data.segmentStalls.totalSegmentStallDuration,
        segmentStallEvents: data.segmentStalls.totalSegmentStallCount,
        deficit: data.selection.deficit || false,
        frozenCount: (data.selection.frozenObjects || []).length,
        frozenObjects: data.selection.frozenObjects || [],
        objects: {
            ...downloadedObjectEntries({
                selection: data.selection,
                bufferBefore: data.bufferBefore,
                segmentStalls: data.segmentStalls
            }),
            ...skippedObjectEntries({
                selection: data.selection,
                bufferBefore: data.bufferBefore,
                segmentStalls: data.segmentStalls
            })
        }
    };
    return payload;
}

/** POST /api/segment/:id/download-complete */
function buildDownloadCompletePayload({
    broadcastId, segmentId, timestamp, estimatedBandwidth, downloadTimeMs,
    downloadSizeBytes, measuredBandwidthMbps, bufferAfter, isLate, usedFallback
}) {
    return {
        broadcastId,
        segmentId,
        downloadTimeMs,
        downloadSizeBytes,
        measuredBandwidthMbps,
        bufferAfter,
        isLate,
        usedFallback,
        estimatedBandwidth,
        timestamp
    };
}

/** POST /api/bitrate-counts-interval */
function buildBitrateCountsPayload({
    segmentId, timestamp, intervalMs, countsPerObject
}) {
    return {
        segmentId,
        timestamp,
        intervalMs,
        countsPerObject: { ...countsPerObject }
    };
}

/** POST /api/render-frames */
function buildRenderFramesPayload({ broadcastId, frames }) {
    return { broadcastId, frames };
}

/** POST /api/viewpoint */
function buildViewpointPayload({ broadcastId, viewpoint, segId }) {
    return { broadcastId, viewpoint, segId };
}

/**
 * POST /api/broadcast/start
 *
 * `sceneObjects` restricts the run to a subset of the server's object catalog.
 * An absent or empty list resets the server to its full catalog (it stores
 * whatever arrives and passes a non-empty list to the ladder service), so a
 * run never silently inherits the previous run's scene.
 *
 * This matters more than it looks: the ladder must publish at least one
 * representation per object in the scene, so the scene size sets an
 * irreducible bitrate floor. Nine ORBIT objects floor at ~116 Mbps, which no
 * ordinary link can carry, and every object the MCKP cannot afford is frozen.
 * Choosing the scene is therefore the difference between demonstrating
 * adaptation and demonstrating a permanent deficit.
 */
function buildBroadcastStartPayload({ clientMode, label, sceneObjects }) {
    const scene = Array.isArray(sceneObjects)
        ? sceneObjects.map(name => String(name).trim()).filter(Boolean) : [];
    return {
        algorithm: `mckp-abr-realtime-${clientMode}`,
        label: label || undefined,
        sceneObjects: scene.length ? scene : undefined
    };
}

module.exports = {
    buildSegmentPayload,
    buildDownloadCompletePayload,
    buildBitrateCountsPayload,
    buildRenderFramesPayload,
    buildViewpointPayload,
    buildBroadcastStartPayload,
    downloadedObjectEntries,
    skippedObjectEntries
};
