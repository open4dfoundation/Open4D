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
        for prefix in (f"function {name}(", f"const {name} = "):
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
