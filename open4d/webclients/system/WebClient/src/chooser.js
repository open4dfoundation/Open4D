'use strict';

/**
 * The launcher: one row per system, so the list stays readable as methods are
 * added.
 *
 * Each row is a link. Selecting objects is behind a per-row toggle rather than
 * inline, because the object list is the one thing here that does not scale —
 * nine rows of checkboxes under every method would bury the list it belongs to.
 *
 * Why an object picker exists at all: the ladder must publish at least one
 * representation per object in the scene, so the scene sets an irreducible
 * bitrate floor. All nine ORBIT objects floor at ~116 Mbps, and under that
 * floor the MCKP can only buy the few highest-weighted objects at their
 * cheapest rung and freezes the rest — the page then looks permanently starved
 * however good the link is. Three objects floor near 21 Mbps. So each picker
 * defaults to the cheapest few rather than leaving a viewer to discover the
 * deficit, and Vega's defaults to the smallest clips because that page
 * preloads whole clips before it plays them.
 */

// One line per system, carrying the one thing a screenshot cannot show:
// whether it adapts, and where. A fixed-quality player and an adaptive one
// look identical on a fast link.
const DESCRIPTION = {
    mesh: 'Textured meshes. Adapts in this browser, one representation per '
        + 'object per segment.',
    vivo: 'Point clouds. Adapts server-side, per spatial tile.',
    nava: 'Point clouds. Adapts server-side, one quality per object per segment.',
    vega: '3D Gaussian splats. Fixed quality, no adaptation.',
    nevo: 'Neural volumetric. Pre-rendered comparison panels.'
};

function el(tag, props = {}, children = []) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(props)) {
        if (key === 'class') node.className = value;
        else if (key === 'text') node.textContent = value;
        else if (key.startsWith('on')) node.addEventListener(key.slice(2), value);
        else node.setAttribute(key, value);
    }
    for (const child of [].concat(children)) if (child) node.appendChild(child);
    return node;
}

function mbps(value) {
    return Number.isFinite(value) ? `${value.toFixed(1)} Mbps` : '—';
}

/** Where a row points, including whatever the page needs to find its data. */
function launchUrl(system, objects) {
    const url = new URL(system.page, window.location.origin);
    if (objects && objects.length) {
        url.searchParams.set('objects', objects.join(','));
    }
    // Each point-cloud baseline has its own bridge, so the address is what
    // selects ViVo vs NAVA — the page itself is the same.
    if (system.bridge) url.searchParams.set('bridge', system.bridge);
    return url.toString();
}

/**
 * Which objects a system offers, and a sensible default selection.
 * `cost` labels the single numeric column: bitrate for a ladder, size for a
 * preloading viewer.
 */
function objectChoice(system) {
    const objects = [...(system.objects || [])];
    if (!objects.length) return null;

    if (system.id === 'vega') {   // sized by download, not by bitrate
        return {
            objects,
            head: 'clip',
            cell: o => `${(o.bytes / 1e6).toFixed(0)} MB`,
            defaults: [...objects].sort((a, b) => a.bytes - b.bytes)
                .slice(0, 2).map(o => o.name)
        };
    }
    const priced = objects.filter(o => o.floorMbps !== null);
    return {
        objects: objects.sort((a, b) => (b.weight || 0) - (a.weight || 0)
            || a.name.localeCompare(b.name)),
        head: 'floor',
        cell: o => mbps(o.floorMbps),
        defaults: (priced.length ? priced : objects).slice()
            .sort((a, b) => (a.floorMbps ?? Infinity) - (b.floorMbps ?? Infinity))
            .slice(0, 3).map(o => o.name)
    };
}

/**
 * Ask the server to (re)start a point-cloud baseline with this object set.
 *
 * Needed because these baselines fix their scene with `--objects` at startup,
 * so unlike the others the selection cannot be a query parameter — it is a new
 * process. The server validates every name against the tile catalogue and
 * waits until both the bridge and the baseline are listening before replying,
 * so a resolved promise means the page can actually connect.
 */
