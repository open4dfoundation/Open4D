// MCKP DP-based ABR with per-object buffers (was Client/client-abr-mckp.js)
// V5.1: Fixed budget usage - don't skip objects when bandwidth is abundant

// MISSING = unintentional starvation (real stall, playback halted for the
// object). FROZEN = deliberate policy decision under bandwidth deficit: the
// object keeps showing its last frame while the rest of the scene plays.
// Frozen time is tracked separately from stall time and penalized less, but
// the penalty grows with frozen duration so long-frozen objects get
// reconsidered when bandwidth allows.
'use strict';

/**
 * Both platform seams are mandatory throughout this module — see the note in
 * each constructor. Failing at construction is deliberate: the alternative is a
 * test that appears to pass while reading wall time.
 */
function requireSeam(value, name) {
    if (typeof value !== 'function') {
        throw new TypeError(
            `MCKP ABR requires a ${name} function (pass platform.clock.now / a logger)`);
    }
    return value;
}

const STALL_COSTS = { 'OK': 0.0, 'MISSING': 1.0, 'FROZEN': 0.3 };

class ObjectBuffer {
    constructor(objectName, maxBufferSec = 20, minBufferSec = 3, deps = {}) {
        // Platform seams, both REQUIRED. A Date.now default would let a test
        // driving a virtual clock silently measure wall time (the startup grace
        // period below is wall-time based), and a console default would keep a
        // platform global in ClientCore forever. Callers pass platform.clock.now
        // and a logger; see ClientCore/platform.js.
        this._now = requireSeam(deps.now, 'now');
        this._log = requireSeam(deps.log, 'log');
        this.objectName = objectName;
        this.maxBuffer = maxBufferSec;
        this.minBuffer = minBufferSec;
        this.level = 0;
        this.state = 'OK';
        this.stallDuration = 0;
        this.totalStallTime = 0;
        this.stallEvents = [];
        this.initialized = false;
        this.segmentsDownloaded = 0;
        this.segmentsConsumed = 0;
        this.stallStartTimestamp = null;
        this.everHadContent = false;
        this.segmentStallTime = 0;
        this.segmentStallCount = 0;
        this.segmentsSkipped = 0;
        this.lastSkipReason = null;
        this.independentPlaybackTime = 0;
        // Deliberate freeze (bandwidth-deficit policy) vs unintentional stall
        this.frozen = false;
        this.frozenDuration = 0;      // current continuous frozen time
        this.totalFrozenTime = 0;
        this.segmentFrozenTime = 0;
        this.frozenSegments = 0;
    }

    freeze(reason = 'bandwidth-deficit') {
        if (!this.frozen) this.frozenDuration = 0;
        this.frozen = true;
        this.lastSkipReason = reason;
        this.frozenSegments++;
    }

    unfreeze() {
        this.frozen = false;
        this.frozenDuration = 0;
        if (this.state === 'FROZEN') {
            this.state = this.level > 0 ? 'OK' : 'MISSING';
        }
    }

    addSegment(segmentDuration = 1.0) {
        const prevLevel = this.level;
        const wasStalling = this.state === 'MISSING';
        
        this.level = Math.min(this.maxBuffer, this.level + segmentDuration);
        this.initialized = true;
        this.everHadContent = true;
        this.segmentsDownloaded++;
        this.unfreeze(); // fresh content ends a deliberate freeze
        
        let stallEndEvent = null;
        
        if (wasStalling && this.level > 0) {
            stallEndEvent = this._endStall(this._now());
        }
        
        return { 
            prevLevel, 
            newLevel: this.level, 
            added: segmentDuration,
            stallEndEvent 
        };
    }

    skipSegment(reason = 'low-priority') {
        this.segmentsSkipped++;
        this.lastSkipReason = reason;
        return { skipped: true, reason, level: this.level };
    }

