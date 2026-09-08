"""The metrics panel, rendered as shipped.

The panel is what makes this a research tool rather than a player, so the
things worth pinning are: one row per pane rather than a total, units in the
headers, and no prose where a number belongs.

Run under Node against a small DOM stub -- enough of `document` for the render
functions to build their tables, and no more. A stub rather than jsdom because
this package installs nothing, and the assertions are about structure, which a
stub can answer exactly.
"""
from __future__ import annotations

import json
import shutil
import subprocess
import textwrap
from pathlib import Path

import pytest

from streamer.client import viewer_path

pytestmark = pytest.mark.cpu

NODE = shutil.which("node")
requires_node = pytest.mark.skipif(NODE is None, reason="node is not installed")

#: A DOM with just enough surface for the panel: elements that hold children,
#: report their text, and can be looked up by id.
DOM = """
class El {
  constructor(tag) {
    this.tag = tag; this.children = []; this.attrs = {};
    this._text = ""; this.className = ""; this.hidden = false;
  }
  set innerHTML(value) { if (!value) this.children = []; this._html = value; }
  get innerHTML() { return this._html || ""; }
  set textContent(value) { this._text = String(value); }
  get textContent() {
    return this.children.length
      ? this.children.map((c) => c.textContent).join(" ") : this._text;
  }
  appendChild(child) { this.children.push(child); return child; }
  append(...kids) { for (const k of kids) this.children.push(k); }
  querySelectorAll(tag) {
    const found = [];
    const walk = (node) => {
      for (const child of node.children) {
        if (child.tag === tag) found.push(child);
        walk(child);
      }
    };
    walk(this);
    return found;
  }
}
const registry = {};
globalThis.document = {
  createElement: (tag) => new El(tag),
  getElementById: (id) => (registry[id] = registry[id] || new El("div")),
};
globalThis.registry = registry;
globalThis.performance = { now: () => 0 };
"""


def _extract(*names: str) -> str:
    page = viewer_path().read_text()
    chunks = []
    for name in names:
        for prefix in (f"function {name}(", f"class {name} ", f"const {name} = "):
            start = page.find(prefix)
            if start >= 0:
                break
        else:
            raise AssertionError(f"{name} is not defined in the viewer")
        end = (page.index(";\n", start) + 2 if prefix.startswith("const")
               else page.index("\n}\n", start) + 3)
        chunks.append(page[start:end])
    return "\n".join(chunks)


PIECES = ("TAG_RULES", "fmt", "rungsOf", "affordable", "bufferedSeconds",
          "isLive", "paneRow", "renderPaneTable", "renderSessionTable",
          "renderTags")


def run_scheduler_js(body: str, tmp_path: Path, name: str = "d.mjs") -> object:
    """Run ``body`` with the shipped Scheduler in scope."""
    script = tmp_path / name
    script.write_text(
        _extract("INDEPENDENT", "dependencyOf", "chain", "Scheduler") + "\n"
        + textwrap.dedent(body)
    )
    finished = subprocess.run(
        [NODE, str(script)], capture_output=True, text=True, timeout=120
    )
    if finished.returncode:
        raise AssertionError(finished.stderr)
    return json.loads(finished.stdout)


def run_js(body: str, tmp_path: Path, name: str = "p.mjs") -> object:
    script = tmp_path / name
    script.write_text(DOM + "\n" + _extract(*PIECES) + "\n" + textwrap.dedent(body))
    finished = subprocess.run(
        [NODE, str(script)], capture_output=True, text=True, timeout=120
    )
    if finished.returncode:
        raise AssertionError(finished.stderr)
    return json.loads(finished.stdout)


def a_pane(method, *, rung="low", frozen=False, buffered=3, quality=True):
    """A pane shaped like the real one, with a laddered clip."""
    return {
        "method": method,
        "frozen": frozen,
        "rung": rung,
        "buffered": buffered,
        "quality": quality,
    }


