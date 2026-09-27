'use strict';

/**
 * The post-clip questionnaire, shared by the browser page and the server.
 *
 * Wording is copied from `QuestQuestionnairePanel.cs` rather than written
 * afresh, so that a rating collected in a browser answers the same question as
 * one collected in the headset and the two can be pooled or compared. Change it
 * there and here together, or not at all.
 *
 * Pure data and one validator, with no DOM and no Node, because both sides
 * require it: the page to build the form, the server to refuse a response the
 * form should not have been able to produce.
 */

const RATING_MIN = 1;
const RATING_MAX = 5;
const MAXIMUM_ARTIFACT_SELECTIONS = 2;

const RATINGS = Object.freeze([
    {
        id: 'C1', title: 'Visual quality',
        prompt: "How sharp, detailed, and accurate were the colors on the performers' surfaces?"
    },
    {
        id: 'C2', title: 'Geometry & depth fidelity',
        prompt: 'How solid, complete, and correctly shaped did performers look as you moved around them?'
    },
    {
        id: 'C3', title: 'Temporal smoothness',
        prompt: 'How smooth was playback—without freezing, stuttering, or visible jumps in quality?'
    },
    {
        id: 'C4', title: 'Overall experience',
        prompt: 'Overall, how would you rate your experience watching this clip?'
    }
]);

const ARTIFACTS = Object.freeze([
    { id: 'surface_geometry_degradation', label: "Blurry surfaces or blocky / 'melted' body shapes" },
    { id: 'missing_parts', label: 'Holes or missing parts of a person' },
    { id: 'quality_flicker', label: 'Flickering or popping between quality levels' },
    { id: 'freezing', label: 'Freezing or a person standing still / disappearing' },
    { id: 'mixed_quality_parts', label: 'Different parts of the same performer showed different quality' },
    { id: 'client_low_fps', label: 'Low frame rate or the whole view stuttering / getting stuck' },
    { id: 'view_dependent_degradation', label: 'Quality got worse when I moved closer or changed angle' },
    { id: 'none', label: 'None of the above' }
]);

const ARTIFACT_PROMPT =
    'Select up to TWO most severe issues. Choose None if nothing applied.';

/**
 * Throw unless `response` is one the form could have produced.
 *
 * @param {object} response
 * @param {Object<string, number>} response.ratings C1..C4 -> 1..5
 * @param {string[]} response.artifacts ids; "none" is exclusive
 * @returns {object} a normalized copy
 */
function validateResponse(response) {
    if (!response || typeof response !== 'object') {
        throw new Error('questionnaire response must be an object');
    }
    const ratings = {};
    for (const { id } of RATINGS) {
        const value = response.ratings?.[id];
        if (!Number.isInteger(value) || value < RATING_MIN || value > RATING_MAX) {
            throw new Error(`${id} must be an integer ${RATING_MIN}-${RATING_MAX}, got ${value}`);
        }
        ratings[id] = value;
    }
    const known = new Set(ARTIFACTS.map(artifact => artifact.id));
    const chosen = [...new Set(Array.isArray(response.artifacts) ? response.artifacts : [])];
    if (!chosen.length) throw new Error('choose at least one artifact, or "none"');
    for (const id of chosen) {
        if (!known.has(id)) throw new Error(`unknown artifact "${id}"`);
    }
    if (chosen.includes('none') && chosen.length > 1) {
        throw new Error('"none" cannot be combined with other artifacts');
    }
    if (chosen.length > MAXIMUM_ARTIFACT_SELECTIONS) {
        throw new Error(`choose at most ${MAXIMUM_ARTIFACT_SELECTIONS} artifacts`);
    }
    const timing = response.timing && typeof response.timing === 'object' ? response.timing : {};
    const seconds = value => (Number.isFinite(value) && value >= 0 ? Number(value) : null);
    return {
        ratings,
        artifacts: chosen,
        timing: {
            totalSeconds: seconds(timing.totalSeconds),
            ratingsPageSeconds: seconds(timing.ratingsPageSeconds),
            artifactsPageSeconds: seconds(timing.artifactsPageSeconds),
            ratingSelections: Number.isInteger(timing.ratingSelections) ? timing.ratingSelections : null,
            artifactSelections: Number.isInteger(timing.artifactSelections) ? timing.artifactSelections : null
        }
    };
}

module.exports = {
    RATINGS, ARTIFACTS, ARTIFACT_PROMPT,
    RATING_MIN, RATING_MAX, MAXIMUM_ARTIFACT_SELECTIONS,
    validateResponse
};
