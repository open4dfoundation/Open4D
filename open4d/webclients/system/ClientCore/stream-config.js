'use strict';

/**
 * Stream timing configuration, as published by the server's /api/config.
 *
 * Extracted from system/Client/client.js `applyStreamConfig`, which mutated
 * five module-level globals. Here it is a pure function returning a frozen
 * config, so the browser client and the Node client cannot drift on validation
 * and a test can exercise the error paths without a server.
 */

/**
 * @param {object} config raw /api/config body
 * @param {object} [previous] values to fall back on for absent fields
 * @returns {Readonly<{segmentDuration: number, framesPerSegment: number,
 *   segmentIntervalMs: number, totalSegments: number,
 *   updateIntervalSegments: number}>}
 * @throws {Error} listing every field that is still missing or invalid
 */
function resolveStreamConfig(config, previous = {}) {
    const resolved = {
        segmentDuration: previous.segmentDuration ?? null,
        framesPerSegment: previous.framesPerSegment ?? null,
        segmentIntervalMs: previous.segmentIntervalMs ?? null,
        totalSegments: previous.totalSegments ?? null,
        updateIntervalSegments: previous.updateIntervalSegments ?? null
    };

    const positive = value => Number.isFinite(value) && value > 0;

    if (config) {
        if (positive(config.segmentDuration)) resolved.segmentDuration = config.segmentDuration;
        if (positive(config.framesPerSegment)) resolved.framesPerSegment = config.framesPerSegment;
        if (positive(config.segmentIntervalMs)) {
            resolved.segmentIntervalMs = config.segmentIntervalMs;
        } else {
            // Derived rather than defaulted: a segment interval that disagreed
            // with the segment duration would silently desynchronise the segment
            // clock from playback.
            resolved.segmentIntervalMs = Math.round(resolved.segmentDuration * 1000);
        }
        if (positive(config.totalSegments)) resolved.totalSegments = config.totalSegments;
        if (positive(config.updateIntervalSegments)) {
            resolved.updateIntervalSegments = config.updateIntervalSegments;
        }
    }

    const missing = [];
    if (!positive(resolved.segmentDuration)) missing.push('segmentDuration');
    if (!positive(resolved.framesPerSegment)) missing.push('framesPerSegment');
    if (!positive(resolved.segmentIntervalMs)) missing.push('segmentIntervalMs');
    if (!positive(resolved.totalSegments)) missing.push('totalSegments');
    if (!positive(resolved.updateIntervalSegments)) missing.push('updateIntervalSegments');
    if (missing.length) {
        throw new Error(`Invalid stream config from server: missing ${missing.join(', ')}`);
    }

    return Object.freeze(resolved);
}

module.exports = { resolveStreamConfig };