SETUP = """
const app = {
  mode: "compare", frame: 4, fps: 30, adapt: true, panes: [],
  targetBuffer: 0.15, shownAt: [0, 100, 200, 300], late: 2,
  decision: { rate: 9e6, share: 1.1e6, panes: 3, ladders: 3, frozen: 1 },
  scenes: { s: { poses: [{ view_id: 0 }] } }, scene: "s",
};
globalThis.app = app;
globalThis.rig = () => app.scenes[app.scene] || null;
globalThis.meter = { bitsPerSecond: 9.4e6, samples: 12 };
// The panel reads the clip length through this; the real one walks the panes.
globalThis.frameCount = () => 30;

function clip(name, { quality = true } = {}) {
  return {
    name, representation: "pixels", scene: "s", method: name, camera: 0,
    frames: Array.from({ length: 30 }, (_, i) => `${name}/f${i}.jpg`),
    detail: {
      bytes_per_frame: 33488,
      resolution: "1280x960",
      ...(quality ? { quality: { psnr: 47.78, ssim: 0.9934 } } : {}),
    },
    variants: [
      { name: "low", bytes: 91047, frames: Array.from({ length: 30 },
          (_, i) => `${name}@low/f${i}.jpg`),
        detail: { resolution: "320x240" },
        ...(quality ? { quality: { psnr: 41.64, ssim: 0.9772 } } : {}) },
      { name: "medium", bytes: 297057, frames: Array.from({ length: 30 },
          (_, i) => `${name}@medium/f${i}.jpg`),
        detail: { resolution: "640x480" },
        ...(quality ? { quality: { psnr: 44.99, ssim: 0.9888 } } : {}) },
    ],
    notes: ["a training view for both Vega and ReRF: this measures reconstruction",
            "sh_degree 0: no view-dependent bands"],
  };
}

function pane(name, { rung = "low", frozen = false, ahead = 4, quality = true } = {}) {
  const cache = new Map();
  for (let i = app.frame; i < app.frame + ahead; i++) cache.set(i, {});
  return {
    method: name, clip: clip(name, { quality }),
    source: {
      cache, frozen,
      rung: rung === "default" ? null : { name: rung },
      stats: { decodes: 9, bytes: 3e6, replays: 0, switches: 2,
               fetchMs: 12.4, decodeMs: 3.1 },
      snapshot() {
        return { cached: cache.size, pending: 1, decodes: 9, bytes: 3e6,
                 replays: 0, mode: "independent",
                 rung: this.rung ? this.rung.name : "default",
                 frozen: !!this.frozen };
      },
    },
  };
}

function cells(table) {
  return table.querySelectorAll("tr").map(
    (tr) => tr.children.map((td) => td.textContent));
}
"""


# ------------------------------------------------------------ one row per pane ---


@requires_node
def test_there_is_one_row_per_pane_not_a_total(tmp_path):
    """The panel used to sum across panes and write sentences about the sum.
    While watching, the question is always *which* pane, and a total cannot
    answer it."""
    result = run_js(SETUP + """
        app.panes = [pane("vega"), pane("rerf", { rung: "medium" }),
                     pane("queen", { rung: "default" })];
        renderPaneTable();
        const rows = cells(registry.paneTable).slice(1, -1);   // drop head, foot
        process.stdout.write(JSON.stringify({
            rows: rows.length, first: rows[0][0], rungs: rows.map((r) => r[1]),
        }));
    """, tmp_path)
    assert result["rows"] == 3
    assert result["first"] == "vega"
    assert result["rungs"] == ["low", "medium", "default"]


@requires_node
def test_the_headers_carry_their_units(tmp_path):
    """A column of numbers with no unit is a column of numbers."""
    result = run_js(SETUP + """
        app.panes = [pane("vega")];
        renderPaneTable();
        const head = registry.paneTable.children[0].children[0];
        process.stdout.write(JSON.stringify(head.children.map((th) => th.textContent)));
    """, tmp_path)
    assert result[:2] == ["method", "rung"]
    for unit in ("kB/f", "Mb/s", "buf s", "net ms", "dec ms"):
        assert unit in result, unit
    assert "PSNR" in result and "SSIM" in result


@requires_node
def test_measured_quality_is_shown_per_rung(tmp_path):
    """It is in the manifest -- `metrics --write` put it there -- and it is the
    reason a rung choice is judgeable rather than arbitrary."""
    result = run_js(SETUP + """
        app.panes = [pane("a", { rung: "low" }), pane("b", { rung: "medium" }),
                     pane("c", { rung: "default" })];
        renderPaneTable();
        const rows = cells(registry.paneTable).slice(1, -1);
        const head = registry.paneTable.children[0].children[0]
            .children.map((th) => th.textContent);
        const psnr = head.indexOf("PSNR");
        process.stdout.write(JSON.stringify(rows.map((r) => r[psnr])));
    """, tmp_path)
    assert result == ["41.64", "44.99", "47.78"]


@requires_node
def test_the_quality_columns_vanish_when_nothing_was_measured(tmp_path):
    """An empty column is worse than no column: it reads as zero."""
    result = run_js(SETUP + """
        app.panes = [pane("a", { quality: false })];
        renderPaneTable();
        const head = registry.paneTable.children[0].children[0];
        process.stdout.write(JSON.stringify(head.children.map((th) => th.textContent)));
    """, tmp_path)
    assert "PSNR" not in result and "SSIM" not in result


@requires_node
def test_a_frozen_pane_is_marked_in_its_own_row(tmp_path):
    """Which pane is frozen is the whole question, and a count cannot say."""
    result = run_js(SETUP + """
        app.panes = [pane("a"), pane("b", { frozen: true })];
        renderPaneTable();
        const rows = registry.paneTable.children[1].children;
        process.stdout.write(JSON.stringify({
            classes: rows.map((tr) => tr.className),
            rungs: rows.map((tr) => tr.children[1].textContent),
        }));
    """, tmp_path)
    assert result["classes"] == ["", "frozen"]
    assert result["rungs"] == ["low", "frozen"]


