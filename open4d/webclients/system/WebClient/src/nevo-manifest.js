'use strict';

/**
 * NeVo clip manifest handling: conditions, frame filenames, panel layout.
 *
 * Pure, so the parts that decide WHAT is shown can be tested without a browser
 * (see tests/test_nevo_manifest.js). The drawing itself is in nevo-client.js.
 *
 * NeVo is the one baseline that cannot run client-side at all. It is a NeRF
 * (ReRF feature voxels) whose frames take roughly half a second each to
 * ray-march on a workstation GPU, and its entropy decoder is CUDA and Python
 * 3.8. So the browser shows PRE-RENDERED frames — exactly what
 * `orbitnevo/live_demo.py` does, and its own docstring says the same. There is
 * therefore no camera control here, and pretending otherwise would be
 * misleading rather than convenient.
 *
 * What the panel does show is the comparison the baseline exists for: plain
 * ReRF against NeVo's visibility-filtered reconstruction at the same instant
 * and viewpoint, with the captured camera alongside as ground truth.
 */

/** The captured-camera condition, appended the way live_demo.py appends it. */
function referenceCondition(manifest) {
    return {
        name: 'capture',
        prefix: 'reference',
        label: `captured camera ${manifest.view}`,
        threshold: null,
        kept_fraction: null,
        isReference: true
    };
}

/**
 * Resolve which conditions to show, in display order.
 *
 * @param {object} manifest parsed manifest.json
 * @param {object} [options]
 * @param {boolean} [options.nevoOnly] show only NeVo's own output — the
 *   conditions with a threshold — dropping plain ReRF and the captured camera,
 *   which are the comparison rather than the output.
 * @param {boolean} [options.withReference=true]
 */
function resolveConditions(manifest, { nevoOnly = false, withReference = true } = {}) {
    const declared = Array.isArray(manifest.conditions) ? manifest.conditions : [];
    if (nevoOnly) {
        return declared.filter(condition => condition.threshold !== null
            && condition.threshold !== undefined);
    }
    const resolved = [...declared];
    if (withReference) resolved.push(referenceCondition(manifest));
    return resolved;
}

/** `{prefix}_{frame:03d}.png`, matching what render_frames.py wrote. */
function frameFile(prefix, frame) {
    return `${prefix}_${String(frame).padStart(3, '0')}.png`;
}

/** Every image a clip needs, so they can be preloaded before playback. */
function frameFiles(manifest, conditions) {
    const files = [];
    for (const frame of manifest.frames) {
        for (const condition of conditions) {
            files.push({ frame, condition, file: frameFile(condition.prefix, frame) });
        }
    }
    return files;
}

/**
 * Lay out the condition panels in a row.
 *
 * The source renders are 1280x960 with the subject a small part of the frame,
 * so a crop is applied identically to every panel — identically, because the
 * conditions must stay pixel-aligned for the comparison to mean anything.
 *
 * @param {object} args
 * @param {number} args.panelCount
 * @param {number} args.sourceWidth  width after cropping
 * @param {number} args.sourceHeight height after cropping
 * @param {number} args.canvasWidth  space available
 * @param {number} args.canvasHeight
 * @param {number} [args.labelHeight]
 * @param {number} [args.gap]
 */
function layoutPanels({
    panelCount, sourceWidth, sourceHeight, canvasWidth, canvasHeight,
    labelHeight = 26, gap = 6
}) {
    if (panelCount <= 0) throw new RangeError('panelCount must be positive');
    if (!(sourceWidth > 0) || !(sourceHeight > 0)) {
        throw new RangeError('source dimensions must be positive');
    }
    const totalGap = gap * (panelCount - 1);
    const aspect = sourceWidth / sourceHeight;

    // Fit by width, then shrink if the resulting height does not fit. Both
    // constraints matter: a wide window is width-bound, a tall narrow one is
    // height-bound, and picking only one leaves panels clipped.
    let panelWidth = Math.max(1, Math.floor((canvasWidth - totalGap) / panelCount));
    let panelHeight = Math.round(panelWidth / aspect);
    const available = canvasHeight - labelHeight;
    if (panelHeight > available && available > 0) {
        panelHeight = available;
        panelWidth = Math.max(1, Math.round(panelHeight * aspect));
    }

    const rowWidth = panelWidth * panelCount + totalGap;
    const originX = Math.max(0, Math.round((canvasWidth - rowWidth) / 2));
    const originY = Math.max(0, Math.round(
        (canvasHeight - (panelHeight + labelHeight)) / 2));

    return {
        panelWidth, panelHeight, labelHeight, gap, rowWidth, originX, originY,
        panels: Array.from({ length: panelCount }, (_, index) => ({
            index,
            x: originX + index * (panelWidth + gap),
            y: originY,
            width: panelWidth,
            height: panelHeight,
            labelY: originY + panelHeight
        }))
    };
}

/** A caption line for a condition: what it is, and how much it kept. */
function conditionCaption(condition) {
    if (condition.kept_fraction === null || condition.kept_fraction === undefined) {
        return condition.label;
    }
    const percent = (condition.kept_fraction * 100).toFixed(1);
    return `${condition.label} · ${percent}% of voxels`;
}

/** Headline facts about a clip, for the page's status area. */
function clipSummary(manifest, conditions) {
    const filtered = conditions.filter(
        c => c.threshold !== null && c.threshold !== undefined);
    return {
        name: manifest.name,
        representation: manifest.representation,
        frames: manifest.frames.length,
        view: manifest.view,
        viewInTrainingSet: manifest.view_in_training_set === true,
        source: `${manifest.width}x${manifest.height}`,
        seconds: manifest.seconds,
        conditions: conditions.length,
        keptFractions: filtered.map(c => ({
            label: c.label, threshold: c.threshold, keptFraction: c.kept_fraction
        }))
    };
}

module.exports = {
    resolveConditions, referenceCondition, frameFile, frameFiles,
    layoutPanels, conditionCaption, clipSummary
};
