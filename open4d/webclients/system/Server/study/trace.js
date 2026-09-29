'use strict';

/**
 * Bandwidth traces for the browser study: parsed, windowed, and read back.
 *
 * The parsing rules are `quest-trace-player.js`'s, deliberately: a trace a
 * participant uploads here must mean the same link it would mean to the Quest
 * study, or the two sets of results stop being comparable. So a CSV is
 * `time,bandwidth` in seconds and Mbps, the first sample is the rate at t=0
 * whatever its timestamp says, each sample holds until the next one, and a
 * malformed row is an error rather than something silently skipped -- the one
 * exception being a `time,bandwidth` header, which is expected.
 *
 * What is added is a window. The Quest study plays "the rebased 0-20 s window
 * of nuwave_trace.csv"; a window here is that, cut out of an uploaded trace and
 * rebased to zero, so a long capture can be used without editing the file.
 */

const HEADER = 'time,bandwidth';

/**
 * @param {string} text CSV contents
 * @param {object} [options]
 * @param {number} [options.scale=1] multiply every rate
 * @returns {{time: number, bandwidth: number}[]} rebased so points[0].time is 0
 */
function parseTrace(text, { scale = 1 } = {}) {
    if (!(Number.isFinite(scale) && scale > 0)) {
        throw new Error(`trace scale must be a positive number, got ${scale}`);
    }
    const points = [];
    const lines = String(text || '').split(/\r?\n/);
    for (let index = 0; index < lines.length; index++) {
        const line = lines[index].trim();
        if (!line) continue;
        const values = line.split(',').map(value => Number(value.trim()));
        if (values.length < 2 || !Number.isFinite(values[0])
            || !Number.isFinite(values[1])) {
            if (line.toLowerCase().replace(/\s+/g, '') === HEADER) continue;
            throw new Error(
                `trace line ${index + 1} is not "seconds,Mbps": ${line.slice(0, 60)}`);
        }
        const point = { time: values[0], bandwidth: values[1] * scale };
        if (point.time < 0 || point.bandwidth <= 0) {
            throw new Error(`invalid trace value on line ${index + 1}: ${line}`);
        }
        if (points.length && point.time < points[points.length - 1].time) {
            throw new Error(`trace time moves backwards on line ${index + 1}`);
        }
        points.push(point);
    }
    if (!points.length) throw new Error('trace contains no bandwidth points');
    const origin = points[0].time;
    return points.map(point => ({ ...point, time: point.time - origin }));
}

/**
 * The part of a trace between `start` and `end`, rebased so `start` is t=0.
 *
 * The rate in force at `start` is kept even when no sample falls exactly on it:
 * a step function is defined between its samples, and cutting at 7.5 s of a
 * trace sampled every second must begin at the 7 s rate, not skip to the 8 s
 * one.
 */
function windowTrace(points, { start = 0, end = Infinity } = {}) {
    if (!(start >= 0) || !(end > start)) {
        throw new Error(`trace window must satisfy 0 <= start < end, got ${start}..${end}`);
    }
    const out = [{ time: 0, bandwidth: rateAt(points, start) }];
    for (const point of points) {
        if (point.time <= start || point.time >= end) continue;
        out.push({ time: point.time - start, bandwidth: point.bandwidth });
    }
    return out;
}

/** The rate at `t` seconds: the last sample at or before it. */
function rateAt(points, t) {
    let rate = points[0].bandwidth;
    for (const point of points) {
        if (point.time > t) break;
        rate = point.bandwidth;
    }
    return rate;
}

/** Seconds from the first sample to the last. */
function traceDuration(points) {
    return points[points.length - 1].time;
}

function summarize(points) {
    const rates = points.map(point => point.bandwidth);
    return {
        points: points.length,
        durationSeconds: traceDuration(points),
        minMbps: Math.min(...rates),
        maxMbps: Math.max(...rates)
    };
}

module.exports = { parseTrace, windowTrace, rateAt, traceDuration, summarize };
