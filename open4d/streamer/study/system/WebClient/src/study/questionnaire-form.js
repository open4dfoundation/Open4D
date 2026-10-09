'use strict';

/**
 * The two-page post-clip questionnaire, in the browser.
 *
 * Behaviour is the Quest panel's (`QuestQuestionnairePanel.cs`), so that a
 * browser rating and a headset rating were collected the same way:
 *
 *   page 1  C1-C4, one to five stars each; Continue only once all four are set
 *   page 2  the artifact checklist, at most two, the most severe; "None of the
 *           above" clears the others and any other choice clears it
 *
 * and the same timing is kept: seconds on each page, in total, and how many
 * selections were made, which separates a considered answer from a click-through.
 */

function el(tag, attributes = {}, children = []) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(attributes)) {
        if (key === 'text') node.textContent = value;
        else if (key === 'class') node.className = value;
        else node.setAttribute(key, value);
    }
    for (const child of children) node.appendChild(child);
    return node;
}

/**
 * Show the questionnaire and resolve with the response once it is submitted.
 *
 * @param {object} args
 * @param {object} args.config `questionnaire` from /api/study/config
 * @param {string} args.label the trial's position label, e.g. "B"
 * @param {(id: string) => HTMLElement} args.$ element lookup
 * @param {() => number} [args.now] ms
 */
function runQuestionnaire({ config, label, $, now = () => performance.now() }) {
    return new Promise(resolve => {
        const ratings = {};
        const artifacts = new Set();
        const timing = { ratingSelections: 0, artifactSelections: 0 };
        const shownAt = now();
        let page = 'ratings';
        let pageSince = shownAt;
        const spent = { ratings: 0, artifacts: 0 };

        const switchTo = next => {
            const at = now();
            spent[page] += at - pageSince;
            page = next;
            pageSince = at;
            $('q-ratings').hidden = next !== 'ratings';
            $('q-artifacts').hidden = next !== 'artifacts';
        };

        // ---- page 1: ratings
        $('q-title').textContent = `How was clip ${label}?`;
        const rows = $('q-rating-rows');
        rows.replaceChildren();
        for (const item of config.ratings) {
            const stars = [];
            const starRow = el('div', { class: 'stars', role: 'radiogroup',
                                        'aria-label': `${item.id} ${item.title}` });
            for (let value = config.min; value <= config.max; value++) {
                const star = el('button', { type: 'button', text: '★',
                                            'aria-label': `${value} star${value > 1 ? 's' : ''}` });
                star.addEventListener('click', () => {
                    ratings[item.id] = value;
                    timing.ratingSelections += 1;
                    stars.forEach((other, index) => other.classList.toggle('on', index < value));
                    refresh();
                });
                stars.push(star);
                starRow.appendChild(star);
            }
            rows.appendChild(el('div', { class: 'rating' }, [
                el('div', {}, [
                    el('div', { class: 'title', text: `${item.id}  ${item.title}` }),
                    el('div', { class: 'prompt', text: item.prompt })
                ]),
                el('div', {}, [starRow, el('div', { class: 'scale' }, [
                    el('span', { text: 'poor' }), el('span', { text: 'excellent' })])])
            ]));
        }

        // ---- page 2: artifacts
        $('q-artifact-prompt').textContent = config.artifactPrompt;
        const list = $('q-artifact-rows');
        list.replaceChildren();
        const boxes = new Map();
        for (const item of config.artifacts) {
            const box = el('input', { type: 'checkbox', value: item.id });
            const row = el('label', {}, [box, el('span', { text: item.label })]);
            box.addEventListener('change', () => {
                timing.artifactSelections += 1;
                if (box.checked) {
                    if (item.id === 'none') artifacts.clear();
                    else artifacts.delete('none');
                    artifacts.add(item.id);
                } else {
                    artifacts.delete(item.id);
                }
                refresh();
            });
            boxes.set(item.id, { box, row });
            list.appendChild(row);
        }

        function refresh() {
            const rated = config.ratings.every(item => Number.isInteger(ratings[item.id]));
            $('q-continue').disabled = !rated;
            $('q-ratings-hint').hidden = rated;
            const full = [...artifacts].filter(id => id !== 'none').length >= config.maxArtifacts;
            for (const [id, { box, row }] of boxes) {
                box.checked = artifacts.has(id);
                // At the limit a third issue cannot be ticked; "none" always can,
                // since choosing it replaces the rest.
                const blocked = full && !box.checked && id !== 'none';
                box.disabled = blocked;
                row.classList.toggle('off', blocked);
            }
            $('q-submit').disabled = artifacts.size === 0;
            $('q-submit-hint').textContent = artifacts.size === 0
                ? 'Choose up to two, or None.' : '';
        }

        $('q-continue').onclick = () => switchTo('artifacts');
        $('q-back').onclick = () => switchTo('ratings');
        $('q-submit').onclick = () => {
            switchTo('ratings');
            const total = now() - shownAt;
            resolve({
                ratings: { ...ratings },
                artifacts: [...artifacts],
                timing: {
                    totalSeconds: Number((total / 1000).toFixed(2)),
                    ratingsPageSeconds: Number((spent.ratings / 1000).toFixed(2)),
                    artifactsPageSeconds: Number((spent.artifacts / 1000).toFixed(2)),
                    ratingSelections: timing.ratingSelections,
                    artifactSelections: timing.artifactSelections
                }
            });
        };

        switchTo('ratings');
        spent.ratings = 0;
        refresh();
    });
}

module.exports = { runQuestionnaire };