    consume(elapsedWallTime, timestamp = 0) {
        this.segmentsConsumed++;

        const prevLevel = this.level;
        const prevState = this.state;

        if (this.level <= 0) {
            if (this.frozen) {
                this._recordFrozenTime(elapsedWallTime);
                return null; // deliberate freeze: last frame shown, no stall event
            }
            const stallTime = elapsedWallTime;
            this._recordStallTime(stallTime);

            if (prevState === 'OK') {
                return this._startStallEvent(timestamp, prevLevel, stallTime, 'buffer-empty');
            }

            return this._reportOngoingStall(timestamp, stallTime);
        }

        const playableTime = Math.min(this.level, elapsedWallTime);
        const stallTime = elapsedWallTime - playableTime;

        this.level = Math.max(0, this.level - playableTime);
        this.independentPlaybackTime += playableTime;

        if (stallTime <= 0) {
            if (prevState === 'MISSING') {
                return this._endStall(timestamp);
            }
            // playing buffered content counts as OK even under a freeze
            // decision; FROZEN only applies once the buffer runs dry
            this.state = 'OK';
            return null;
        }

        if (this.frozen) {
            this._recordFrozenTime(stallTime);
            return null;
        }

        this._recordStallTime(stallTime);

        if (prevState === 'OK') {
            return this._startStallEvent(timestamp, prevLevel, stallTime, 'buffer-depleted');
        }

        return this._reportOngoingStall(timestamp, stallTime);
    }

    _recordStallTime(stallTime) {
        this.state = 'MISSING';
        this.stallDuration += stallTime;
        this.totalStallTime += stallTime;
        this.segmentStallTime += stallTime;
    }

    _recordFrozenTime(frozenTime) {
        this.state = 'FROZEN';
        this.frozenDuration += frozenTime;
        this.totalFrozenTime += frozenTime;
        this.segmentFrozenTime += frozenTime;
    }

    _startStallEvent(timestamp, bufferBefore, stallTime, reason) {
        this.stallStartTimestamp = timestamp;
        this.segmentStallCount++;
        
        this.stallEvents.push({
            startTime: timestamp,
            endTime: null,
            duration: 0,
            objectName: this.objectName,
            reason: this.everHadContent ? reason : 'no-data-received'
        });
        
        return {
            objectName: this.objectName,
            timestamp,
            type: 'start',
            bufferBefore,
            stallTime,
            reason: this.everHadContent ? reason : 'no-data-received'
        };
    }

    _reportOngoingStall(timestamp, stallTime) {
        const prevSeconds = Math.floor(this.stallDuration - stallTime);
        const currSeconds = Math.floor(this.stallDuration);
        
        if (currSeconds > prevSeconds) {
            return {
                objectName: this.objectName,
                timestamp,
                type: 'ongoing',
                stallDuration: this.stallDuration,
                stallTime,
                bufferLevel: this.level
            };
        }
        return null;
    }

    _endStall(timestamp) {
        const stallInfo = {
            objectName: this.objectName,
            timestamp,
            type: 'end',
            stallDuration: this.stallDuration,
            bufferAfter: this.level
        };
        
        if (this.stallEvents.length > 0) {
            const lastEvent = this.stallEvents[this.stallEvents.length - 1];
            if (lastEvent.endTime === null) {
                lastEvent.endTime = timestamp;
                lastEvent.duration = this.stallDuration;
            }
        }
        
        this.stallDuration = 0;
        this.stallStartTimestamp = null;
        this.state = 'OK';
        
        return stallInfo;
    }

    getSegmentStallStats() {
        return {
            stallTime: this.segmentStallTime,
            stallCount: this.segmentStallCount,
            isCurrentlyStalling: this.state === 'MISSING',
            currentOngoingStallDuration: this.stallDuration,
            bufferLevel: this.level,
            totalStallTime: this.totalStallTime,
            frozenTime: this.segmentFrozenTime,
            isFrozen: this.state === 'FROZEN' || this.frozen
        };
    }

    resetSegmentStalls() {
        this.segmentStallTime = 0;
        this.segmentStallCount = 0;
        this.segmentFrozenTime = 0;
    }

    getStatus() {
        return {
            objectName: this.objectName,
            level: this.level,
            state: this.state,
            stallDuration: this.stallDuration,
            totalStallTime: this.totalStallTime,
            stallEventCount: this.stallEvents.length,
            isHealthy: this.level >= this.minBuffer,
            isCritical: this.level < 1.0,
            initialized: this.initialized,
            everHadContent: this.everHadContent,
            segmentsDownloaded: this.segmentsDownloaded,
            segmentsConsumed: this.segmentsConsumed,
            segmentsSkipped: this.segmentsSkipped,
            segmentStallTime: this.segmentStallTime,
            segmentStallCount: this.segmentStallCount,
            independentPlaybackTime: this.independentPlaybackTime,
            frozen: this.frozen,
            frozenDuration: this.frozenDuration,
            totalFrozenTime: this.totalFrozenTime,
            frozenSegments: this.frozenSegments
        };
    }