@requires_node
def test_fetch_and_decode_time_are_separate_columns(tmp_path):
    """They fail for different reasons and have different fixes: a slow fetch
    is the link, a slow decode is this machine. One column for both would send
    a reader to the wrong place."""
    result = run_js(SETUP + """
        app.panes = [pane("a")];
        renderPaneTable();
        const head = registry.paneTable.children[0].children[0]
            .children.map((th) => th.textContent);
        const row = registry.paneTable.children[1].children[0].children
            .map((td) => td.textContent);
        process.stdout.write(JSON.stringify({
            net: row[head.indexOf("net ms")], dec: row[head.indexOf("dec ms")],
        }));
    """, tmp_path)
    assert result == {"net": "12", "dec": "3"}


# ------------------------------------------------------------- the session row ---


@requires_node
def test_the_session_table_reports_achieved_against_target_fps(tmp_path):
    """The honest stall measure here: playback never freezes mid-frame, it runs
    slow, so what matters is how far below target it ran."""
    result = run_js(SETUP + """
        app.panes = [pane("a")];
        renderSessionTable();
        const rows = cells(registry.sessionTable);
        process.stdout.write(JSON.stringify(Object.fromEntries(
            rows.map((r) => [r[0], r[1]]))));
    """, tmp_path)
    assert result["fps"] == "10.0 / 30 target"
    assert result["late advances"] == "2"
    assert result["link"] == "9.40 Mbit/s"
    assert result["frozen"] == "1 / 3"


@requires_node
def test_pinning_adaptation_is_stated_not_implied(tmp_path):
    result = run_js(SETUP + """
        app.adapt = false;
        app.panes = [pane("a")];
        renderSessionTable();
        const rows = cells(registry.sessionTable);
        process.stdout.write(JSON.stringify(Object.fromEntries(
            rows.map((r) => [r[0], r[1]]))));
    """, tmp_path)
    assert result["adaptation"] == "pinned to default"


# ------------------------------------------------------------------- the tags ---


@requires_node
def test_caveats_become_tags(tmp_path):
    """A caveat a reader needs while looking at a render is a label, not a
    paragraph. Three paragraphs of them pushed the numbers off the screen."""
    result = run_js(SETUP + """
        app.panes = [pane("a")];
        renderTags();
        process.stdout.write(JSON.stringify(
            registry.tags.children.map((t) => [t.textContent, t.className])));
    """, tmp_path)
    labels = [label for label, _ in result]
    assert "training view" in labels
    assert "SH deg 0" in labels
    # The two that change how a number should be read are flagged.
    assert all(kind == "tag warn" for _, kind in result)


@requires_node
def test_a_missing_rig_is_flagged_in_compare(tmp_path):
    """Compare mode's whole guarantee is one shared pose; without a rig there
    is none, and the panes are not comparable by position."""
    result = run_js(SETUP + """
        app.scenes = {};
        app.panes = [pane("a")];
        renderTags();
        process.stdout.write(JSON.stringify(
            registry.tags.children.map((t) => t.textContent)));
    """, tmp_path)
    assert "no rig for this subject" in result


# --------------------------------------------------------------- no prose left ---


def test_the_panel_writes_no_sentences():
    """What "less LLM-like" means concretely: the panel emits labels and
    numbers. Explanations live under `caveats`, behind a disclosure, where
    someone who wants them can go looking."""
    page = viewer_path().read_text()
    for name in ("renderPaneTable", "renderSessionTable"):
        start = page.index(f"function {name}(")
        body = page[start:page.index("\n}\n", start)]
        # A string literal long enough to be a sentence, outside a comment.
        for line in body.splitlines():
            if line.strip().startswith(("//", "*", "/*")):
                continue
            for quoted in line.split('"')[1::2]:
                assert len(quoted) < 26, f"{name}: prose in the panel: {quoted!r}"


# --------------------------------------------------- why a pane is empty ---


def test_a_dead_live_stream_explains_itself_on_the_pane():
    """A live pane depends on a renderer this page does not control, and those
    die -- the box reboots, the GPU is wanted elsewhere. The proxy then answers
    502 and an <img> shows nothing at all, so the subject looks simply absent
    while the page insists everything is fine. That is how four subjects came
    to be reported missing when the renderers had stopped.
    """
    page = viewer_path().read_text()
    start = page.index("    if (isLive(clip)) {")
    body = page[start:page.index("    const make = kind.renderer;", start)]
    assert "this.image.onerror" in body
    assert "_explain(" in body
    # And the message has to name the renderer, because the fix is not here.
    assert "clip.detail && clip.detail.upstream" in body


def test_the_explanation_clears_when_the_stream_recovers():
    """Starting the renderer and reloading is the documented remedy, but a
    stream that comes back on its own should not leave the overlay up."""
    page = viewer_path().read_text()
    start = page.index("    if (isLive(clip)) {")
    body = page[start:page.index("    const make = kind.renderer;", start)]
    assert "this.image.onload" in body
    assert "this.missing.remove()" in body


def test_a_late_handler_cannot_overwrite_a_newer_clip():
    """An error can arrive after the pane has moved to another clip; writing
    the overlay then would blame the wrong method."""
    page = viewer_path().read_text()
    start = page.index("    if (isLive(clip)) {")
    body = page[start:page.index("    const make = kind.renderer;", start)]
    assert body.count("this.clip !== clip") + body.count("this.clip === clip") == 2


