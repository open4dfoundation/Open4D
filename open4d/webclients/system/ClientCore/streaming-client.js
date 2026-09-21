'use strict';

/**
 * The streaming client: segment loop, playback clock, download orchestration,
 * server reporting, shutdown.
 *
 * Moved from system/Client/client.js. Everything platform-shaped goes through
 * `platform` (see platform.js), so this same class drives the Node desktop
 * client and the browser client. Where the original used module-level `let`,
 * this holds instance state — the behaviour is otherwise intended to be
 * identical, and tests/test_client_core_parity.js pins the parts most likely
 * to drift.
 *
 * Two modes, both load-bearing:
 *   interactive  a renderer decodes and displays; objects become playable only
 *                on `object_ready`, and the camera comes from the live window.
 *   simulated    no renderer; segments are credited straight from the download
 *                result and canned viewpoints are cycled.
 * They measure different things. Do not collapse them.
 */

const { MCKPAdaptiveBitrate } = require('./abr');
const { BandwidthEstimator } = require('./bandwidth');
const { LinkThroughputMeter } = require('./link-throughput');
const { resolveStreamConfig } = require('./stream-config');
const { validatePlatform } = require('./platform');
const {
    planSegmentDownload, creditTaskResult, summarizeSegmentDownload,
    expectedSelectionBytes
} = require('./download-plan');
const {
    createMetricsRecord, recordBitrateSelection, buildSegmentRecord,
    buildBufferHistoryRecord, finalizeMetrics
} = require('./metrics');
const {
    buildSegmentPayload, buildDownloadCompletePayload, buildBitrateCountsPayload,
    buildRenderFramesPayload, buildViewpointPayload, buildBroadcastStartPayload
} = require('./payloads');
const { collectSegmentStalls, applyStallEvents } = require('./stalls');

/**
 * Tunables that were environment variables in the Node client. The adapter
 * reads its environment and passes them in; the core never looks at one.
 */
const DEFAULT_CONFIG = Object.freeze({
    clientMode: 'interactive',
    // One report per segment: the server-side ladder re-solves every segment
    // and uses the freshest report with segmentId <= t — at 10s it flew blind
    // for the first 5 segments and reused one stale snapshot for the rest of a
    // short run.
    bitrateReportIntervalMs: 2000,
    playbackTickIntervalMs: 100,
    // Bounded so a slow server cannot skew the tick clock.
    manifestFetchTimeoutMs: 500,
    // Dead-man fallback only; the manifest is refreshed at every segment tick.
    manifestPollIntervalMs: 10000,
    // Backpressure: never stack more than this many segment downloads. Without
    // a cap, a link slower than the selected ladder accumulates concurrent
    // downloads that share bandwidth, so every download gets slower each tick
    // (queueing death spiral) and the bandwidth estimate collapses.
    maxInflightSegments: 2,
    // Keep a continuous sliding window of requests in flight across ALL objects
    // of the segment, so the link never idles at object boundaries.
    downloadConcurrency: 10,
    // Fraction of the bandwidth ESTIMATE committed per segment. SHARED WITH THE
    // SERVER: vstream/config.py reads the same two values, because the ladder
    // service must know what the client can afford to decide whether the menu's
    // published floor is within budget.
    budgetMultiplier: 1,
    budgetMultiplierStruggling: 1,
    // How many segments each canned viewpoint is held (simulated mode).
    // 0 follows updateIntervalSegments.
    viewpointHoldSegments: 0,
    renderTraceBatchSize: 30,
    manifestDebugName: 'menu.json',
    renderTraceName: 'render_frames.jsonl',
    decodeEventName: 'decode_events.jsonl',
    runLabel: null,
    abr: {
        delta: 0.1,
        switchPenalty: 0.25,
        stallLambda: 20.0,
        stallGamma: 2.0,
        stallBeta: 1.3,
        maxBuffer: 15,
        minBuffer: 2,
        startupBuffer: 2
    }
});