    reset() {
        this.level = 0;
        this.state = 'OK';
        this.stallDuration = 0;
        this.totalStallTime = 0;
        this.stallEvents = [];
        this.initialized = false;
        this.everHadContent = false;
        this.segmentsDownloaded = 0;
        this.segmentsConsumed = 0;
        this.segmentsSkipped = 0;
        this.stallStartTimestamp = null;
        this.segmentStallTime = 0;
        this.segmentStallCount = 0;
        this.lastSkipReason = null;
        this.independentPlaybackTime = 0;
    }
}

class PerObjectBufferManager {
    constructor(config = {}, deps = {}) {
        // Platform seams, both REQUIRED. A Date.now default would let a test
        // driving a virtual clock silently measure wall time (the startup grace
        // period below is wall-time based), and a console default would keep a
        // platform global in ClientCore forever. Callers pass platform.clock.now
        // and a logger; see ClientCore/platform.js.
        this._now = requireSeam(deps.now, 'now');
        this._log = requireSeam(deps.log, 'log');
        this._deps = deps;
        this.maxBuffer = config.maxBuffer ?? 15;
        this.minBuffer = config.minBuffer ?? 2;
        this.buffers = new Map();
        this.playbackStarted = false;
        this.startupBufferTarget = config.startupBuffer ?? 2;
        this.segmentCount = 0;
        this.totalPlaybackTime = 0;
        this.firstSegmentRequestTime = null;
        this.startupGracePeriod = config.startupGracePeriod ?? 3;
    }

    getBuffer(objectName) {
        if (!this.buffers.has(objectName)) {
            this.buffers.set(objectName, new ObjectBuffer(
                objectName, 
                this.maxBuffer, 
                this.minBuffer,
                this._deps
            ));
        }
        return this.buffers.get(objectName);
    }

    initFromManifest(manifest) {
        for (const objName of Object.keys(manifest.objects)) {
            this.getBuffer(objName);
        }
        if (this.firstSegmentRequestTime === null) {
            this.firstSegmentRequestTime = this._now();
        }
    }

    addSegmentForObject(objectName, segmentDuration = 1.0) {
        const buffer = this.getBuffer(objectName);
        const result = buffer.addSegment(segmentDuration);
        return { [objectName]: result, stallEndEvent: result.stallEndEvent };
    }

    addDownloadedSegments(downloadedObjects, segmentDuration = 1.0) {
        const results = {};
        const stallEndEvents = [];
        
        for (const [objName, info] of Object.entries(downloadedObjects)) {
            const buffer = this.getBuffer(objName);
            if (info.downloaded) {
                const result = buffer.addSegment(segmentDuration);
                results[objName] = result;
                if (result.stallEndEvent) {
                    stallEndEvents.push(result.stallEndEvent);
                }
            } else if (info.skipped) {
                results[objName] = buffer.skipSegment(info.reason || 'low-priority');
            } else {
                results[objName] = { prevLevel: buffer.level, newLevel: buffer.level, added: 0 };
            }
        }
        this.segmentCount++;
        return { results, stallEndEvents };
    }

    shouldStartPlayback() {
        if (this.playbackStarted) return true;
        
        if (this.firstSegmentRequestTime !== null) {
            const elapsed = (this._now() - this.firstSegmentRequestTime) / 1000;
            if (elapsed >= this.startupGracePeriod) {
                this._log(`[BUFFER] Starting playback after ${elapsed.toFixed(1)}s grace period`);
                this.playbackStarted = true;
                return true;
            }
        }
        
        const minLevel = this.getMinBufferLevel();
        if (minLevel >= this.startupBufferTarget) {
            this._log(`[BUFFER] Starting playback with ${minLevel.toFixed(2)}s buffer`);
            this.playbackStarted = true;
            return true;
        }
        return false;
    }

    consumeAll(elapsedWallTime = 1.0, timestamp = 0) {
        if (!this.shouldStartPlayback()) return [];
        
        this.totalPlaybackTime += elapsedWallTime;
        const stallEvents = [];
        
        for (const buffer of this.buffers.values()) {
            const event = buffer.consume(elapsedWallTime, timestamp);
            if (event) stallEvents.push(event);
        }
        
        return stallEvents;
    }

    getSegmentStallStatus() {
        let totalSegmentStallDuration = 0;
        let totalSegmentStallCount = 0;
        let stallingCount = 0;
        const perObject = {};
        
        for (const [name, buffer] of this.buffers.entries()) {
            const stats = buffer.getSegmentStallStats();
            perObject[name] = stats;
            totalSegmentStallDuration += stats.stallTime;
            totalSegmentStallCount += stats.stallCount;
            if (stats.isCurrentlyStalling) stallingCount++;
        }
        
        return { 
            totalSegmentStallDuration, 
            totalSegmentStallCount, 
            stallingCount, 
            perObject 
        };
    }