def test_every_empty_reason_uses_one_overlay_builder():
    """Two hand-built overlays drifted apart in styling once already, so there
    is one builder and every reason routes through it.

    The invariant is the *single builder*, not how many callers it has -- an
    exact caller count passes for the wrong set and fails for a correctly wired
    new one, which is what happened when a decode failure grew a third caller.
    """
    page = viewer_path().read_text()
    # One place constructs the overlay.
    assert page.count('className = "missing"') == 1
    assert page.count("_explain(headline, remedy)") == 1
    # Callers there are, and each gives both a headline and a remedy: an
    # overlay that says only what is wrong reads as a dead end.
    callers = [
        line for line in page.splitlines()
        if "_explain(" in line and "_explain(headline" not in line
    ]
    assert len(callers) >= 3


# ------------------------------------------------------- download, then play ---


@requires_node
def test_a_clip_is_downloaded_whole_and_then_held(tmp_path):
    """The on-demand shape, and the right one for a free camera.

    A streaming player fetches a few frames ahead and discards what is behind
    the playhead, because it only needs the frame it is showing. A viewer that
    can be spun around needs the frame's geometry resident to redraw it from a
    new angle -- so the frame has to stay, and if it has to stay there is no
    reason to have fetched it late.
    """
    body = '''
        globalThis.performance = { now: () => 0 };
        let fetched = 0;
        globalThis.fetch = async (url) => {
          fetched += 1;
          return { ok: true, headers: { get: () => "application/octet-stream" },
                   arrayBuffer: async () => new ArrayBuffer(1000) };
        };
        const clip = { name: "c", representation: "gaussians",
                       frames: Array.from({ length: 12 }, (_, i) => `c/f${i}.bin`) };
        const s = new Scheduler(clip, "./", async () => ({ count: 1 }),
                                { cacheSize: 6 });
        const seen = [];
        const held = await s.downloadAll((got, of) => seen.push([got, of]));
        process.stdout.write(JSON.stringify({
            held, fetched, cached: s.cache.size, resident: s.resident,
            progress: seen.length, last: seen[seen.length - 1],
        }));
    '''
    result = run_scheduler_js(body, tmp_path)
    assert result["held"] is True
    assert result["resident"] is True
    assert result["fetched"] == 12
    assert result["cached"] == 12          # the window grew to hold the clip
    assert result["progress"] == 12
    assert result["last"] == [12, 12]


@requires_node
def test_a_clip_too_big_to_hold_falls_back_to_a_window(tmp_path):
    """Rather than exhausting memory and taking the page down. Thirty Gaussian
    frames decode to about 100 MB, and four panes of that is most of a tab."""
    body = '''
        globalThis.performance = { now: () => 0 };
        globalThis.fetch = async () => ({
          ok: true, headers: { get: () => "application/octet-stream" },
          arrayBuffer: async () => new ArrayBuffer(50e6),
        });
        const clip = { name: "c", representation: "gaussians",
                       frames: Array.from({ length: 30 }, (_, i) => `c/f${i}.bin`) };
        const s = new Scheduler(clip, "./", async () => ({ count: 1 }),
                                { cacheSize: 6 });
        const held = await s.downloadAll(() => {});
        process.stdout.write(JSON.stringify({
            held, resident: s.resident, cacheSize: s.cacheSize,
            bytes: s.stats.bytes,
        }));
    '''
    result = run_scheduler_js(body, tmp_path)
    assert result["held"] is False
    assert result["resident"] is False
    assert result["cacheSize"] == 6         # back to streaming
    assert result["bytes"] > 200e6


@requires_node
def test_abandoning_a_pane_stops_its_download(tmp_path):
    """A slow clip must not keep fetching into a pane that has moved on."""
    body = '''
        globalThis.performance = { now: () => 0 };
        let fetched = 0;
        globalThis.fetch = async () => {
          fetched += 1;
          return { ok: true, headers: { get: () => "x" },
                   arrayBuffer: async () => new ArrayBuffer(10) };
        };
        const clip = { name: "c", representation: "gaussians",
                       frames: Array.from({ length: 20 }, (_, i) => `c/f${i}.bin`) };
        const s = new Scheduler(clip, "./", async () => ({ count: 1 }), {});
        const run = s.downloadAll(() => { if (fetched === 3) s.release(); });
        const held = await run;
        process.stdout.write(JSON.stringify({ held, fetched, abandoned: s.abandoned }));
    '''
    result = run_scheduler_js(body, tmp_path)
    assert result["held"] is False
    assert result["abandoned"] is True
    assert result["fetched"] < 20           # stopped early


@pytest.mark.parametrize(
    "function", ["updateClips", "rebuildPanes", "followCamera"]
)
def test_every_path_that_changes_the_pane_set_downloads(function):
    """Three functions change which clips the panes hold, and a download hooked
    into only some of them leaves the rest silently streaming frame by frame.

    Named rather than counted: an occurrence count passes for the wrong two of
    three, and fails for a fourth that is correctly wired.
    """
    page = viewer_path().read_text()
    start = page.index(f"function {function}(")
    body = page[start:page.index("\n}\n", start)]
    assert "downloadSelection()" in body, f"{function} does not download"