async function restartBaseline(system, objects) {
    const response = await fetch('/api/pointcloud/start', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        cache: 'no-store',
        body: JSON.stringify({ id: system.id, objects })
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(body.error || `HTTP ${response.status}`);
    return body;
}

/** One row: name, description, and an optional collapsed object picker. */
function renderRow(system) {
    const choice = objectChoice(system);
    const chosen = new Set(choice ? choice.defaults : []);
    const status = document.getElementById('status');

    const link = el('a', {
        class: 'name',
        text: system.name,
        // `detail` explains a system that cannot run. It is not on the row —
        // that is what keeps the list scannable — but it is one hover away,
        // and /api/systems still reports `ready`.
        title: system.detail || ''
    });
    const sync = () => { link.href = launchUrl(system, [...chosen]); };
    sync();

    if (system.restartable) {
        // Starting the process takes ~15 s, so navigating first would land on
        // a page that cannot connect yet. Hold the click, start it, then go.
        link.addEventListener('click', async (event) => {
            if (event.metaKey || event.ctrlKey || event.button !== 0) return;
            event.preventDefault();
            const objects = [...chosen];
            if (!objects.length) {
                status.textContent = `${system.name}: choose at least one object`;
                status.className = 'bad';
                return;
            }
            link.classList.add('busy');
            status.className = '';
            status.textContent = `starting ${system.name} with `
                + `${objects.join(', ')} …`;
            try {
                const started = await restartBaseline(system, objects);
                window.location.href = launchUrl(
                    { ...system, bridge: started.bridge }, []);
            } catch (error) {
                status.textContent = `${system.name}: ${error.message}`;
                status.className = 'bad';
                link.classList.remove('busy');
            }
        });
    }

    const row = el('li', { class: 'row' }, [
        link,
        el('span', { class: 'desc', text: DESCRIPTION[system.id] || '' })
    ]);
    if (!choice) return row;

    const picker = el('div', { class: 'picker', hidden: 'hidden' });
    const toggle = el('button', {
        class: 'toggle',
        text: `${chosen.size} of ${choice.objects.length}`,
        onclick: () => {
            const open = picker.hasAttribute('hidden');
            if (open) picker.removeAttribute('hidden');
            else picker.setAttribute('hidden', 'hidden');
            toggle.classList.toggle('open', open);
        }
    });
    row.appendChild(toggle);
    row.appendChild(picker);

    const table = el('table', {}, [el('tbody')]);
    const body = table.querySelector('tbody');
    for (const object of choice.objects) {
        const box = el('input', { type: 'checkbox' });
        box.checked = chosen.has(object.name);
        const tr = el('tr', {}, [
            el('td', {}, box),
            el('td', { text: object.name }),
            el('td', { class: 'num', text: choice.cell(object) })
        ]);
        box.addEventListener('change', () => {
            if (box.checked) chosen.add(object.name);
            else chosen.delete(object.name);
            tr.classList.toggle('off', !box.checked);
            toggle.textContent = `${chosen.size} of ${choice.objects.length}`;
            sync();
        });
        tr.classList.toggle('off', !box.checked);
        body.appendChild(tr);
    }
    picker.appendChild(table);
    return row;
}

async function main() {
    const root = document.getElementById('systems');
    const status = document.getElementById('status');
    let payload;
    try {
        const response = await fetch('/api/systems', { cache: 'no-store' });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        payload = await response.json();
    } catch (error) {
        status.textContent = `could not reach /api/systems: ${error.message}`;
        status.className = 'bad';
        return;
    }
    status.textContent = '';

    const list = el('ul', { class: 'systems' });
    for (const system of payload.systems || []) list.appendChild(renderRow(system));
    root.appendChild(list);
}

main().catch(error => {
    const status = document.getElementById('status');
    if (status) status.textContent = `fatal: ${error.message}`;
    // eslint-disable-next-line no-console
    console.error(error);
});
