"""Whole-page checks on the client, of the kind a browser would catch too late.

The page is edited by hand and served as one file, so a syntax error or a
dangling reference ships silently: the server returns 200, the browser reports
it in a console nobody is watching, and every pane is blank. These are the two
cheapest checks that catch that class of mistake -- the script parses, and the
representation registry actually evaluates.
"""

from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path

import pytest
from open4d.core import Representation

from streamer import representations
from streamer.client import viewer_path

pytestmark = pytest.mark.cpu

NODE = shutil.which("node")
requires_node = pytest.mark.skipif(NODE is None, reason="node is not installed")


def page() -> str:
    return viewer_path().read_text()


def script() -> str:
    """The page's script, which is where everything that can break lives."""
    source = page()
    start = source.index("<script>") + len("<script>")
    return source[start : source.rindex("</script>")]


def cut(name: str) -> str:
    """One top-level definition, by the name the page gives it.

    `async function X(` is tried first because `function X(` is a substring of
    it: matching the shorter one drops the `async` keyword, and the extracted
    text then fails to parse on its own `await`.
    """
    source = page()
    for prefix in (
        f"async function {name}(",
        f"function {name}(",
        f"const {name} =",
        f"let {name} =",
        f"class {name} ",
    ):
        start = source.find(prefix)
        if start < 0:
            continue
        if prefix.startswith(("const", "let")):
            return source[start : source.index(";\n", start) + 2]
        line = source[start : source.index("\n", start)]
        if line.count("{") and line.count("{") == line.count("}"):
            return line
        return source[start : source.index("\n}\n", start) + 3]
    raise AssertionError(f"{name} is not defined in the viewer")


@requires_node
def test_the_script_parses(tmp_path):
    path = tmp_path / "viewer.js"
    path.write_text(script())
    finished = subprocess.run(
        [NODE, "--check", str(path)], capture_output=True, text=True, timeout=120
    )
    assert finished.returncode == 0, finished.stderr


@requires_node
def test_the_representation_registry_evaluates(tmp_path):
    """It is a const literal that calls functions declared further down the file.

    That works only because those are function declarations and therefore
    hoisted. Reordering them into `const` arrow functions would leave the
    literal reading them in their temporal dead zone -- which fails at load,
    before anything renders, and only in a browser.
    """
    # Everything the literal reaches while it is being evaluated. Names the
    # literal only calls later -- parseDraco, loadDraco -- are not needed here,
    # which is why this is a list rather than the whole script: what is being
    # tested is that the literal can be built at load, not that the page runs.
    # The literal reaches `decodeFrame` and `decodeImage` while it is being
    # evaluated. Both are function declarations, so both are hoisted; the parsers
    # they eventually reach live in the worker and are not needed here.
    probe = "\n".join(
        cut(name) for name in ("REPRESENTATIONS", "decodeFrame", "decodeImage")
    ) + """
        const out = {};
        for (const [name, spec] of Object.entries(REPRESENTATIONS)) {
          out[name] = {
            decode: typeof spec.decode,
            geometry: spec.geometry,
            cacheSize: spec.cacheSize,
          };
        }
        process.stdout.write(JSON.stringify(out));
    """
    path = tmp_path / "registry.mjs"
    path.write_text(probe)
    finished = subprocess.run(
        [NODE, str(path)], capture_output=True, text=True, timeout=120
    )
    assert finished.returncode == 0, finished.stderr
    entries = json.loads(finished.stdout)

    assert set(entries) == {member.value for member in Representation}
    for name, entry in entries.items():
        assert entry["decode"] == "function", name
        assert entry["geometry"] is Representation(name).has_geometry, name
        assert isinstance(entry["cacheSize"], int) and entry["cacheSize"] > 0, name


