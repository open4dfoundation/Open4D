'use strict';

/**
 * Tests for NeVo clip-manifest handling, against a real manifest.
 *
 * The fixture is a copy of `~/nevo_output/g_dancer/manifest.json` as written by
 * `orbitnevo/render_frames.py`, so the field names and the condition shape are
 * the real ones rather than my guess at them.
 *
 * Run: node --test tests/test_nevo_manifest.js
 */

const assert = require('assert');
const test = require('node:test');
const fs = require('fs');
const path = require('path');

const {
    resolveConditions, referenceCondition, frameFile, frameFiles,
    layoutPanels, conditionCaption, clipSummary
} = require('../system/WebClient/src/nevo-manifest');

const FIXTURE = path.join(__dirname, 'fixtures_nevo_manifest.json');
const MANIFEST = JSON.parse(fs.readFileSync(FIXTURE, 'utf8'));

test('the fixture is a real render_frames manifest', () => {
    assert.strictEqual(MANIFEST.representation, 'ReRF (NeRF feature voxels)');
    assert.ok(Array.isArray(MANIFEST.frames) && MANIFEST.frames.length > 0);
    assert.ok(Array.isArray(MANIFEST.conditions) && MANIFEST.conditions.length >= 2);
    assert.ok(MANIFEST.conditions.some(c => c.threshold !== null),
        'at least one visibility-filtered condition');
});

// --------------------------------------------------------------------------
// Conditions
// --------------------------------------------------------------------------

test('conditions include the declared ones plus the captured camera', () => {
    const conditions = resolveConditions(MANIFEST);
    assert.strictEqual(conditions.length, MANIFEST.conditions.length + 1);
    assert.deepStrictEqual(conditions.slice(0, -1).map(c => c.prefix),
        MANIFEST.conditions.map(c => c.prefix), 'declared order preserved');
    const last = conditions[conditions.length - 1];
    assert.strictEqual(last.prefix, 'reference');
    assert.strictEqual(last.isReference, true);
    assert.match(last.label, /captured camera/);
});

test('the reference condition names the view it was captured from', () => {
    // Which view matters: a view inside the training set is a reconstruction
    // of something the model saw, not a held-out test of generalisation.
    const reference = referenceCondition(MANIFEST);
    assert.strictEqual(reference.label, `captured camera ${MANIFEST.view}`);
    assert.strictEqual(reference.kept_fraction, null);
});

test('nevoOnly keeps just the filtered output, dropping the comparisons', () => {
    // Plain ReRF and the captured camera are what NeVo is compared AGAINST;
    // they are not what NeVo would transmit.
    const conditions = resolveConditions(MANIFEST, { nevoOnly: true });
    assert.ok(conditions.length >= 1);
    assert.ok(conditions.every(c => c.threshold !== null
        && c.threshold !== undefined));
    assert.ok(!conditions.some(c => c.prefix === 'reference'));
    assert.ok(!conditions.some(c => c.name === 'rerf'));
});

test('the reference can be suppressed without switching to nevoOnly', () => {
    const conditions = resolveConditions(MANIFEST, { withReference: false });
    assert.deepStrictEqual(conditions.map(c => c.prefix),
        MANIFEST.conditions.map(c => c.prefix));
});

test('a manifest with no declared conditions still yields the reference', () => {
    const conditions = resolveConditions({ ...MANIFEST, conditions: undefined });
    assert.strictEqual(conditions.length, 1);
    assert.strictEqual(conditions[0].prefix, 'reference');
});

// --------------------------------------------------------------------------
// Filenames
// --------------------------------------------------------------------------

test('frame filenames are zero-padded to three digits', () => {
    // render_frames.py writes frame_000.png; frame_0.png would 404 silently
    // behind a broken image icon.
    assert.strictEqual(frameFile('frame', 0), 'frame_000.png');
    assert.strictEqual(frameFile('nevo0.025', 7), 'nevo0.025_007.png');
    assert.strictEqual(frameFile('reference', 9), 'reference_009.png');
    assert.strictEqual(frameFile('frame', 123), 'frame_123.png');
});

test('frameFiles covers every condition of every frame, frame-major', () => {
    const conditions = resolveConditions(MANIFEST);
    const files = frameFiles(MANIFEST, conditions);
    assert.strictEqual(files.length,
        MANIFEST.frames.length * conditions.length);
    // Frame-major: all conditions of frame 0, then all of frame 1. That is the
    // order playback needs, so preloading in this order makes the first frame
    // displayable soonest.
    assert.strictEqual(files[0].frame, MANIFEST.frames[0]);
    assert.strictEqual(files[conditions.length].frame, MANIFEST.frames[1]);
    assert.ok(files.every(entry => entry.file.endsWith('.png')));
});