def test_a_stale_download_cannot_finish_into_a_new_selection():
    """A 63 MB clip takes a while; if the user switches subject meanwhile, the
    old download must not report progress for panes that no longer exist."""
    page = viewer_path().read_text()
    start = page.index("async function downloadSelection(")
    body = page[start:page.index("\n}\n", start)]
    assert "++app.downloadToken" in body
    assert body.count("token !== app.downloadToken") >= 2


# ------------------------------------------------- methods that cannot show ---


@requires_node
def test_a_pixel_method_is_listed_in_explore_not_hidden(tmp_path):
    """Reported as "on basketball, there is no captured option".

    Explore is a free camera and a photograph has no geometry to aim one at, so
    a captured method genuinely cannot appear there. But hiding it means
    someone opening the subject to see the photograph finds it simply absent,
    with nothing saying it exists or how to reach it -- a worse failure than a
    greyed-out row.
    """
    body = '''
        globalThis.REPRESENTATIONS = {
          gaussians: { geometry: true }, pixels: { geometry: false },
        };
        globalThis.spec = (clip) => clip && REPRESENTATIONS[clip.representation];
        globalThis.hasGeometry = (clip) => {
          const s = spec(clip); return s !== undefined && s.geometry;
        };
        globalThis.isPixels = (clip) => {
          const s = spec(clip); return s !== undefined && !s.geometry;
        };
        const app = { scenes: {}, index: { clips: [
          { scene: "b", method: "vega", representation: "gaussians" },
          { scene: "b", method: "captured", representation: "pixels", camera: 0 },
          { scene: "b", method: "rerf", representation: "pixels", camera: 0 },
        ] } };
        globalThis.app = app;
        process.stdout.write(JSON.stringify({
          explore: methodsFor("b", "explore"),
          compare: methodsFor("b", "compare"),
        }));
    '''
    script = tmp_path / "m.mjs"
    script.write_text(_extract("SHELL_TOLERANCE", "RING_MERGE_DEG", "lookAtCentre",
                   "shellOf", "methodsFor") + "\n" + textwrap.dedent(body))
    finished = subprocess.run([NODE, str(script)], capture_output=True, text=True,
                              timeout=60)
    if finished.returncode:
        raise AssertionError(finished.stderr)
    result = json.loads(finished.stdout)

    explore = {entry["method"]: entry["usable"] for entry in result["explore"]}
    # Every method is listed in both modes.
    assert set(explore) == {"vega", "captured", "rerf"}
    # But only the one with geometry can actually be shown under a free camera.
    assert explore == {"vega": True, "captured": False, "rerf": False}
    compare = {entry["method"]: entry["usable"] for entry in result["compare"]}
    assert all(compare.values())


@requires_node
def test_a_method_with_both_kinds_survives_explore(tmp_path):
    """Vega ships Gaussians and images; a method is usable if any one of its
    clips is, or the geometry would be masked by the images beside it."""
    body = '''
        globalThis.REPRESENTATIONS = {
          gaussians: { geometry: true }, pixels: { geometry: false },
        };
        globalThis.spec = (clip) => clip && REPRESENTATIONS[clip.representation];
        globalThis.hasGeometry = (clip) => {
          const s = spec(clip); return s !== undefined && s.geometry;
        };
        globalThis.isPixels = (clip) => {
          const s = spec(clip); return s !== undefined && !s.geometry;
        };
        globalThis.app = { scenes: {}, index: { clips: [
          { scene: "b", method: "vega", representation: "pixels", camera: 0 },
          { scene: "b", method: "vega", representation: "gaussians" },
        ] } };
        process.stdout.write(JSON.stringify(methodsFor("b", "explore")));
    '''
    script = tmp_path / "m2.mjs"
    script.write_text(_extract("SHELL_TOLERANCE", "RING_MERGE_DEG", "lookAtCentre",
                   "shellOf", "methodsFor") + "\n" + textwrap.dedent(body))
    finished = subprocess.run([NODE, str(script)], capture_output=True, text=True,
                              timeout=60)
    if finished.returncode:
        raise AssertionError(finished.stderr)
    assert json.loads(finished.stdout) == [{"method": "vega", "usable": True}]


def test_an_unusable_method_says_where_it_lives():
    """Not only that it cannot be shown here."""
    page = viewer_path().read_text()
    start = page.index("function renderMethodList(")
    body = page[start:page.index("\n}\n", start)]
    assert "compare only" in body
    assert "box.disabled = !playable" in body


def test_every_caller_of_methodsFor_filters_to_usable():
    """It returns entries now, not names. A caller that treats them as names
    would select a method this mode cannot show -- or worse, compare a string
    against an object and silently select nothing."""
    page = viewer_path().read_text()
    calls = page.count("methodsFor(")
    assert calls == 4                       # the definition plus three callers
    for marker in ("const entries = methodsFor(app.scene, app.mode);",
                   "methodsFor(scene, app.mode)\n    .filter((entry) => entry.usable)",
                   "methodsFor(app.scene, app.mode)\n      .filter((entry) => entry.usable)"):
        assert marker in page, marker


