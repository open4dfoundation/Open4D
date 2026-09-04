"""The client's half of the loop: measure the link, pick a rung, fetch it.

The pieces are cut out of `viewer.html` as shipped and run under Node, the same
way `test_scheduler.py` exercises the scheduler -- so what is tested is the
code the browser gets, not a copy of it.

The rule the client applies is deliberately *not* `streamer.policy`'s. That one
maximises total weighted quality, which for a comparison view is the wrong
objective: the way to maximise a sum is to make the panes unequal. These tests
pin the even-share rule so that difference stays a decision rather than a
divergence.
"""
from __future__ import annotations

import json
import shutil
import subprocess
import textwrap
from pathlib import Path

import pytest

from streamer import bundle
from streamer.client import viewer_path

pytestmark = pytest.mark.cpu

NODE = shutil.which("node")
requires_node = pytest.mark.skipif(NODE is None, reason="node is not installed")


def _extract(*names: str) -> str:
    page = viewer_path().read_text()
    chunks = []
    for name in names:
        for prefix in (f"function {name}(", f"class {name} ", f"const {name} ="):
            start = page.find(prefix)
            if start >= 0:
                break
        else:
            raise AssertionError(f"{name} is not defined in the viewer")
        end = (page.index(";\n", start) + 2 if prefix.startswith("const")
               else page.index("\n}\n", start) + 3)
        chunks.append(page[start:end])
    return "\n".join(chunks)


def run_js(body: str, tmp_path: Path, name: str = "a.mjs") -> object:
    script = tmp_path / name
    script.write_text(
        _extract("RateMeter", "rungsOf", "affordable") + "\n"
        + textwrap.dedent(body)
    )
    finished = subprocess.run(
        [NODE, str(script)], capture_output=True, text=True, timeout=120
    )
    if finished.returncode:
        raise AssertionError(finished.stderr)
    return json.loads(finished.stdout)


# ------------------------------------------------------------- the estimate ---


@requires_node
def test_the_first_sample_is_taken_as_the_rate(tmp_path):
    """With no history there is nothing to smooth towards, and starting from
    zero would spend the first several frames climbing out of it."""
    result = run_js("""
        const meter = new RateMeter();
        meter.record(1_000_000, 1.0);
        process.stdout.write(JSON.stringify(meter.bitsPerSecond));
    """, tmp_path)
    assert result == pytest.approx(8_000_000)


@requires_node
def test_the_estimate_moves_towards_a_changed_rate(tmp_path):
    result = run_js("""
        const meter = new RateMeter({ halfLife: 2 });
        meter.record(1_000_000, 1.0);            // 8 Mbit/s
        const before = meter.bitsPerSecond;
        for (let i = 0; i < 8; i++) meter.record(1_000_000, 8.0);   // 1 Mbit/s
        process.stdout.write(JSON.stringify({ before, after: meter.bitsPerSecond }));
    """, tmp_path)
    assert result["before"] == pytest.approx(8_000_000)
    assert 1_000_000 <= result["after"] < 2_500_000


@requires_node
def test_a_tiny_response_is_ignored(tmp_path):
    """A cache hit completes in microseconds and would read as a gigabit link,
    which is exactly the over-estimate that then overshoots the real one."""
    result = run_js("""
        const meter = new RateMeter();
        meter.record(1_000_000, 1.0);
        meter.record(300, 0.000001);             // a 304, effectively
        process.stdout.write(JSON.stringify(meter.bitsPerSecond));
    """, tmp_path)
    assert result == pytest.approx(8_000_000)


@requires_node
def test_a_zero_duration_sample_is_ignored(tmp_path):
    """Rather than dividing by zero and poisoning the estimate with Infinity."""
    result = run_js("""
        const meter = new RateMeter();
        meter.record(1_000_000, 0);
        process.stdout.write(JSON.stringify(
            { rate: meter.bitsPerSecond, samples: meter.samples }));
    """, tmp_path)
    assert result == {"rate": 0, "samples": 0}


@requires_node
def test_reset_forgets_everything(tmp_path):
    result = run_js("""
        const meter = new RateMeter();
        meter.record(1_000_000, 1.0);
        meter.reset();
        process.stdout.write(JSON.stringify(
            { rate: meter.bitsPerSecond, samples: meter.samples }));
    """, tmp_path)
    assert result == {"rate": 0, "samples": 0}


# ---------------------------------------------------------------- the ladder ---


def a_clip(*, default_bytes=33_500, variants=(("low", 3_035), ("medium", 9_902))):
    clip = {
        "name": "c", "representation": "pixels",
        "frames": [f"c/frame_{i:04d}.jpg" for i in range(30)],
        "detail": {"bytes_per_frame": default_bytes,
                   "quality": {"psnr": 47.2}} if default_bytes else {},
        "variants": [
            {"name": name, "bytes": per * 30,
             "frames": [f"c@{name}/frame_{i:04d}.jpg" for i in range(30)],
             "quality": {"psnr": 40.0}}
            for name, per in variants
        ],
    }
    return clip


@requires_node
def test_the_ladder_is_read_cheapest_first(tmp_path):
    result = run_js(f"""
        const rungs = rungsOf({json.dumps(a_clip())}, 30);
        process.stdout.write(JSON.stringify(rungs.map(
            (r) => [r.name, Math.round(r.bitsPerSecond / 1000)])));
    """, tmp_path)
    assert [name for name, _ in result] == ["low", "medium", "default"]
    # 3035 B/frame x 8 x 30 fps = 728 kbit/s
    assert result[0][1] == pytest.approx(728, abs=2)
    assert result[-1][1] == pytest.approx(8040, abs=5)