test('the real render files named by the fixture exist on disk',
    { skip: !fs.existsSync('/home/ryan/nevo_output/g_dancer') }, () => {
        // Guards the filename convention against the actual output tree.
        const directory = '/home/ryan/nevo_output/g_dancer';
        const conditions = resolveConditions(MANIFEST);
        const missing = frameFiles(MANIFEST, conditions)
            .filter(entry => !fs.existsSync(path.join(directory, entry.file)))
            .map(entry => entry.file);
        assert.deepStrictEqual(missing, [],
            'every manifest-declared render should be present');
    });

// --------------------------------------------------------------------------
// Layout
// --------------------------------------------------------------------------

const source = { sourceWidth: 640, sourceHeight: 960 };

test('panels tile a row, centred, without overlapping', () => {
    const layout = layoutPanels({
        panelCount: 3, ...source, canvasWidth: 1200, canvasHeight: 900
    });
    assert.strictEqual(layout.panels.length, 3);
    for (let i = 1; i < 3; i++) {
        const previous = layout.panels[i - 1];
        const current = layout.panels[i];
        assert.strictEqual(current.x, previous.x + previous.width + layout.gap,
            'panels are spaced by exactly one gap');
    }
    assert.ok(layout.originX >= 0 && layout.originY >= 0);
    assert.ok(layout.rowWidth <= 1200, 'the row fits the canvas');
});

test('panels keep the source aspect ratio', () => {
    const layout = layoutPanels({
        panelCount: 2, ...source, canvasWidth: 1000, canvasHeight: 900
    });
    const aspect = layout.panelWidth / layout.panelHeight;
    assert.ok(Math.abs(aspect - source.sourceWidth / source.sourceHeight) < 0.02,
        `aspect ${aspect} should match the source`);
});

test('a short canvas is height-bound, not clipped', () => {
    // Fitting by width alone would make tall panels overflow a short window,
    // cutting the subject's head off.
    const layout = layoutPanels({
        panelCount: 3, ...source, canvasWidth: 1800, canvasHeight: 400
    });
    assert.ok(layout.panelHeight + layout.labelHeight <= 400,
        `panel ${layout.panelHeight} + label must fit 400`);
});

test('a narrow canvas is width-bound', () => {
    const layout = layoutPanels({
        panelCount: 3, ...source, canvasWidth: 300, canvasHeight: 2000
    });
    assert.ok(layout.rowWidth <= 300);
    assert.ok(layout.panelWidth >= 1);
});

test('layout rejects impossible inputs rather than dividing by zero', () => {
    assert.throws(() => layoutPanels({
        panelCount: 0, ...source, canvasWidth: 100, canvasHeight: 100
    }), RangeError);
    assert.throws(() => layoutPanels({
        panelCount: 2, sourceWidth: 0, sourceHeight: 10,
        canvasWidth: 100, canvasHeight: 100
    }), RangeError);
});

test('a single panel is centred', () => {
    const layout = layoutPanels({
        panelCount: 1, ...source, canvasWidth: 1000, canvasHeight: 900
    });
    const centre = layout.originX + layout.panelWidth / 2;
    assert.ok(Math.abs(centre - 500) <= 1, `centre ${centre} should be ~500`);
});

// --------------------------------------------------------------------------
// Captions and summary
// --------------------------------------------------------------------------

test('captions state how much each condition kept', () => {
    // The kept fraction IS NeVo's contribution, so it belongs on screen rather
    // than only in a log.
    const conditions = resolveConditions(MANIFEST);
    const filtered = conditions.find(c => c.threshold !== null && c.threshold !== undefined);
    const caption = conditionCaption(filtered);
    assert.match(caption, /% of voxels/);
    assert.match(caption, new RegExp(
        (filtered.kept_fraction * 100).toFixed(1).replace('.', '\\.')));

    const reference = conditions.find(c => c.isReference);
    assert.strictEqual(conditionCaption(reference), reference.label,
        'the captured camera has no kept fraction to report');
});

test('the clip summary reports the facts that qualify the comparison', () => {
    const conditions = resolveConditions(MANIFEST);
    const summary = clipSummary(MANIFEST, conditions);
    assert.strictEqual(summary.name, MANIFEST.name);
    assert.strictEqual(summary.frames, MANIFEST.frames.length);
    assert.strictEqual(summary.source, `${MANIFEST.width}x${MANIFEST.height}`);
    assert.strictEqual(summary.view, MANIFEST.view);
    // Whether the shown view was in the training set decides how much the
    // comparison proves; it must not be silently dropped.
    assert.strictEqual(summary.viewInTrainingSet, MANIFEST.view_in_training_set);
    assert.ok(summary.keptFractions.length >= 1);
    assert.ok(summary.keptFractions.every(
        entry => entry.keptFraction > 0 && entry.keptFraction <= 1));
});