class StreamingClient {
    /**
     * @param {object} args
     * @param {import('./platform').ClientPlatform} args.platform
     * @param {object} [args.config] overrides for DEFAULT_CONFIG
     */
    constructor({ platform, config = {} }) {
        this.config = { ...DEFAULT_CONFIG, ...config, abr: { ...DEFAULT_CONFIG.abr, ...(config.abr || {}) } };

        const mode = String(this.config.clientMode).toLowerCase();
        if (!['interactive', 'simulated'].includes(mode)) {
            throw new Error(
                `clientMode must be "interactive" or "simulated", got "${mode}"`);
        }
        this.config.clientMode = mode;
        this.interactive = mode === 'interactive';

        this.platform = validatePlatform(platform, {
            requireRenderer: this.interactive
        });

        const { clock } = this.platform;
        this.startTime = clock.now();

        // --- stream timing, from the server ---
        this.streamConfig = null;

        // --- run state ---
        this.broadcastId = null;
        this.viewpoints = [];
        this.currentViewpoint = null;
        this.currentSegmentId = 0;
        this.currentManifest = null;
        this.previousManifest = null;
        this.finishing = false;
        this._finishPromise = null;
        this.scratchRoot = null;
        this.lastDownloadLate = false;

        this.inFlightDownloads = new Map();
        this.pendingDownloads = new Map();
        this.intervalBitrateCounts = {};

        this.playbackState = { totalPlaybackTime: 0, lastTickTime: null };

        // --- timers ---
        this.segmentTimer = null;
        this.playbackTimer = null;
        this.manifestTimer = null;
        this.bitrateReportTimer = null;

        // --- renderer plumbing ---
        this.latestRendererCamera = null;
        this.rendererReadyBySegment = new Map();
        this.renderTraceBatch = [];
        this.renderTraceStream = null;
        this.decodeEventStream = null;
        this.renderUploadPromise = Promise.resolve();

        // The ABR gets the platform clock and logger too: its startup grace
        // period is measured in wall time, and its debug lines belong in the
        // run log rather than on stdout.
        this.abr = new MCKPAdaptiveBitrate(this.config.abr, {
            now: () => clock.now(),
            log: message => this._logInfo('ABR', message)
        });

        this.bandwidth = new BandwidthEstimator({
            elapsedMs: () => this._elapsed(),
            inFlightEntries: () => this.inFlightDownloads.entries(),
            signals: () => ({
                missingCount: this.abr.getBufferManager().getSummary().missingCount,
                inFlightCount: this.inFlightDownloads.size,
                maxInFlight: this.config.maxInflightSegments,
                lastDownloadLate: this.lastDownloadLate
            }),
            logger: {
                debug: (c, m, d) => this._log('DEBUG', c, m, d),
                warn: (c, m, d) => this._log('WARN', c, m, d)
            },
            now: () => clock.now(),
            initialEstimate: 5,
            multiplier: this.config.budgetMultiplier,
            multiplierStruggling: this.config.budgetMultiplierStruggling
        });

        this.linkMeter = new LinkThroughputMeter({
            // Read through a function: the interval arrives from the server
            // after this meter is constructed.
            segmentIntervalMs: () => this.streamConfig?.segmentIntervalMs ?? 0,
            onSample: (bytes, busyMs) => this.bandwidth.update(bytes, busyMs),
            now: () => clock.now()
        });

        this.metrics = createMetricsRecord({
            clientMode: mode,
            startTime: this.startTime,
            bandwidthHistory: this.bandwidth.history
        });
    }

    // ---------------------------------------------------------------- logging

    _elapsed() { return this.platform.clock.now() - this.startTime; }

    /**
     * The core formats the prefix because it owns the run clock, the playback
     * clock and the segment counter; the adapter only decides where the line
     * goes.
     */
    _log(level, component, message, data = null) {
        const elapsed = (this._elapsed() / 1000).toFixed(2);
        const playback = this.playbackState.totalPlaybackTime.toFixed(2);
        const prefix = `[wall:+${elapsed}s][play:${playback}s]`
            + `[seg:${this.currentSegmentId}][${level}][${component}]`;
        this.platform.logger.emit(level, `${prefix} ${message}`, data);
    }

    _logInfo(c, m, d = null) { this._log('INFO', c, m, d); }
    _logWarn(c, m, d = null) { this._log('WARN', c, m, d); }
    _logError(c, m, d = null) { this._log('ERROR', c, m, d); }
    _logDebug(c, m, d = null) { this._log('DEBUG', c, m, d); }

    get _log4() {
        return {
            info: (c, m, d) => this._logInfo(c, m, d),
            warn: (c, m, d) => this._logWarn(c, m, d),
            error: (c, m, d) => this._logError(c, m, d),
            debug: (c, m, d) => this._logDebug(c, m, d)
        };
    }

    // ------------------------------------------------------- renderer events

    _enqueueRenderTraceUpload(force = false) {
        if (!this.broadcastId || this.renderTraceBatch.length === 0) {
            return this.renderUploadPromise;
        }
        if (!force && this.renderTraceBatch.length < this.config.renderTraceBatchSize) {
            return this.renderUploadPromise;
        }
        const frames = this.renderTraceBatch.splice(0, this.renderTraceBatch.length);
        this.renderUploadPromise = this.renderUploadPromise.then(async () => {
            try {
                const res = await this.platform.transport.postJson('/api/render-frames',
                    buildRenderFramesPayload({ broadcastId: this.broadcastId, frames }));
                if (!res.ok) throw new Error(`HTTP ${res.status}`);
            } catch (err) {
                this._logError('RENDER',
                    `Failed to upload ${frames.length} frame records: ${err.message}`);
            }
        });
        return this.renderUploadPromise;
    }

    _onRendererFrame(event) {
        this.latestRendererCamera = event.camera || this.latestRendererCamera;
        const record = { ...event, clientElapsedMs: this._elapsed() };
        this.metrics.renderSummary.framesPresented++;
        this.metrics.renderSummary.framesDropped +=
            Number(event.droppedFramesBefore || 0);
        this.renderTraceStream?.write(JSON.stringify(record) + '\n');
        this.renderTraceBatch.push(record);
        this._enqueueRenderTraceUpload(false);
    }