    resetAllSegmentStalls() {
        for (const buffer of this.buffers.values()) {
            buffer.resetSegmentStalls();
        }
    }

    getAndResetSegmentStalls() {
        const status = this.getSegmentStallStatus();
        this.resetAllSegmentStalls();
        return status;
    }

    getStallStates() {
        const states = {};
        for (const [name, buffer] of this.buffers.entries()) {
            states[name] = buffer.state;
        }
        return states;
    }

    getStallDurations() {
        const durations = {};
        for (const [name, buffer] of this.buffers.entries()) {
            durations[name] = buffer.stallDuration;
        }
        return durations;
    }

    getTotalStallTimes() {
        const times = {};
        for (const [name, buffer] of this.buffers.entries()) {
            times[name] = buffer.totalStallTime;
        }
        return times;
    }

    getFrozenDurations() {
        const durations = {};
        for (const [name, buffer] of this.buffers.entries()) {
            durations[name] = buffer.frozenDuration;
        }
        return durations;
    }

    getTotalFrozenMetrics() {
        let sumFrozenTime = 0;
        let frozenSegments = 0;
        const perObject = {};
        for (const buffer of this.buffers.values()) {
            perObject[buffer.objectName] = {
                totalFrozenTime: buffer.totalFrozenTime,
                frozenSegments: buffer.frozenSegments
            };
            sumFrozenTime += buffer.totalFrozenTime;
            frozenSegments += buffer.frozenSegments;
        }
        return { sumFrozenTime, frozenSegments, perObject };
    }

    getBufferLevels() {
        const levels = {};
        for (const [name, buffer] of this.buffers.entries()) {
            levels[name] = buffer.level;
        }
        return levels;
    }

    getMinBufferLevel({ excludeFrozen = false } = {}) {
        let min = Infinity;
        for (const buffer of this.buffers.values()) {
            if (excludeFrozen && buffer.frozen) continue;
            min = Math.min(min, buffer.level);
        }
        return min === Infinity ? 0 : min;
    }

    getBufferLevel(objectName) {
        const buffer = this.buffers.get(objectName);
        return buffer ? buffer.level : 0;
    }

    getSummary() {
        const statuses = Array.from(this.buffers.values()).map(b => b.getStatus());
        const missingCount = statuses.filter(s => s.state === 'MISSING').length;
        const frozenCount = statuses.filter(s => s.state === 'FROZEN' || s.frozen).length;
        const criticalCount = statuses.filter(s => s.isCritical).length;
        const avgLevel = statuses.length > 0 
            ? statuses.reduce((sum, s) => sum + s.level, 0) / statuses.length 
            : 0;
        
        return {
            totalObjects: statuses.length,
            missingCount,
            frozenCount,
            criticalCount,
            avgBufferLevel: avgLevel,
            minBufferLevel: statuses.length > 0 ? Math.min(...statuses.map(s => s.level)) : 0,
            maxBufferLevel: statuses.length > 0 ? Math.max(...statuses.map(s => s.level)) : 0,
            allHealthy: missingCount === 0 && criticalCount === 0,
            playbackStarted: this.playbackStarted,
            segmentCount: this.segmentCount,
            totalPlaybackTime: this.totalPlaybackTime,
            perObjectStallTimes: Object.fromEntries(
                statuses.map(s => [s.objectName, s.totalStallTime])
            ),
            objects: statuses
        };
    }

    getTotalStallMetrics() {
        let maxStallTime = 0;
        let sumStallTime = 0;
        let totalStallEvents = 0;
        const perObject = {};
        
        for (const buffer of this.buffers.values()) {
            const objStallTime = buffer.totalStallTime;
            const completedEvents = buffer.stallEvents.filter(e => e.endTime !== null).length;
            const ongoingEvent = buffer.state === 'MISSING' ? 1 : 0;
            
            perObject[buffer.objectName] = {
                totalStallTime: objStallTime,
                stallEventCount: completedEvents + ongoingEvent
            };
            
            maxStallTime = Math.max(maxStallTime, objStallTime);
            sumStallTime += objStallTime;
            totalStallEvents += completedEvents + ongoingEvent;
        }
        
        return { 
            totalStallTime: maxStallTime,
            sumStallTime,
            avgStallTime: this.buffers.size > 0 ? sumStallTime / this.buffers.size : 0,
            totalStallEvents,
            perObject
        };
    }