@requires_node
def test_the_default_needs_its_recorded_size_to_be_a_rung(tmp_path):
    """`streamer.metrics --write` records it. Without it the client can compare
    the rungs it might move to and not the one it is already playing."""
    result = run_js(f"""
        const rungs = rungsOf({json.dumps(a_clip(default_bytes=0))}, 30);
        process.stdout.write(JSON.stringify(rungs.map((r) => r.name)));
    """, tmp_path)
    assert result == ["low", "medium"]


@requires_node
def test_a_clip_with_no_frames_has_no_ladder(tmp_path):
    result = run_js("""
        process.stdout.write(JSON.stringify([
            rungsOf(null, 30).length,
            rungsOf({ frames: [] }, 30).length,
        ]));
    """, tmp_path)
    assert result == [0, 0]


@requires_node
def test_an_empty_variant_is_skipped(tmp_path):
    """A rung with no frames is not playable, and picking it would blank the
    pane rather than degrade it."""
    clip = a_clip()
    clip["variants"].append({"name": "broken", "bytes": 10, "frames": []})
    result = run_js(f"""
        const rungs = rungsOf({json.dumps(clip)}, 30);
        process.stdout.write(JSON.stringify(rungs.map((r) => r.name)));
    """, tmp_path)
    assert "broken" not in result


# ------------------------------------------------------------- the decision ---


@requires_node
def test_the_best_affordable_rung_is_taken(tmp_path):
    result = run_js(f"""
        const rungs = rungsOf({json.dumps(a_clip())}, 30);
        const at = (share) => affordable(rungs, share).name;
        process.stdout.write(JSON.stringify({{
            starved: at(100e3), low: at(1e6), medium: at(3e6),
            almost: at(8e6), plenty: at(20e6),
        }}));
    """, tmp_path)
    assert result == {
        "starved": "low",       # nothing fits; the cheapest rather than nothing
        "low": "low",
        "medium": "medium",
        "almost": "medium",     # 8.04 Mbit/s default does not fit in 8
        "plenty": "default",
    }


@requires_node
def test_a_starved_pane_shows_the_cheapest_rung_rather_than_nothing(tmp_path):
    """A blank pane tells a viewer less than a coarse one, and the frames are
    on disk either way -- this is a bundle, not a live encoder, so overrunning
    costs lateness and not absence."""
    result = run_js(f"""
        const rungs = rungsOf({json.dumps(a_clip())}, 30);
        process.stdout.write(JSON.stringify(affordable(rungs, 0).name));
    """, tmp_path)
    assert result == "low"


@requires_node
def test_no_rungs_means_no_choice(tmp_path):
    result = run_js("""
        process.stdout.write(JSON.stringify(affordable([], 1e9)));
    """, tmp_path)
    assert result is None


# ------------------------------------------------- wired into the page ---


def test_the_scheduler_reports_to_a_meter():
    """Injected, not reached for as a global: a scheduler that read one could
    not be run outside a page, and this one is."""
    page = viewer_path().read_text()
    start = page.index("class Scheduler")
    body = page[start:page.index("\n}\n", start)]
    assert "meter = null" in body            # injected with a default
    assert "if (this.meter)" in body         # and optional
    assert "this.meter.record(" in body


def test_the_scheduler_fetches_the_chosen_rung():
    page = viewer_path().read_text()
    start = page.index("class Scheduler")
    body = page[start:page.index("\n}\n", start)]
    assert "get frames()" in body
    assert "this.root + this.frames[index]" in body
    # Not the clip's own list, which would ignore the choice entirely.
    assert "this.clip.frames[index]" not in body


def test_switching_rung_keeps_the_cache():
    """Renditions share a timeline, so a decoded frame is still the right
    picture for its index. Dropping the cache would re-fetch what is in hand,
    stalling exactly when the link is under pressure."""
    page = viewer_path().read_text()
    start = page.index("  useRung(rung) {")
    body = page[start:page.index("\n  }\n", start)]
    assert "cache" not in body
    assert "release" not in body


def test_the_decision_runs_before_the_fetch():
    page = viewer_path().read_text()
    start = page.index("function tick(")
    body = page[start:page.index("\n}\n", start)]
    assert body.index("chooseRungs()") < body.index("showFrame(app.frame + 1)")


def test_adaptation_can_be_pinned_off():
    """Comparing two methods usually means wanting both at their best whatever
    the link can sustain; letting the link decide would quietly make the
    comparison about bandwidth."""
    page = viewer_path().read_text()
    start = page.index("function chooseRungs(")
    body = page[start:page.index("\n}\n", start)]
    assert "if (!app.adapt)" in body
    assert "useRung(null)" in body
    assert 'id="adapt"' in page


def test_the_client_splits_the_budget_evenly():
    """The decision that separates this from `streamer.policy`. If it ever
    becomes a utility maximisation, this test should be the thing that objects.
    """
    page = viewer_path().read_text()
    start = page.index("function chooseRungs(")
    body = page[start:page.index("\n}\n", start)]
    assert "rate / panes.length" in body


def test_live_panes_are_left_alone():
    """A live stream has no frame list, so there is no rung to choose."""
    page = viewer_path().read_text()
    start = page.index("function chooseRungs(")
    body = page[start:page.index("\n}\n", start)]
    assert "!isLive(pane.clip)" in body