    async _onRendererObjectReady(event) {
        const key = `${event.segmentId}:${event.objectName}`;
        const pending = this.rendererReadyBySegment.get(key);
        const record = {
            timestamp: this._elapsed(),
            segmentId: event.segmentId,
            objectName: event.objectName,
            repId: event.repId,
            status: event.type === 'object_ready' ? 'ready' : 'error',
            decodeMs: event.decodeMs ?? null,
            cacheHit: event.cacheHit === true,
            diskCacheHit: event.diskCacheHit === true,
            decodeShared: event.decodeShared === true,
            superseded: event.superseded === true,
            error: event.message ?? null
        };
        this.metrics.decodeEvents.push(record);
        // Also streamed out: decodeEvents used to be serialized only at the end
        // of a run, so an aborted run lost every decode timing and error — the
        // one telemetry needed to diagnose a starved renderer.
        this.decodeEventStream?.write(JSON.stringify(record) + '\n');

        if (event.type === 'object_ready' && pending && !pending.credited) {
            pending.credited = true;
            this.abr.onObjectDownloaded(
                event.objectName, this.streamConfig.segmentDuration);
            this._logInfo('DECODE', `${event.objectName} ready`, {
                segmentId: event.segmentId,
                repId: event.repId,
                decodeMs: Number(event.decodeMs || 0).toFixed(1),
                cache: event.cacheHit ? 'memory'
                    : (event.diskCacheHit ? 'disk' : 'decoded')
            });
        } else if (event.type === 'object_error' && event.superseded) {
            // Not a failure: a newer segment picked a different representation
            // before this decode started. No work was lost and the new rep is
            // already queued.
            this._logDebug('DECODE', `${event.objectName} superseded`, {
                segmentId: event.segmentId, repId: event.repId
            });
        } else if (event.type === 'object_error') {
            this.metrics.renderSummary.objectDecodeFailures++;
            this._logError('DECODE', `${event.objectName} failed: ${event.message}`, {
                segmentId: event.segmentId, repId: event.repId
            });
        }

        if (pending?.objectDir) {
            try {
                await this.platform.storage.release(pending.objectDir);
            } catch (err) {
                this._logWarn('CACHE',
                    `Could not release ${pending.objectDir}: ${err.message}`);
            }
        }
        this.rendererReadyBySegment.delete(key);
    }

    _onRendererClosed(event) {
        if (this.finishing) return;
        this.metrics.renderSummary.earlyWindowClose =
            this.currentSegmentId < (this.streamConfig?.totalSegments || Infinity);
        this._logInfo('RENDER', 'Window closed', event);
        this._stopSegmentTimer();
        this.finish();
    }

    // --------------------------------------------------------------- timers

    _startPlaybackTimer() {
        const { clock } = this.platform;
        this.playbackState.lastTickTime = clock.now();
        this.playbackState.totalPlaybackTime = 0;

        this._logInfo('PLAYBACK', 'Starting playback timer', {
            intervalMs: this.config.playbackTickIntervalMs
        });

        this.playbackTimer = clock.every(this.config.playbackTickIntervalMs, () => {
            const now = clock.now();
            const elapsedSec = (now - this.playbackState.lastTickTime) / 1000;
            this.playbackState.lastTickTime = now;
            this.playbackState.totalPlaybackTime += elapsedSec;

            const timestamp = now - this.startTime;
            const stallEvents = this.abr.onPlaybackTick(elapsedSec, timestamp);

            if (stallEvents && stallEvents.length > 0) {
                applyStallEvents(this.metrics, stallEvents, timestamp, this._log4);
            }
            if (this.platform.renderer) {
                const objects = Object.fromEntries(
                    this.abr.getBufferManager().getSummary().objects.map(obj => [
                        obj.objectName,
                        { state: obj.state, bufferLevel: obj.level }
                    ])
                );
                this.platform.renderer.setPlaybackState(
                    Math.max(0, this.currentSegmentId - 1), objects);
            }
        });
    }

    _stopPlaybackTimer() {
        if (this.playbackTimer) {
            this.platform.clock.cancel(this.playbackTimer);
            this.playbackTimer = null;
        }
    }

    _startSegmentTimer() {
        this._logInfo('SEGMENT', 'Starting segment timer', {
            intervalMs: this.streamConfig.segmentIntervalMs
        });
        this._processSegmentTick();
        this.segmentTimer = this.platform.clock.every(
            this.streamConfig.segmentIntervalMs, () => this._processSegmentTick());
    }

    _stopSegmentTimer() {
        if (this.segmentTimer) {
            this.platform.clock.cancel(this.segmentTimer);
            this.segmentTimer = null;
        }
    }

    // --------------------------------------------------------- segment tick