    reset() {
        for (const buffer of this.buffers.values()) {
            buffer.reset();
        }
        this.playbackStarted = false;
        this.segmentCount = 0;
        this.totalPlaybackTime = 0;
        this.firstSegmentRequestTime = null;
    }
}

class MCKPAdaptiveBitrate {
    constructor(config = {}, deps = {}) {
        // Platform seams, both REQUIRED. A Date.now default would let a test
        // driving a virtual clock silently measure wall time (the startup grace
        // period below is wall-time based), and a console default would keep a
        // platform global in ClientCore forever. Callers pass platform.clock.now
        // and a logger; see ClientCore/platform.js.
        this._now = requireSeam(deps.now, 'now');
        this._log = requireSeam(deps.log, 'log');
        this.delta = config.delta ?? 0.1;
        this.switchPenalty = config.switchPenalty ?? 0.25;
        this.stallLambda = config.stallLambda ?? 20.0;
        this.stallGamma = config.stallGamma ?? 2.0;
        this.stallBeta = config.stallBeta ?? 1.3;
        
        this.enablePriorityDropping = config.enablePriorityDropping ?? true;
        this.minObjectsToDownload = config.minObjectsToDownload ?? 1;
        this.bufferThresholdForDropping = config.bufferThresholdForDropping ?? 3.0;
        this.highBufferThreshold = config.highBufferThreshold ?? 5.0;
        this.abundantBandwidthMultiplier = config.abundantBandwidthMultiplier ?? 1.5; // NEW
        
        this.bufferManager = new PerObjectBufferManager({
            maxBuffer: config.maxBuffer ?? 15,
            minBuffer: config.minBuffer ?? 2,
            startupBuffer: config.startupBuffer ?? 2,
            startupGracePeriod: config.startupGracePeriod ?? 3
        }, deps);
        
        this.prevRepByObj = new Map();
        this.prevQualityByObj = new Map();
    }

    getWeightsFromManifest(manifest) {
        const weights = {};
        for (const [objName, objData] of Object.entries(manifest.objects)) {
            weights[objName] = objData.weight || 1.0;
        }
        return weights;
    }

    calculateStallPenalty(weights) {
        if (this.stallLambda === 0) return { total: 0, perObject: {} };
        
        const stallStates = this.bufferManager.getStallStates();
        const stallDurations = this.bufferManager.getStallDurations();
        const frozenDurations = this.bufferManager.getFrozenDurations();
        const totalStallTimes = this.bufferManager.getTotalStallTimes();

        let totalPenalty = 0;
        const perObject = {};

        for (const [objName, weight] of Object.entries(weights)) {
            const state = stallStates[objName] || 'OK';
            // FROZEN uses its own (continuous) duration and a lower cost:
            // deliberate freezes are cheaper than real stalls, but the
            // penalty still grows so long-frozen objects get unfrozen when
            // bandwidth allows.
            const currentDuration = state === 'FROZEN'
                ? (frozenDurations[objName] || 0)
                : (stallDurations[objName] || 0);
            const totalDuration = totalStallTimes[objName] || 0;
            const cost = STALL_COSTS[state] || 0;

            let penalty = 0;
            if (cost > 0 && currentDuration > 0) {
                penalty = this.stallLambda *
                    Math.pow(weight * cost, this.stallGamma) *
                    Math.pow(currentDuration, this.stallBeta);
            }
            
            perObject[objName] = { 
                state, 
                currentDuration,
                totalDuration,
                cost, 
                penalty 
            };
            totalPenalty += penalty;
        }
        return { total: totalPenalty, perObject };
    }

    calculateSwitchPenalty(objName, newQuality, weight) {
        if (this.switchPenalty === 0) return 0;
        const prevQuality = this.prevQualityByObj.get(objName) || 0;
        const drop = Math.max(0, prevQuality - newQuality);
        return weight * this.switchPenalty * drop;
    }

