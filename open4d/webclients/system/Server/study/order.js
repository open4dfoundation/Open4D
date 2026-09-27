'use strict';

/**
 * Trial order: a Williams balanced Latin square, keyed on the participant.
 *
 * The same design the Quest study uses (`scripts/user_study.py`), for the same
 * reason. Rating a method is not independent of what was watched just before
 * it, so the order is counterbalanced: across a full set of participants every
 * method appears in every position once, and every method is immediately
 * preceded by every other exactly once, which balances first-order carryover.
 * An even number of methods needs n rows; an odd number needs 2n, the rows and
 * their reverses.
 *
 * Participants see only a position label, never the method, so a rating
 * cannot be anchored on a name.
 */

/** The first row of the design: 0, 1, n-1, 2, n-2, ... */
function firstRow(n) {
    const row = [0];
    for (let k = 1; k < n; k++) row.push(k % 2 ? (k + 1) / 2 : n - k / 2);
    return row;
}

/** Every row of the design for `n` conditions. */
function williamsRows(n) {
    if (!Number.isInteger(n) || n < 1) throw new Error(`need at least one condition, got ${n}`);
    const base = firstRow(n);
    const rows = [];
    for (let shift = 0; shift < n; shift++) rows.push(base.map(value => (value + shift) % n));
    if (n % 2) for (let index = 0; index < n; index++) rows.push([...rows[index]].reverse());
    return rows;
}

/**
 * The digits in a participant code, as the index that picks a row. "p07" and
 * "p7" are the same participant, which is the point of reading the number
 * rather than hashing the text.
 */
function participantIndex(code) {
    const digits = String(code || '').match(/\d+/g);
    return digits ? Number(digits.join('')) : 0;
}

/** Position labels shown to the participant: A, B, ... Z, AA, AB, ... */
function positionLabel(index) {
    let label = '';
    let n = index + 1;
    while (n > 0) {
        const remainder = (n - 1) % 26;
        label = String.fromCharCode(65 + remainder) + label;
        n = Math.floor((n - 1) / 26);
    }
    return label;
}

/**
 * @param {string[]} methods condition ids, in a fixed canonical order
 * @param {number} index participant index
 * @returns {{row: number, order: {position: number, label: string, method: string}[]}}
 */
function trialOrder(methods, index) {
    const rows = williamsRows(methods.length);
    const row = ((index % rows.length) + rows.length) % rows.length;
    return {
        row,
        order: rows[row].map((condition, position) => ({
            position, label: positionLabel(position), method: methods[condition]
        }))
    };
}

module.exports = { williamsRows, participantIndex, positionLabel, trialOrder };