    async _processSegmentTick() {
        if (this.currentSegmentId >= this.streamConfig.totalSegments) {
            this._logInfo('SEGMENT', 'All segments complete, stopping...');
            this._stopSegmentTimer();
            this.finish();
            return;
        }

        // Refresh the manifest every tick — BEFORE the no-manifest guard, so a
        // client that started against an empty server picks up the bootstrap
        // ladder on the next tick instead of waiting for the slow fallback poll.
        await Promise.race([
            this._fetchManifest(),
            this.platform.clock.delay(this.config.manifestFetchTimeoutMs)
        ]);

        if (!this.currentManifest) {
            this._logWarn('SEGMENT', 'No manifest yet, sending empty segment');
            this._sendSegmentToServer(this.currentSegmentId, null, true);
            this.currentSegmentId++;
            return;
        }

        const segmentId = this.currentSegmentId;
        const timestamp = this._elapsed();

        // Per-segment stalls BEFORE processing: this RESETS the counters.
        const segmentStalls = collectSegmentStalls(this.abr.getBufferManager());

        if (this.interactive) {
            if (this.latestRendererCamera) {
                this.currentViewpoint = {
                    filename: 'interactive-camera',
                    data: this.latestRendererCamera
                };
            }
        } else {
            const hold = this.config.viewpointHoldSegments
                || this.streamConfig.updateIntervalSegments;
            if (segmentId % hold === 0) {
                this.currentViewpoint = this.viewpoints[
                    Math.floor(segmentId / hold) % this.viewpoints.length];
                this._logInfo('VIEWPOINT', 'Changed', {
                    filename: this.currentViewpoint.filename
                });
            }
        }

        const viewpointPriorities = extractViewpointPriorities(
            this.currentViewpoint.data);
        const bufferBefore = this.abr.getBufferManager().getSummary();
        const budget = this.bandwidth.budget();

        const selection = this.abr.selectBestCombination(
            this.currentManifest, budget, { viewpointPriorities });

        this._logInfo('SEGMENT', `>>> Tick ${segmentId}`, {
            bufferMin: bufferBefore.minBufferLevel.toFixed(2),
            estBW: this.bandwidth.estimate.toFixed(2),
            budget: budget.toFixed(2),
            bitrate: selection.totalBitrate.toFixed(2),
            inFlight: this.inFlightDownloads.size,
            segmentStallSec: segmentStalls.totalSegmentStallDuration.toFixed(3),
            ...(selection.deficit
                ? { deficit: true, frozen: selection.frozenObjects } : {}),
            stallStates: Object.fromEntries(
                bufferBefore.objects.map(o => [o.objectName, o.state]))
        });

        // Report BEFORE kicking off downloads so ladder generation for this
        // segment's viewpoint starts immediately.
        this._sendSegmentToServer(segmentId, {
            selection, bufferBefore, budget, timestamp, viewpointPriorities,
            segmentStalls
        }, false);

        if (this.inFlightDownloads.size >= this.config.maxInflightSegments) {
            // Link can't keep up with the segment clock: skip this segment's
            // download instead of queueing another concurrent transfer.
            // Playback stalls honestly on the starved buffers.
            this._logWarn('DOWNLOAD', `Skipping segment ${segmentId} download`, {
                inFlight: this.inFlightDownloads.size,
                maxInFlight: this.config.maxInflightSegments
            });
            this.metrics.summary.skippedDownloads =
                (this.metrics.summary.skippedDownloads || 0) + 1;
            // This path used to bypass the per-object bookkeeping inside
            // _startSegmentDownload, so the whole scene lost a segment's credit
            // with nothing but a counter to show for it.
            for (const objName of Object.keys(selection.combo)) {
                this.abr.getBufferManager().getBuffer(objName)
                    .skipSegment('download-skipped-backlog');
            }
        } else {
            this._startSegmentDownload(
                segmentId, selection, this.currentManifest, viewpointPriorities);
        }

        this._recordSegmentMetrics(
            segmentId, selection, bufferBefore, budget, timestamp, segmentStalls);

        this.currentSegmentId++;
    }

    // ------------------------------------------------------------ downloads