    calculateEffectivePriority(objName, viewpointPriorities, weights) {
        const vp = viewpointPriorities[objName] || { inFOV: true, priority: 3, distance: 5 };
        const weight = weights[objName] || 1.0;
        const bufferLevel = this.bufferManager.getBufferLevel(objName);
        
        let priority = 10 - (vp.priority || 3);
        
        if (vp.inFOV) priority += 5;
        priority += weight * 2;
        
        if (bufferLevel < 1.0) priority += 10;
        else if (bufferLevel < 2.0) priority += 5;
        else if (bufferLevel < 3.0) priority += 2;
        
        if (bufferLevel > 8.0) priority -= 3;
        else if (bufferLevel > 5.0) priority -= 1;
        
        priority -= Math.min(5, (vp.distance || 5) / 2);
        
        return {
            objectName: objName,
            effectivePriority: priority,
            inFOV: vp.inFOV,
            viewpointPriority: vp.priority,
            distance: vp.distance,
            weight,
            bufferLevel
        };
    }

    // FIXED: Don't skip objects when bandwidth is abundant
    selectObjectsToDownload(manifest, budget, viewpointPriorities) {
        const weights = this.getWeightsFromManifest(manifest);
        const objectNames = Object.keys(manifest.objects);
        
        // Calculate minimum total bitrate needed (lowest rep for each object)
        const minReps = {};
        let minTotalBitrate = 0;
        
        for (const [objName, objData] of Object.entries(manifest.objects)) {
            const minRep = objData.representations.reduce((a, b) => 
                a.predicted.bitrate_mbps < b.predicted.bitrate_mbps ? a : b
            );
            minReps[objName] = minRep;
            minTotalBitrate += minRep.predicted.bitrate_mbps;
        }
        
        // Check if we have abundant bandwidth (>1.5x minimum needed)
        const abundantBandwidth = budget > (minTotalBitrate * this.abundantBandwidthMultiplier);
        // Sustained deficit: even the cheapest full-scene ladder exceeds the
        // budget. Some objects must be frozen (keep showing their last frame)
        // so the rest fit.
        const deficit = budget < minTotalBitrate;

        const priorities = objectNames.map(objName =>
            this.calculateEffectivePriority(objName, viewpointPriorities, weights)
        );

        priorities.sort((a, b) => b.effectivePriority - a.effectivePriority);

        const selectedObjects = [];
        const skippedObjects = [];
        let usedBudget = 0;

        if (deficit) {
            // Freeze order = ascending manifest weight (the server-computed
            // viewpoint importance): keep the highest-weight objects live,
            // freeze from the least important upward until the rest fit.
            const byWeight = [...objectNames].sort((a, b) => {
                const dw = (weights[b] || 0) - (weights[a] || 0);
                if (dw !== 0) return dw;
                const pa = priorities.find(p => p.objectName === a)?.effectivePriority || 0;
                const pb = priorities.find(p => p.objectName === b)?.effectivePriority || 0;
                return pb - pa;
            });

            for (const objName of byWeight) {
                const minRep = minReps[objName];
                const minBitrate = minRep.predicted.bitrate_mbps;
                const mustInclude = selectedObjects.length < this.minObjectsToDownload;
                const fitsInBudget = (usedBudget + minBitrate) <= budget;

                if (mustInclude || fitsInBudget) {
                    selectedObjects.push(objName);
                    usedBudget += minBitrate;
                    this.bufferManager.getBuffer(objName).unfreeze();
                } else {
                    skippedObjects.push({
                        objectName: objName,
                        reason: 'frozen-bandwidth-deficit',
                        frozen: true,
                        weight: weights[objName] || 0,
                        bufferLevel: this.bufferManager.getBufferLevel(objName),
                        repId: minRep.id,
                        bitrate: minBitrate,
                        quality: minRep.predicted.quality
                    });
                    this.bufferManager.getBuffer(objName).freeze('bandwidth-deficit');
                }
            }
        } else {
            for (const p of priorities) {
                const objName = p.objectName;
                const minRep = minReps[objName];
                const minBitrate = minRep.predicted.bitrate_mbps;

                const mustInclude = selectedObjects.length < this.minObjectsToDownload;
                const bufferLow = p.bufferLevel < this.highBufferThreshold;
                const fitsInBudget = (usedBudget + minBitrate) <= budget;

                if (mustInclude || (fitsInBudget && (bufferLow || abundantBandwidth))) {
                    selectedObjects.push(objName);
                    usedBudget += minBitrate;
                    this.bufferManager.getBuffer(objName).unfreeze();
                } else {
                    // High buffer with tight (but sufficient) bandwidth, or a
                    // straggler that no longer fits: skip this segment. Not a
                    // freeze - the object still has buffer to play.
                    skippedObjects.push({
                        objectName: objName,
                        reason: fitsInBudget ? 'high-buffer' : 'budget-exceeded',
                        frozen: false,
                        weight: weights[objName] || 0,
                        priority: p.effectivePriority,
                        bufferLevel: p.bufferLevel,
                        repId: minRep.id,
                        bitrate: minBitrate,
                        quality: minRep.predicted.quality
                    });
                }
            }
        }

        return {
            selectedObjects,
            skippedObjects,
            frozenObjects: skippedObjects.filter(s => s.frozen).map(s => s.objectName),
            priorities,
            usedBudget,
            totalBudget: budget,
            abundantBandwidth,
            deficit,
            minTotalBitrate
        };
    }

