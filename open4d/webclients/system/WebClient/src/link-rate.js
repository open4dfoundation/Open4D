'use strict';

/**
 * Live readout of the shaped link rate, polled from /api/shaping.
 *
 * The point of showing it is causality. A representation switch on its own is
 * unreadable — it could be the ABR working or the ABR thrashing. Beside the
 * rate the kernel is enforcing, the same switch becomes evidence. And when the
 * link is unshaped this says so, which matters because an unshaped run looks
 * like a broken adaptive system: every system just holds one operating point.
 */
function mountLinkRate(element, { fetchImpl = fetch, intervalMs = 2000 } = {}) {
    if (!element) return () => {};
    let stopped = false;

    // Short text on screen, full detail in the tooltip. The distinctions still
    // matter -- "cannot tell" must never read as "flat link" -- but they belong
    // on hover rather than in the viewer's way.
    const render = (state) => {
        if (state.error) {
            element.textContent = 'link: unknown';
            element.title = `could not read the shaped rate: ${state.error}`;
            element.className = 'link unknown';
            return;
        }
        if (!state.shaped) {
            element.textContent = 'link: unshaped';
            element.title = 'no root TBF installed, so nothing to adapt to — '
                + 'replay a trace with scripts/shape_web_demo.sh';
            element.className = 'link unshaped';
            return;
        }
        element.textContent = state.rateMbps === null
            ? 'link: shaped'
            : `link: ${state.rateMbps.toFixed(1)} Mbps`;
        element.title = `enforced by tc on ${state.interface}`;
        element.className = 'link shaped';
    };

    const tick = async () => {
        try {
            const response = await fetchImpl('/api/shaping', { cache: 'no-store' });
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            render(await response.json());
        } catch (error) {
            render({ error: error.message });
        }
    };

    tick();
    const handle = setInterval(() => { if (!stopped) tick(); }, intervalMs);
    return () => { stopped = true; clearInterval(handle); };
}

module.exports = { mountLinkRate };