    _startSegmentDownload(segmentId, selection, manifest, viewpointPriorities) {
        const { clock } = this.platform;
        const downloadStart = clock.now();

        this.inFlightDownloads.set(segmentId, {
            startTime: downloadStart,
            bytesExpected: expectedSelectionBytes(selection)
        });

        const downloadPromise = (async () => {
            this.linkMeter.started();
            let linkAccounted = false;
            let effectiveSelection = selection;
            try {
                let downloadResult = await this._downloadSegmentFiles(
                    effectiveSelection, segmentId, manifest);
                let usedFallback = false;

                if (downloadResult.totalSize === 0 && this.previousManifest) {
                    this._logWarn('DOWNLOAD',
                        `Segment ${segmentId} failed, trying fallback`);
                    const prevSelection = this.abr.selectBestCombination(
                        this.previousManifest, this.bandwidth.budget(),
                        { viewpointPriorities });
                    downloadResult = await this._downloadSegmentFiles(
                        prevSelection, segmentId, this.previousManifest);
                    if (downloadResult.totalSize > 0) {
                        effectiveSelection = prevSelection;
                        usedFallback = true;
                        this.metrics.summary.fallbackCount++;
                    }
                }

                const downloadTimeMs = clock.now() - downloadStart;

                // bytes were already credited per-file by linkMeter.bytesDelivered
                this.linkMeter.finished();
                linkAccounted = true;

                const isLate = downloadTimeMs > this.streamConfig.segmentIntervalMs;
                this.lastDownloadLate = isLate;
                if (isLate) {
                    this.metrics.summary.lateDownloads++;
                    this._logWarn('DOWNLOAD', `Segment ${segmentId} download LATE`, {
                        downloadTimeMs,
                        expectedMs: this.streamConfig.segmentIntervalMs
                    });
                }

                await this._applyDownloadResult(
                    segmentId, effectiveSelection, downloadResult);

                const bufferAfter = this.abr.getBufferManager().getSummary();
                const measuredBW = downloadTimeMs > 0
                    ? (downloadResult.totalSize * 8) / downloadTimeMs / 1000
                    : 0;

                // Backfill the transferred size onto the segment record. It is
                // not known at tick time (the download has not run yet), and its
                // absence made every data-volume figure in the QoE report read 0.
                const segmentRecord = this.metrics.segments.find(
                    s => s.segmentId === segmentId);
                if (segmentRecord) {
                    segmentRecord.size = downloadResult.totalSize;
                    segmentRecord.downloadTimeMs = downloadTimeMs;
                    segmentRecord.measuredBandwidth = measuredBW;
                }

                this._logInfo('DOWNLOAD', `Segment ${segmentId} complete`, {
                    sizeMB: (downloadResult.totalSize / 1024 / 1024).toFixed(2),
                    timeMs: downloadTimeMs,
                    measuredBW: measuredBW.toFixed(2),
                    estBW: this.bandwidth.estimate.toFixed(2),
                    bufferAfter: bufferAfter.minBufferLevel.toFixed(2),
                    late: isLate,
                    fallback: usedFallback
                });

                this._sendDownloadCompleteToServer(segmentId, {
                    downloadTimeMs,
                    downloadSizeBytes: downloadResult.totalSize,
                    measuredBandwidthMbps: measuredBW,
                    bufferAfter,
                    isLate,
                    usedFallback
                });

                return { success: true, downloadTimeMs, downloadResult };

            } catch (err) {
                // partial bytes are already credited; just release the busy counter
                if (!linkAccounted) this.linkMeter.finished();
                this._logError('DOWNLOAD',
                    `Segment ${segmentId} failed: ${err.message}`);
                return { success: false, error: err.message };
            } finally {
                this.inFlightDownloads.delete(segmentId);
                this.pendingDownloads.delete(segmentId);
            }
        })();

        this.pendingDownloads.set(segmentId, downloadPromise);
    }

    /** Per-object bookkeeping and renderer staging once a segment has landed. */
    async _applyDownloadResult(segmentId, selection, downloadResult) {
        const downloadedObjects = {};
        const renderObjects = {};

        for (const [objName, rep] of Object.entries(selection.combo)) {
            const objResult = downloadResult.objects.find(
                o => o.objectName === objName);
            // Interactive mode used to demand all ~61 files, so one 404 or
            // timeout discarded a whole object-segment; the renderer now holds
            // the previous frame for any gap instead.
            const success = Boolean(objResult && objResult.success && objResult.size > 0);
            if (success && objResult.geometryDownloaded < objResult.geometryTotal) {
                this._logWarn('DOWNLOAD', `${objName} partial geometry`, {
                    segmentId,
                    got: objResult.geometryDownloaded,
                    want: objResult.geometryTotal
                });
            }
            downloadedObjects[objName] = { downloaded: success, repId: rep.id };
            recordBitrateSelection(
                this.metrics, this.intervalBitrateCounts, objName, rep.id);

            if (this.interactive && success) {
                const decodeDir = this.platform.storage.handle(
                    objResult.objectDir, 'decoded');
                this.rendererReadyBySegment.set(`${segmentId}:${objName}`, {
                    repId: rep.id,
                    objectDir: objResult.objectDir,
                    decodeDir,
                    credited: false
                });
                renderObjects[objName] = {
                    state: 'download',
                    repId: rep.id,
                    geometryFiles: objResult.geometryFiles,
                    textureFiles: objResult.textureFiles,
                    textureFile: objResult.textureFile,
                    decodeDir
                };
            } else if (this.interactive && objResult?.objectDir) {
                try {
                    await this.platform.storage.release(objResult.objectDir);
                } catch (err) {
                    this._logWarn('CACHE',
                        `Could not release failed download: ${err.message}`);
                }
            }
        }

        // Skipped/frozen objects: record the skip so per-object segment
        // accounting stays complete (the freeze itself was applied at selection
        // time).
        for (const s of (selection.skippedObjects || [])) {
            if (!downloadedObjects[s.objectName]) {
                downloadedObjects[s.objectName] = {
                    downloaded: false, skipped: true, reason: s.reason
                };
            }
        }

        if (this.interactive) {
            if (this.platform.renderer && Object.keys(renderObjects).length > 0) {
                this.platform.renderer.stageSegment(segmentId, renderObjects);
            }
            // Selected objects become playable only on object_ready.
            for (const [objName, info] of Object.entries(downloadedObjects)) {
                if (!info.downloaded) {
                    // Carry the real reason: a deliberate freeze or a
                    // high-buffer skip is not a download failure, and labelling
                    // all three the same hid which was which.
                    this.abr.getBufferManager().getBuffer(objName)
                        .skipSegment(info.reason || 'download-failed');
                }
            }
        } else {
            this.abr.onSegmentDownloaded(
                downloadedObjects, this.streamConfig.segmentDuration);
        }
    }