    solveMCKP_DP(keptRepsByObj, weights, budgetMbps) {
        const objNames = Object.keys(keptRepsByObj);
        const n = objNames.length;
        
        if (n === 0) return { totalBitrate: 0, totalValue: 0, choices: {} };
        
        // One quantization for budget and item costs (was 100 vs a shadowed
        // 10 inside the loop, inflating the DP budget 10x and misreporting
        // the selected bitrate).
        const SCALE = Math.round(1 / this.delta); // e.g. 10 for delta=0.1
        const maxBudget = Math.floor(budgetMbps * SCALE);
        
        const minPossibleBitrate = objNames.reduce((sum, objName) => {
            const reps = keptRepsByObj[objName];
            const minRep = reps.reduce((a, b) => 
                a.predicted.bitrate_mbps < b.predicted.bitrate_mbps ? a : b
            );
            return sum + minRep.predicted.bitrate_mbps;
        }, 0);
        
        if (budgetMbps < minPossibleBitrate) {
            const choices = {};
            let usedBudget = 0;
            let totalValue = 0;
            
            for (const objName of objNames) {
                const reps = keptRepsByObj[objName];
                const lowest = reps.reduce((a, b) => 
                    a.predicted.bitrate_mbps < b.predicted.bitrate_mbps ? a : b
                );
                choices[objName] = lowest;
                usedBudget += lowest.predicted.bitrate_mbps;
                totalValue += (weights[objName] || 1.0) * lowest.predicted.quality;
            }
            return { totalBitrate: usedBudget, totalValue, choices };
        }
        
        let dp = new Map();
        dp.set(maxBudget, { value: 0, choices: {} });
        
        for (const objName of objNames) {
            const reps = keptRepsByObj[objName];
            const w = weights[objName] || 1.0;
            const newDp = new Map();
            
            for (const [remainBudget, state] of dp.entries()) {
                for (const rep of reps) {
                    const bitrate = Math.floor(rep.predicted.bitrate_mbps * SCALE + 1e-9);
                    
                    if (bitrate <= remainBudget) {
                        const quality = rep.predicted.quality;
                        const switchPen = this.calculateSwitchPenalty(objName, quality, w);
                        const repValue = w * quality - switchPen;
                        const newBudget = remainBudget - bitrate;
                        const newValue = state.value + repValue;
                        
                        const existing = newDp.get(newBudget);
                        if (!existing || newValue > existing.value) {
                            newDp.set(newBudget, {
                                value: newValue,
                                choices: { ...state.choices, [objName]: rep }
                            });
                        }
                    }
                }
            }
            
            dp = newDp;

        }
        
        let bestValue = -Infinity;
        let bestChoices = null;
        let usedBudget = 0;
        
        for (const [remainBudget, state] of dp.entries()) {
            if (Object.keys(state.choices).length === n && state.value > bestValue) {
                bestValue = state.value;
                bestChoices = state.choices;
                usedBudget = (maxBudget - remainBudget) / SCALE;
            }
        }
        
        if (!bestChoices) {
            bestChoices = {};
            usedBudget = 0;
            bestValue = 0;
            
            for (const objName of objNames) {
                const reps = keptRepsByObj[objName];
                const lowest = reps.reduce((a, b) => 
                    a.predicted.bitrate_mbps < b.predicted.bitrate_mbps ? a : b
                );
                bestChoices[objName] = lowest;
                usedBudget += lowest.predicted.bitrate_mbps;
                bestValue += (weights[objName] || 1.0) * lowest.predicted.quality;
            }
        }
        
        return { totalBitrate: usedBudget, totalValue: bestValue, choices: bestChoices };
    }