# ------------------------------------------- a free camera onto a shell ---
# ReRF renders free viewpoint -- `rerf_render.py --render_360` is the path that
# produced this bundle's orbit clips -- but it needs a CUDA GPU, so the pixels
# are made offline and a drag picks the nearest rendered view rather than
# rasterising a new one. These hold the "nearest" to being actually nearest,
# and hold the guard that stops an arbitrary rig being treated as an orbit.

SHELL = ("SHELL_TOLERANCE", "RING_MERGE_DEG", "lookAtCentre", "shellOf",
         "nearestStation", "exploreStation", "shellSampling")

PRELUDE = """
    const WORLD_UP = [0, 1, 0];
    const normalize = (v) => {
      const n = Math.hypot(v[0], v[1], v[2]) || 1;
      return [v[0] / n, v[1] / n, v[2] / n];
    };
"""


def _shell_js(body: str, tmp_path: Path, name: str) -> object:
    script = tmp_path / name
    script.write_text(PRELUDE + _extract(*SHELL) + "\n" + textwrap.dedent(body))
    finished = subprocess.run([NODE, str(script)], capture_output=True, text=True,
                              timeout=60)
    if finished.returncode:
        raise AssertionError(finished.stderr)
    return json.loads(finished.stdout)


# The shape of the bundle's own rig: N stations per ring, at a constant radius
# about a common look-at point, on one ring per elevation. Poses carry
# `forward` because that is what locates the centre.
BUILD = """
    const ringsAt = (centre, radius, count, elevations) => {
      const poses = [];
      for (const deg of elevations) {
        const el = deg * Math.PI / 180;
        for (let i = 0; i < count; i++) {
          const yaw = Math.PI / 2 - i * 2 * Math.PI / count;
          const dir = [Math.sin(yaw) * Math.cos(el), Math.sin(el),
                       Math.cos(yaw) * Math.cos(el)];
          const position = [0, 1, 2].map((k) => centre[k] + radius * dir[k]);
          poses.push({position, forward: dir.map((v) => -v)});
        }
      }
      return {poses};
    };
    const orbit = ringsAt([3, 0.937, 15], 3.176, 72, [0]);
    const stack = ringsAt([3, 0.937, 15], 3.176, 72, [0, 25, -25]);
"""


@requires_node
def test_the_centre_is_found_from_where_the_cameras_look(tmp_path):
    result = _shell_js(
        BUILD + """
        // The centroid of a stack of rings is not its centre: the +25 and -25
        // rings pull it off the axis, and every radius measured from it comes
        // out different. Solving from the view rays is what fixes that.
        const poses = stack.poses.slice(0, 72 + 36);   // one full ring, half of another
        const centroid = [0, 1, 2].map((k) =>
          poses.reduce((sum, p) => sum + p.position[k], 0) / poses.length);
        process.stdout.write(JSON.stringify({
          solved: lookAtCentre(poses).map((v) => Math.round(v * 1000) / 1000),
          centroid: centroid.map((v) => Math.round(v * 1000) / 1000),
        }));
    """, tmp_path, "s0.mjs")
    assert result["solved"] == [3.0, 0.937, 15.0]
    assert result["centroid"] != result["solved"]


@requires_node
def test_a_stack_of_rings_is_recognised_with_its_sampling(tmp_path):
    result = _shell_js(
        BUILD + """
        const one = shellOf(orbit), three = shellOf(stack);
        const describe = (shell) => ({
          centre: shell.centre.map((v) => Math.round(v * 1000) / 1000),
          radius: Math.round(shell.radius * 1000) / 1000,
          stations: shell.dirs.length,
          rings: shell.rings.map((r) => [Math.round(r.elevation), r.count]),
          azimuthStep: shell.azimuthStep,
          sampling: shellSampling(shell),
        });
        process.stdout.write(JSON.stringify({one: describe(one), three: describe(three)}));
    """, tmp_path, "s1.mjs")

    assert result["one"]["centre"] == [3.0, 0.937, 15.0]
    assert result["one"]["radius"] == 3.176
    assert result["one"]["stations"] == 72
    assert result["one"]["rings"] == [[0, 72]]
    assert result["one"]["azimuthStep"] == 5.0
    assert result["one"]["sampling"] == "±2.5° azimuth, elevation fixed at 0°"

    # Three rings, recovered from the pose geometry -- nothing in the manifest
    # says how they were stacked.
    assert result["three"]["stations"] == 216
    assert result["three"]["rings"] == [[-25, 72], [0, 72], [25, 72]]
    assert result["three"]["radius"] == 3.176
    # Two numbers, because that is how it is experienced: fine sideways, coarse
    # vertically. One worst-case figure would be the 25 degree ring gap and
    # would describe the sideways drag wrongly.
    assert result["three"]["sampling"] == (
        "±2.5° azimuth, 3 elevations (-25°, 0°, 25°)")