    async _downloadSegmentFiles(selection, segmentIdx, manifest) {
        const { clock, storage, transport } = this.platform;
        const downloadStart = clock.now();

        const { tasks, perObj } = planSegmentDownload({
            selection,
            manifest,
            framesPerSegment: this.streamConfig.framesPerSegment,
            interactive: this.interactive,
            objectDirFor: (objName, repId) => storage.handle(
                this.scratchRoot,
                `segment_${String(segmentIdx).padStart(4, '0')}`,
                objName,
                repId.replace(/[^a-zA-Z0-9._-]+/g, '-')
            ),
            destinationFor: (objName, objectDir, file) => (file.kind === 'texture'
                ? storage.handle(objectDir,
                    `texture_${String(file.textureIndex).padStart(4, '0')}.mp4`)
                : storage.handle(objectDir,
                    `geometry_${String(file.frameIndex).padStart(4, '0')}.drc`)),
            pathToUrl: assetPath => transport.assetUrl(assetPath)
        });

        // Drain the list with a sliding window: a new request starts the moment
        // one finishes, so the link never idles at batch or object boundaries.
        let next = 0;
        const worker = async () => {
            while (next < tasks.length) {
                const task = tasks[next++];
                const r = await transport.fetchAsset(
                    task.url, this.interactive ? task.destination : null);
                // Credit the link estimator as this file lands, so bytes and
                // busy time cover the same window even when segment downloads
                // overlap.
                this.linkMeter.bytesDelivered(r.size);
                creditTaskResult(perObj.get(task.objName), task, r, this.interactive);
            }
        };
        await Promise.all(Array.from(
            { length: Math.min(this.config.downloadConcurrency, tasks.length) },
            worker));

        const { totalSize, objects } = summarizeSegmentDownload(perObj);
        return { totalSize, totalTimeMs: clock.now() - downloadStart, objects };
    }

    // -------------------------------------------------------------- reports

    async _sendSegmentToServer(segmentId, data, isEmpty) {
        try {
            const updateInterval = this.streamConfig.updateIntervalSegments;
            const payload = buildSegmentPayload({
                broadcastId: this.broadcastId,
                segmentId,
                timestamp: this._elapsed(),
                playbackTime: this.playbackState.totalPlaybackTime,
                estimatedBandwidth: this.bandwidth.estimate,
                isEmpty,
                data,
                viewpoint: (segmentId % updateInterval === 0)
                    ? this.currentViewpoint.data : null,
                bufferLevels: this.abr.getBufferManager().getBufferLevels()
            });

            const res = await this.platform.transport.postJson(
                `/api/segment/${segmentId}`, payload);

            // Manifest staleness telemetry: how many segments behind the newest
            // server-side ladder this client is.
            const latest = res?.body?.latestManifestSegId;
            if (typeof latest === 'number') {
                const manifestSeg = this.currentManifest?.segment?.t ?? -1;
                this._logDebug('MANIFEST', 'Staleness', {
                    segId: segmentId,
                    latestManifestSegId: latest,
                    usingManifestSeg: manifestSeg,
                    behind: latest - manifestSeg
                });
            }

            this._logDebug('API', `Segment ${segmentId} sent to server`, { isEmpty });

        } catch (err) {
            this._logError('API',
                `Failed to send segment ${segmentId}: ${err.message}`);
        }
    }

    async _sendDownloadCompleteToServer(segmentId, data) {
        try {
            await this.platform.transport.postJson(
                `/api/segment/${segmentId}/download-complete`,
                buildDownloadCompletePayload({
                    broadcastId: this.broadcastId,
                    segmentId,
                    timestamp: this._elapsed(),
                    estimatedBandwidth: this.bandwidth.estimate,
                    ...data
                }));
        } catch (err) {
            this._logDebug('API',
                `Failed to send download complete for ${segmentId}`);
        }
    }

    async _sendBitrateCountsToServer() {
        if (Object.keys(this.intervalBitrateCounts).length === 0) return;
        try {
            await this.platform.transport.postJson('/api/bitrate-counts-interval',
                buildBitrateCountsPayload({
                    segmentId: this.currentSegmentId,
                    timestamp: this._elapsed(),
                    intervalMs: this.config.bitrateReportIntervalMs,
                    countsPerObject: this.intervalBitrateCounts
                }));
            this.intervalBitrateCounts = {};
        } catch (err) {
            this._logError('BITRATE', `Failed to send: ${err.message}`);
        }
    }

    _recordSegmentMetrics(
        segmentId, selection, bufferBefore, budget, timestamp, segmentStalls) {
        const bufferManager = this.abr.getBufferManager();
        this.metrics.segments.push(buildSegmentRecord({
            segmentId, selection, bufferBefore, budget, timestamp, segmentStalls,
            summary: bufferManager.getSummary(),
            playbackTime: this.playbackState.totalPlaybackTime,
            estimatedBandwidth: this.bandwidth.estimate,
            inFlightCount: this.inFlightDownloads.size,
            prevQualityByObj: this.abr.prevQualityByObj
        }));
        this.metrics.bufferHistory.push(buildBufferHistoryRecord({
            timestamp,
            playbackTime: this.playbackState.totalPlaybackTime,
            bufferBefore,
            segmentStalls
        }));
    }

    // ------------------------------------------------------------- manifest

