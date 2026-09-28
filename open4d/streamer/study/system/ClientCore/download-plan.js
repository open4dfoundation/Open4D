'use strict';

/**
 * Segment download planning and result summarization.
 *
 * Extracted verbatim from system/Client/client.js `downloadSegmentFiles`. This
 * module decides WHAT to fetch and HOW to judge the outcome; it never fetches
 * anything, so the same rules apply in Node and in a browser.
 *
 * The transport half (a sliding window of `DOWNLOAD_CONCURRENCY` workers
 * draining `tasks`) stays with the platform adapter, because that is where
 * keep-alive agents, destinations and byte crediting live.
 */

/**
 * Build the flat task list for one segment across ALL objects.
 *
 * Collecting every file up front and draining the list with a sliding window
 * means a new request starts the moment one finishes, so the link never idles
 * at batch or object boundaries.
 *
 * @param {object} args
 * @param {object} args.selection ABR selection (`combo` maps object -> rep)
 * @param {object} args.manifest the published menu.json
 * @param {number} args.framesPerSegment geometry frames in this segment
 * @param {boolean} args.interactive interactive mode writes files to disk
 * @param {(objName: string, repId: string) => (string|null)} args.objectDirFor
 *        per-object staging directory, or null when nothing is written
 * @param {(objName: string, dir: string|null, file: object) => (string|null)} args.destinationFor
 *        where a planned file should land, or null to keep it in memory
 * @param {(assetPath: string) => (string|null)} args.pathToUrl manifest path -> URL
 * @returns {{tasks: object[], perObj: Map<string, object>}}
 */
function planSegmentDownload({
    selection,
    manifest,
    framesPerSegment,
    interactive,
    objectDirFor,
    destinationFor,
    pathToUrl
}) {
    const tasks = [];
    const perObj = new Map();   // objName -> accumulator (insertion = combo order)

    for (const [objName, rep] of Object.entries(selection.combo)) {
        const objData = manifest.objects[objName];
        const startNumber = objData.start_number || 1;
        const baseDir = rep.paths.base_dir;
        const files = [];
        const objectDir = interactive ? objectDirFor(objName, rep.id) : null;

        const textureAssets = (
            Array.isArray(rep.paths.texture_urls) && rep.paths.texture_urls.length > 0
                ? rep.paths.texture_urls
                : Array.isArray(rep.paths.texture_mp4s) && rep.paths.texture_mp4s.length > 0
                    ? rep.paths.texture_mp4s
                    : [rep.paths.texture_url || rep.paths.texture_mp4].filter(Boolean)
        );
        textureAssets.forEach((textureAsset, textureIndex) => {
            const url = pathToUrl(textureAsset);
            if (url) {
                const file = { url, kind: 'texture', textureIndex, frameNum: null };
                files.push({
                    ...file,
                    destination: interactive
                        ? destinationFor(objName, objectDir, file) : null
                });
            }
        });

        const geometryPattern = rep.paths.geometry_url_pattern || rep.paths.geometry_drc_pattern;
        if (geometryPattern) {
            for (let f = 0; f < framesPerSegment; f++) {
                const frameNum = startNumber + f;
                const drcFile = geometryPattern.replace(
                    '%04d', String(frameNum).padStart(4, '0'));
                const asset = drcFile.startsWith('/files/') || /^https?:\/\//.test(drcFile)
                    ? drcFile
                    : `${baseDir}/${drcFile}`;
                const url = pathToUrl(asset);
                if (url) {
                    const file = { url, kind: 'geometry', frameNum, frameIndex: f };
                    files.push({
                        ...file,
                        destination: interactive
                            ? destinationFor(objName, objectDir, file) : null
                    });
                }
            }
        }

        perObj.set(objName, {
            repId: rep.id,
            size: 0,
            success: 0,
            total: files.length,
            geometryTotal: files.filter(f => f.kind === 'geometry').length,
            geometrySuccess: 0,
            objectDir,
            textureFile: null,
            textureFiles: new Array(textureAssets.length).fill(null),
            // Index-aligned to the segment's frames. A frame whose download
            // fails keeps its slot as null so the renderer can hold the previous
            // frame; collecting only the successes used to shift every later
            // frame by one and silently desynchronise the clip.
            geometryFiles: new Array(framesPerSegment).fill(null)
        });
        for (const file of files) tasks.push({ objName, ...file });
    }

    return { tasks, perObj };
}

/**
 * Credit one completed task against its object's accumulator.
 *
 * @param {object} acc accumulator from planSegmentDownload
 * @param {object} task the planned task
 * @param {{success: boolean, size: number}} result transport outcome
 * @param {boolean} interactive
 */
function creditTaskResult(acc, task, result, interactive) {
    acc.size += result.size;
    if (!result.success) return;

    acc.success++;
    if (interactive && task.kind === 'texture') {
        acc.textureFiles[task.textureIndex] = task.destination;
        // Legacy renderer/message consumers still understand one textureFile.
        // Multi-group-aware consumers use the list.
        if (acc.textureFiles.length === 1) acc.textureFile = task.destination;
    }
    if (task.kind === 'geometry') {
        acc.geometrySuccess++;
        if (interactive) acc.geometryFiles[task.frameIndex] = task.destination;
    }
}

/**
 * Turn the accumulators into per-object results plus the segment total.
 *
 * The old `|| acc.size > 0` success rule let a 1-of-61 download report success,
 * which then failed hard in the renderer's frame-count check. Geometry is judged
 * on its own: the texture is optional (an untextured mesh still plays) but a clip
 * needs most of its frames.
 *
 * @param {Map<string, object>} perObj
 * @returns {{totalSize: number, objects: object[]}}
 */
function summarizeSegmentDownload(perObj) {
    let totalSize = 0;
    const results = [];
    for (const [objName, acc] of perObj.entries()) {
        totalSize += acc.size;
        const successRatio = acc.total > 0 ? acc.success / acc.total : 0;
        const geometryRatio = acc.geometryTotal > 0 ? acc.geometrySuccess / acc.geometryTotal : 0;
        results.push({
            objectName: objName,
            repId: acc.repId,
            filesDownloaded: acc.success,
            totalFiles: acc.total,
            geometryDownloaded: acc.geometrySuccess,
            geometryTotal: acc.geometryTotal,
            size: acc.size,
            success: successRatio >= 0.9 && geometryRatio >= 0.9,
            successRatio,
            geometryRatio,
            objectDir: acc.objectDir,
            textureFiles: acc.textureFiles,
            textureFile: acc.textureFile,
            geometryFiles: acc.geometryFiles
        });
    }
    return { totalSize, objects: results };
}

/** Expected bytes for a selection, used to seed in-flight download tracking. */
function expectedSelectionBytes(selection) {
    return Object.values(selection.combo).reduce(
        (sum, rep) => sum + (rep.predicted.bitrate_mbps * 1000000 / 8), 0);
}

module.exports = {
    planSegmentDownload,
    creditTaskResult,
    summarizeSegmentDownload,
    expectedSelectionBytes
};