@requires_node
def test_a_rig_that_is_not_a_shell_is_refused(tmp_path):
    result = _shell_js(
        BUILD + """
        const aim = (position, centre) => ({
          position,
          forward: normalize([0, 1, 2].map((k) => centre[k] - position[k])),
        });
        // Two radii about one centre: half the stations are twice as far out, so
        // snapping between them would jump the subject's size.
        const twoRadii = {poses: [
          aim([3, 0, 0], [0, 0, 0]), aim([0, 0, 3], [0, 0, 0]),
          aim([-6, 0, 0], [0, 0, 0]), aim([0, 0, -6], [0, 0, 0]),
        ]};
        // Every camera pointing the same way: no convergence, so no centre.
        const parallel = {poses: [
          {position: [0, 0, 0], forward: [0, 0, 1]},
          {position: [1, 0, 0], forward: [0, 0, 1]},
          {position: [2, 0, 0], forward: [0, 0, 1]},
          {position: [3, 0, 0], forward: [0, 0, 1]},
        ]};
        // Poses with no forward at all -- a rig from a corpus that recorded
        // only positions.
        const poseless = {poses: orbit.poses.map((p) => ({position: p.position}))};
        process.stdout.write(JSON.stringify({
          twoRadii: shellOf(twoRadii), parallel: shellOf(parallel),
          poseless: shellOf(poseless),
          tiny: shellOf({poses: [{position: [0, 0, 1], forward: [0, 0, -1]}]}),
          none: shellOf(null),
        }));
    """, tmp_path, "s2.mjs")
    # Snapping a free camera onto an arbitrary cloud of capture positions would
    # move it somewhere the user never pointed it. One radius about one look-at
    # point, or no snapping.
    assert result == {"twoRadii": None, "parallel": None, "poseless": None,
                      "tiny": None, "none": None}


@requires_node
def test_a_tilted_ring_is_still_one_radius(tmp_path):
    """The export tilts spherically rather than lifting, and this is why.

    Lifting a ring straight up puts its cameras `sqrt(r^2 + h^2)` from the
    subject, so it would fail the one-radius guard -- and, worse, would pass a
    loosened one while quietly shrinking the subject in the tilted rings.
    """
    result = _shell_js("""
        const lifted = {poses: []};
        for (const h of [0, 1.3]) {
          for (let i = 0; i < 8; i++) {
            const yaw = i * Math.PI / 4;
            const position = [3 * Math.sin(yaw), h, 3 * Math.cos(yaw)];
            const forward = normalize([-position[0], -h, -position[2]]);
            lifted.poses.push({position, forward});
          }
        }
        process.stdout.write(JSON.stringify({lifted: shellOf(lifted)}));
    """, tmp_path, "s3.mjs")
    assert result["lifted"] is None


@requires_node
def test_the_nearest_station_is_the_nearest_one(tmp_path):
    result = _shell_js(
        BUILD + """
        const shell = shellOf(orbit);
        const at = (yawDeg, pitchDeg = 0) => {
          const y = yawDeg * Math.PI / 180, p = (pitchDeg || 0) * Math.PI / 180;
          return nearestStation(shell, [Math.sin(y) * Math.cos(p), Math.sin(p),
                                        Math.cos(y) * Math.cos(p)]);
        };
        process.stdout.write(JSON.stringify({
          exact: at(90), next: at(85),
          // Either side of the 87.5 degree midpoint between them.
          aboveMid: at(87.6), belowMid: at(87.4),
          // The seam: yaw is never differenced, so there is nothing to wrap.
          justUnder: at(-179), justOver: at(179), half: at(-180),
          wrapped: at(90 + 360),
          // A single ring absorbs any pitch -- there is nowhere else to go.
          tilted: at(90, 40),
        }));
    """, tmp_path, "s4.mjs")
    assert result["exact"] == 0                 # station 0 sits at +90
    assert result["next"] == 1                  # station 1 at +85
    assert result["aboveMid"] == 0
    assert result["belowMid"] == 1
    # -179, +179 and -180 all land on the station at -180, the short way round.
    assert result["justUnder"] == result["justOver"] == result["half"] == 54
    assert result["wrapped"] == 0               # a full turn is the same place
    assert result["tilted"] == 0


@requires_node
def test_dragging_up_moves_between_rings(tmp_path):
    result = _shell_js(
        BUILD + """
        const shell = shellOf(stack);
        const at = (yawDeg, pitchDeg) => {
          const y = yawDeg * Math.PI / 180, p = pitchDeg * Math.PI / 180;
          return nearestStation(shell, [Math.sin(y) * Math.cos(p), Math.sin(p),
                                        Math.cos(y) * Math.cos(p)]);
        };
        process.stdout.write(JSON.stringify({
          level: at(90, 0), up: at(90, 25), down: at(90, -25),
          // Either side of the midpoint between the level and raised rings.
          justBelowMid: at(90, 12.4), justAboveMid: at(90, 12.6),
          // Past the top ring there is nothing higher to snap to.
          overhead: at(90, 80),
          // Azimuth and elevation move independently.
          quarterUp: at(0, 25),
        }));
    """, tmp_path, "s5.mjs")
    # The rings were built 0, +25, -25, so their stations are laid out in that
    # order and station 0 of each ring is 72 apart.
    assert result["level"] == 0
    assert result["up"] == 72
    assert result["down"] == 144
    assert result["justBelowMid"] == 0
    assert result["justAboveMid"] == 72
    assert result["overhead"] == 72
    # A quarter turn on the raised ring: 18 stations round, on ring 1.
    assert result["quarterUp"] == 72 + 18