    async _fetchStreamConfig() {
        const res = await this.platform.transport.getJson('/api/config');
        if (!res.ok) {
            throw new Error(`Config fetch failed: HTTP ${res.status}`);
        }
        this.streamConfig = resolveStreamConfig(res.body, this.streamConfig || {});
        this._logInfo('CONFIG', 'Loaded from server', {
            totalSegments: this.streamConfig.totalSegments,
            segmentDurationS: this.streamConfig.segmentDuration,
            framesPerSegment: this.streamConfig.framesPerSegment,
            segmentIntervalMs: this.streamConfig.segmentIntervalMs,
            updateIntervalSegments: this.streamConfig.updateIntervalSegments
        });
    }

    async _fetchManifest() {
        try {
            const res = await this.platform.transport.getJson('/api/manifest');
            if (!res.ok) return;
            const newManifest = res.body;
            const newSeg = newManifest?.segment?.t;
            const curSeg = this.currentManifest?.segment?.t;
            // Same ladder version: keep previousManifest meaningfully older (it
            // backs the download fallback path) and skip the write.
            if (this.currentManifest && newSeg === curSeg) return;
            if (this.currentManifest) this.previousManifest = this.currentManifest;
            this.currentManifest = newManifest;
            await this.platform.storage.writeText(
                this.config.manifestDebugName,
                JSON.stringify(this.currentManifest, null, 2));
            this._logInfo('MANIFEST', 'Updated', {
                manifestSeg: newSeg,
                objects: Object.keys(this.currentManifest.objects || {}).length,
                nFrames: this.streamConfig.framesPerSegment,
                segmentDurationS: this.streamConfig.segmentDuration
            });
        } catch (err) {
            this._logError('MANIFEST', `Fetch failed: ${err.message}`);
        }
    }

    // ------------------------------------------------------------ lifecycle

    /** Start streaming. Resolves once all timers are armed. */
    async run() {
        const { platform } = this;
        this._logInfo('STREAM',
            `=== VOLUMETRIC STREAMING (${this.config.clientMode.toUpperCase()}) ===`);

        platform.lifecycle.onShutdownRequest(() => {
            this._logInfo('STREAM', 'Shutdown requested, finalizing partial run');
            this.finish().catch(err => {
                this._logError('STREAM', `Shutdown failed: ${err.message}`);
                platform.lifecycle.exit(1);
            });
        });

        try {
            await this._fetchStreamConfig();

            this.viewpoints = await platform.viewpoints.list();
            this.currentViewpoint = this.viewpoints[0];
            if (this.interactive) {
                this.viewpoints = [this.currentViewpoint];
                this._logInfo('VIEWPOINT', 'Using startup pose', {
                    filename: this.currentViewpoint.filename,
                    behavior: 'live camera; canned viewpoints will not be cycled'
                });
            } else {
                this._logInfo('VIEWPOINT',
                    `Loaded ${this.viewpoints.length} simulation viewpoints`);
            }

            this.scratchRoot = await platform.storage.createScratch();

            if (this.interactive) {
                this.renderTraceStream = await platform.storage.openAppendStream(
                    this.config.renderTraceName);
                this.decodeEventStream = await platform.storage.openAppendStream(
                    this.config.decodeEventName);

                platform.renderer.setCallbacks({
                    onFrame: event => this._onRendererFrame(event),
                    onObjectReady: event => this._onRendererObjectReady(event),
                    onClosed: event => this._onRendererClosed(event),
                    onLog: (level, message, data) => this._log(
                        String(level || 'INFO').toUpperCase(), 'RENDER', message, data)
                });
                await platform.renderer.start({
                    fps: Math.round(this.streamConfig.framesPerSegment
                        / this.streamConfig.segmentDuration),
                    frameCount: this.streamConfig.framesPerSegment,
                    initialCamera: this.currentViewpoint.data
                });
                this.latestRendererCamera =
                    platform.renderer.latestCamera || this.currentViewpoint.data;
                this.currentViewpoint = {
                    filename: 'interactive-camera',
                    data: this.latestRendererCamera
                };
                this._logInfo('RENDER', 'Interactive window ready', {
                    scratchRoot: this.scratchRoot
                });
            }

            const started = await platform.transport.postJson('/api/broadcast/start',
                buildBroadcastStartPayload({
                    clientMode: this.config.clientMode,
                    label: this.config.runLabel,
                    sceneObjects: this.config.sceneObjects
                }));
            this.broadcastId = started?.body?.broadcastId ?? null;
            this.metrics.broadcastId = this.broadcastId;
            this._logInfo('STREAM', 'Broadcast started', {
                broadcastId: this.broadcastId
            });

            await platform.transport.postJson('/api/viewpoint',
                buildViewpointPayload({
                    broadcastId: this.broadcastId,
                    viewpoint: this.currentViewpoint.data,
                    segId: 0
                }));

            await this._fetchManifest();

            this.manifestTimer = platform.clock.every(
                this.config.manifestPollIntervalMs, () => this._fetchManifest());
            this.bitrateReportTimer = platform.clock.every(
                this.config.bitrateReportIntervalMs,
                () => this._sendBitrateCountsToServer());

            this._startPlaybackTimer();
            this._startSegmentTimer();

            this._logInfo('STREAM', 'All timers started - streaming in real-time');

        } catch (err) {
            this._logError('STREAM', `Fatal error: ${err.message}`,
                { stack: err.stack });
            this._stopPlaybackTimer();
            this._stopSegmentTimer();
            if (platform.renderer) await platform.renderer.stop();
            platform.lifecycle.exit(1);
        }
    }