    selectBestCombination(manifest, bitrateBudget, options = {}) {
        const { viewpointPriorities = {} } = options;
        
        this.bufferManager.initFromManifest(manifest);
        
        const weights = this.getWeightsFromManifest(manifest);
        
        let safetyMargin = 1;
        const safeBudget = bitrateBudget * safetyMargin;
        
        let objectSelection = null;
        let keptRepsByObj = {};
        
        if (this.enablePriorityDropping) {
            objectSelection = this.selectObjectsToDownload(manifest, safeBudget, viewpointPriorities);
            
            for (const objName of objectSelection.selectedObjects) {
                keptRepsByObj[objName] = manifest.objects[objName].representations;
            }
            
            const frozen = objectSelection.skippedObjects.filter(s => s.frozen);
            const softSkips = objectSelection.skippedObjects.filter(s => !s.frozen);

            if (frozen.length > 0) {
                const detail = frozen.map(s =>
                    `${s.objectName}(w:${(s.weight || 0).toFixed(3)},buf:${s.bufferLevel.toFixed(1)})`
                ).join(', ');
                this._log(`     [FREEZE] Bandwidth deficit (budget ${safeBudget.toFixed(1)} < min ladder ${objectSelection.minTotalBitrate.toFixed(1)} Mbps): freezing ${frozen.length} lowest-weight objects: ${detail}`);
            }
            if (softSkips.length > 0) {
                const skipReasons = softSkips.map(s =>
                    `${s.objectName}(buf:${s.bufferLevel.toFixed(1)},${s.reason})`
                ).join(', ');
                this._log(`     [PRIORITY-DROP] Skipping ${softSkips.length} objects: ${skipReasons}`);
            }
        } else {
            for (const [objName, objData] of Object.entries(manifest.objects)) {
                keptRepsByObj[objName] = objData.representations;
            }
        }
        
        const stallPenalties = this.calculateStallPenalty(weights);
        
        const { totalBitrate, totalValue, choices } = this.solveMCKP_DP(
            keptRepsByObj, weights, safeBudget
        );
        
        for (const [objName, rep] of Object.entries(choices)) {
            this.prevRepByObj.set(objName, rep.id);
            this.prevQualityByObj.set(objName, rep.predicted.quality);
        }
        
        const totalQuality = Object.values(choices).reduce(
            (sum, rep) => sum + rep.predicted.quality, 0
        );
        
        return {
            combo: choices,
            totalBitrate,
            totalQuality,
            skippedObjects: objectSelection?.skippedObjects || [],
            frozenObjects: objectSelection?.frozenObjects || [],
            deficit: objectSelection?.deficit || false,
            objectSelection,
            metadata: {
                algorithm: 'mckp-dp-adaptive-budget-v5.2-freeze',
                stallPenalties,
                adjustedValue: totalValue - stallPenalties.total,
                weights,
                safetyMargin,
                safeBudget,
                bufferSummary: this.bufferManager.getSummary(),
                priorityDropping: this.enablePriorityDropping,
                abundantBandwidth: objectSelection?.abundantBandwidth || false,
                minTotalBitrate: objectSelection?.minTotalBitrate || 0
            }
        };
    }

    onSegmentDownloaded(downloadedObjects, segmentDuration = 1.0) {
        return this.bufferManager.addDownloadedSegments(downloadedObjects, segmentDuration);
    }

    onObjectDownloaded(objectName, segmentDuration = 1.0) {
        return this.bufferManager.addSegmentForObject(objectName, segmentDuration);
    }

    onPlaybackTick(playbackTime = 1.0, timestamp = 0) {
        return this.bufferManager.consumeAll(playbackTime, timestamp);
    }

    getBufferManager() {
        return this.bufferManager;
    }

    reset() {
        this.prevRepByObj.clear();
        this.prevQualityByObj.clear();
        this.bufferManager.reset();
    }

    getState() {
        return {
            prevReps: Object.fromEntries(this.prevRepByObj),
            bufferSummary: this.bufferManager.getSummary(),
            config: {
                delta: this.delta,
                switchPenalty: this.switchPenalty,
                stallLambda: this.stallLambda,
                stallGamma: this.stallGamma,
                stallBeta: this.stallBeta,
                enablePriorityDropping: this.enablePriorityDropping,
                minObjectsToDownload: this.minObjectsToDownload,
                bufferThresholdForDropping: this.bufferThresholdForDropping,
                highBufferThreshold: this.highBufferThreshold,
                abundantBandwidthMultiplier: this.abundantBandwidthMultiplier
            }
        };
    }
}

module.exports = { 
    MCKPAdaptiveBitrate, 
    PerObjectBufferManager, 
    ObjectBuffer,
    STALL_COSTS 
};