@requires_node
def test_the_explore_station_follows_both_axes(tmp_path):
    result = _shell_js(
        BUILD + """
        globalThis.app = {scenes: {b: stack}, scene: "b",
                          camera: {yaw: 0, pitch: 0}};
        globalThis.rig = () => app.scenes[app.scene];
        const seen = [];
        for (const [yaw, pitch] of [[90, 0], [0, 0], [0, 25], [-90, -25]]) {
          app.camera.yaw = yaw * Math.PI / 180;
          app.camera.pitch = pitch * Math.PI / 180;
          seen.push(exploreStation());
        }
        app.scenes.b = {poses: [{position: [0, 0, 1], forward: [0, 0, -1]}]};
        seen.push(exploreStation());
        process.stdout.write(JSON.stringify(seen));
    """, tmp_path, "s6.mjs")
    # 5 degrees a station starting at +90 and going down, so yaw 0 is station 18
    # and yaw -90 is station 36; +72 a ring, in the order they were built.
    assert result[:4] == [0, 18, 72 + 18, 144 + 36]
    # No shell, no station: pixels stay compare-only rather than being snapped
    # onto a viewpoint that does not exist.
    assert result[4] is None


@requires_node
def test_a_pixel_method_becomes_usable_in_explore_on_a_shell(tmp_path):
    body = BUILD + '''
        globalThis.REPRESENTATIONS = {
          gaussians: { geometry: true }, pixels: { geometry: false },
        };
        globalThis.spec = (clip) => clip && REPRESENTATIONS[clip.representation];
        globalThis.hasGeometry = (clip) => {
          const s = spec(clip); return s !== undefined && s.geometry;
        };
        globalThis.isPixels = (clip) => {
          const s = spec(clip); return s !== undefined && !s.geometry;
        };
        const clips = [
          { scene: "b", method: "vega", representation: "gaussians" },
          { scene: "b", method: "rerf", representation: "pixels", camera: 0 },
          { scene: "b", method: "live", representation: "pixels", camera: null },
        ];
        globalThis.app = { scenes: { b: stack }, index: { clips } };
        const onShell = methodsFor("b", "explore");
        globalThis.app = { scenes: { b: {poses: []} }, index: { clips } };
        process.stdout.write(JSON.stringify({
          onShell, offShell: methodsFor("b", "explore"),
        }));
    '''
    script = tmp_path / "s7.mjs"
    script.write_text(
        PRELUDE
        + _extract("SHELL_TOLERANCE", "RING_MERGE_DEG", "lookAtCentre", "shellOf",
                   "methodsFor")
        + "\n" + textwrap.dedent(body)
    )
    finished = subprocess.run([NODE, str(script)], capture_output=True, text=True,
                              timeout=60)
    if finished.returncode:
        raise AssertionError(finished.stderr)
    result = json.loads(finished.stdout)

    on = {entry["method"]: entry["usable"] for entry in result["onShell"]}
    # ReRF's views were rendered around a shell, so a drag has somewhere to land.
    assert on["rerf"] is True
    assert on["vega"] is True
    # A live clip has no station -- it renders its own camera -- so there is
    # nothing to snap it to.
    assert on["live"] is False
    off = {entry["method"]: entry["usable"] for entry in result["offShell"]}
    assert off == {"vega": True, "rerf": False, "live": False}


def test_following_the_camera_is_debounced():
    """A spin across 216 stations must not be 216 downloads.

    Crossing a station changes which clip a pixel pane plays, so re-resolving on
    every pointermove would fetch a container per station -- hundreds of MB of
    pictures nobody stopped on. The geometry panes are unaffected either way:
    they redraw from the camera every frame.
    """
    page = viewer_path().read_text()
    start = page.index("function followCamera(")
    body = page[start:page.index("\n}\n", start)]
    assert "clearTimeout(followTimer)" in body
    assert "setTimeout(" in body
    # And the resident check is what makes the one that does fire cheap.
    start = page.index("  async downloadAll(")
    assert "if (this.resident)" in page[start:page.index("\n  }\n", start)]


def test_dragging_the_pitch_follows_the_camera_too():
    """Both axes now select a station, so both have to trigger the follow.

    Hooked to the branch rather than to the handler: the pan branch moves the
    look-at target, which changes what is being looked at and not from where.
    """
    page = viewer_path().read_text()
    start = page.index("panesEl.addEventListener(\"pointermove\"")
    body = page[start:page.index("\n  });\n", start)]
    orbit = body[body.index("} else {"):]
    assert "camera.pitch" in orbit and "followCamera()" in orbit
    assert "followCamera()" not in body[:body.index("} else {")]