    /**
     * Finalize: drain, upload, write metrics, release scratch, exit.
     *
     * Idempotent AND awaitable: repeated calls return the same promise, so a
     * caller that arrives during shutdown waits for the real completion instead
     * of racing past it. The original returned undefined on the second call,
     * which meant nothing could reliably await the upload of a partial run.
     */
    finish() {
        if (this._finishPromise) return this._finishPromise;
        this.finishing = true;
        this._finishPromise = this._finish();
        return this._finishPromise;
    }

    async _finish() {
        const { platform } = this;
        this._logInfo('STREAM', '=== FINISHING STREAM ===');
        this._stopSegmentTimer();
        this._stopPlaybackTimer();

        if (this.pendingDownloads.size > 0
            && !this.metrics.renderSummary.earlyWindowClose) {
            this._logInfo('STREAM',
                `Waiting for ${this.pendingDownloads.size} pending downloads...`);
            await Promise.all(this.pendingDownloads.values());
        } else if (this.pendingDownloads.size > 0) {
            this._logInfo('STREAM',
                `Discarding ${this.pendingDownloads.size} in-flight downloads `
                + 'after window close');
        }

        if (this.manifestTimer) platform.clock.cancel(this.manifestTimer);
        if (this.bitrateReportTimer) platform.clock.cancel(this.bitrateReportTimer);
        this.manifestTimer = null;
        this.bitrateReportTimer = null;

        await this._sendBitrateCountsToServer();
        await this._enqueueRenderTraceUpload(true);
        await this.renderUploadPromise;
        if (platform.renderer) await platform.renderer.stop();

        // Detach first, so a late frame can never write to a closing stream.
        const closing = [this.renderTraceStream, this.decodeEventStream]
            .filter(Boolean);
        this.renderTraceStream = null;
        this.decodeEventStream = null;
        for (const stream of closing) await stream.close();

        const bufferManager = this.abr.getBufferManager();
        const { finalStallMetrics, frozenMetrics } = finalizeMetrics({
            metrics: this.metrics,
            bufferManager,
            totalSegments: this.currentSegmentId,
            totalPlaybackTime: this.playbackState.totalPlaybackTime,
            totalWallTime: this._elapsed() / 1000,
            estimatedBandwidth: this.bandwidth.estimate,
            bandwidthSamples: this.bandwidth.samples,
            renderTraceFile: this.interactive ? this.config.renderTraceName : null,
            decodeEventFile: this.interactive ? this.config.decodeEventName : null
        });

        await platform.storage.writeResult(JSON.stringify(this.metrics, null, 2));

        try {
            await platform.transport.postJson('/api/results', this.metrics);
        } catch (err) {
            this._logError('RESULTS', `Failed to send: ${err.message}`);
        }

        if (this.scratchRoot) {
            try {
                await platform.storage.release(this.scratchRoot);
            } catch (err) {
                this._logWarn('CACHE',
                    `Could not remove ${this.scratchRoot}: ${err.message}`);
            }
        }

        this._logInfo('STREAM', '=== STREAM COMPLETE ===', {
            totalSegments: this.currentSegmentId,
            playbackTimeSec: this.playbackState.totalPlaybackTime.toFixed(2),
            wallTimeSec: (this._elapsed() / 1000).toFixed(2),
            totalStallsSec: (this.metrics.summary.totalStallDuration / 1000).toFixed(2),
            stallRatio: (this.playbackState.totalPlaybackTime > 0
                ? (finalStallMetrics.totalStallTime
                    / this.playbackState.totalPlaybackTime) * 100
                : 0).toFixed(2) + '%',
            totalFrozenSec: frozenMetrics.sumFrozenTime.toFixed(2),
            frozenSegments: frozenMetrics.frozenSegments,
            skippedDownloads: this.metrics.summary.skippedDownloads || 0,
            lateDownloads: this.metrics.summary.lateDownloads,
            fallbacks: this.metrics.summary.fallbackCount,
            finalBWEstimate: this.bandwidth.estimate.toFixed(2)
        });

        platform.lifecycle.exit(0);
    }
}

/**
 * Per-object viewpoint priorities as the ABR expects them.
 *
 * Defaults matter: an object the viewpoint feed says nothing about is treated
 * as visible at middling priority rather than dropped, so a partial feed
 * degrades the weighting instead of silently culling the scene.
 */
function extractViewpointPriorities(viewpointData) {
    const priorities = {};
    if (viewpointData?.objects) {
        for (const [objName, objData] of Object.entries(viewpointData.objects)) {
            priorities[objName] = {
                inFOV: objData.inFOV ?? true,
                priority: objData.priority ?? 3,
                distance: objData.distance ?? 5
            };
        }
    }
    return priorities;
}

module.exports = { StreamingClient, extractViewpointPriorities, DEFAULT_CONFIG };