@requires_node
def test_frames_display_in_order_under_varying_decode_latency(tmp_path):
    """Why playback has to be buffer-driven, demonstrated on both patterns.

    Decode latency varies per frame -- that is what a fetch does -- and an
    advance loop that fires on a wall clock without waiting overlaps its own
    calls, which then resolve in whatever order they finish. The result is not
    slow playback but *wrong* playback: the counter races ahead and the panes
    show whichever decode landed last. Measured here at 3,1,5,0,6,4,2 for a
    request of 0..6.

    This simulates the two patterns rather than driving the real `tick`, which
    needs a DOM. `test_the_playback_loop_waits_for_the_frame_it_asked_for`
    checks that the shipped loop still uses the pattern this one vindicates.
    """
    script = tmp_path / "loop.mjs"
    script.write_text(
        """
        const LATENCY = [40, 15, 60, 10, 50, 12, 45, 18, 55, 11];
        const FPS = 30;
        function makeShow(shown) {
          return async (index) => {
            await new Promise((r) => setTimeout(r, LATENCY[index % LATENCY.length]));
            shown.push(index);
          };
        }
        async function wallClock(steps) {
          const shown = []; const show = makeShow(shown);
          let frame = 0, last = 0, now = 0;
          for (let i = 0; i < steps; i++) {
            now += 1000 / FPS;
            if (now - last >= 1000 / FPS) { last = now; show(frame++); }
            await new Promise((r) => setTimeout(r, 1));
          }
          await new Promise((r) => setTimeout(r, 300));
          return shown;
        }
        async function bufferDriven(steps) {
          const shown = []; const show = makeShow(shown);
          let frame = 0, advancing = false;
          for (let i = 0; i < steps * 12; i++) {
            if (!advancing) {
              advancing = true;
              show(frame++).finally(() => { advancing = false; });
            }
            await new Promise((r) => setTimeout(r, 1));
            if (shown.length >= steps) break;
          }
          return shown;
        }
        process.stdout.write(JSON.stringify({
          wallClock: (await wallClock(10)).slice(0, 7),
          bufferDriven: (await bufferDriven(7)).slice(0, 7),
        }));
        """
    )
    finished = subprocess.run(
        [NODE, str(script)], capture_output=True, text=True, timeout=120
    )
    assert finished.returncode == 0, finished.stderr
    result = json.loads(finished.stdout)

    ordered = lambda seq: all(b > a for a, b in zip(seq, seq[1:]))
    assert not ordered(result["wallClock"]), result["wallClock"]
    assert ordered(result["bufferDriven"]), result["bufferDriven"]
    assert result["bufferDriven"] == sorted(result["bufferDriven"])


def test_the_playback_loop_waits_for_the_frame_it_asked_for():
    """Structural, because `tick` needs a DOM to run.

    Weak on its own, which is why it names what it is guarding: the invariant is
    at most one advance in flight, and the next interval timed from when a frame
    was actually shown.
    """
    source = page()
    start = source.index("function tick(")
    body = source[start : source.index("\n}\n", start)]
    assert "!app.advancing" in body, "the advance guard is gone"
    assert "app.advancing = true" in body
    assert ".finally(" in body, "the guard is never cleared"
    assert "app.lastAdvance = performance.now()" in body, (
        "the interval is timed from the clock again, not from the frame"
    )


def test_the_page_carries_no_external_dependency():
    """No build step and no CDN is what makes the page servable as one file."""
    source = page()
    for pattern in ("http://", "https://", "cdn.", "<script src"):
        assert pattern not in source, pattern


def test_every_playable_representation_has_a_client_entry():
    """A spec claiming playable with no entry renders an empty pane."""
    source = page()
    for spec in representations.playable():
        assert f"\n  {spec.name}: {{" in source, spec.name


def test_the_page_names_itself_the_same_way_everywhere():
    """The tab, the static header, and the fallback all said different things.

    The markup said "open4d streamer", the tab said "gs-tools view", and the
    fallback when a bundle carries no title said "gs-tools view" too -- so a
    bundle without a title rendered under a name that appears nowhere else.
    """
    page = viewer_path().read_text()
    assert "<title>Open4D Streaming Platform</title>" in page
    assert '<h1 id="title">Open4D Streaming Platform</h1>' in page
    assert 'index.title || "Open4D Streaming Platform"' in page
    # And nothing *displays* the old name. Matched as the string literal it
    # would have to be, not as any occurrence: the file's own comment names
    # `gs-tools view` because that is the command which serves the page, and
    # forbidding the words would forbid saying so.
    assert '"gs-tools view"' not in page


def test_the_header_does_not_print_the_bundle_s_input_paths():
    """It used to, and on this bundle that was five absolute paths.

    Provenance is still in the manifest and in each clip's `detail.source`,
    which the notes surface per pane. A header is for saying what this is.
    """
    page = viewer_path().read_text()
    assert 'id="source"' not in page
    # Not "the words never appear" -- the comment explaining the removal
    # mentions it. What must not exist is the assignment that put it on screen.
    assert "textContent = index.source" not in page
    assert 'getElementById("source")' not in page
    # The rule that existed only to wrap those paths went with them. The
    # selector, not the declaration: the comment above the change quotes the
    # declaration to explain why it is gone.
    assert "  .sub {" not in page


def test_the_tab_title_follows_the_bundle():
    """A bundle that names itself should name the tab too, so several open at
    once are distinguishable."""
    page = viewer_path().read_text()
    assert "document.title = title;" in page
