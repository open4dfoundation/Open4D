"""Clips rendered as they are watched.

The transport that makes this a streaming platform for the modules that cannot
be decoded client-side at all, and the only one measured here that fits a link
rather than a LAN: about 47 kB a rendered frame against 4.3 MB for a decoded
Gaussian one.
"""

from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path

import pytest

from streamer import bundle, live
from streamer.client import viewer_path

pytestmark = pytest.mark.cpu

NODE = shutil.which("node")
requires_node = pytest.mark.skipif(NODE is None, reason="node is not installed")

URL = "http://127.0.0.1:8768/stream"


# --------------------------------------------------------------- the clip ---


def test_a_live_clip_carries_a_url_and_no_frames():
    clip = live.mjpeg(URL, name="wall", scene="Vega live wall", method="vega-live")
    assert clip.stream == {"url": URL, "protocol": "mjpeg"}
    assert clip.frames == []
    assert clip.representation == "pixels"


def test_it_is_pixels_because_that_is_what_arrives():
    """Whatever the server rendered from, the client receives an image."""
    assert live.mjpeg(URL, name="w").representation == "pixels"


def test_the_scene_defaults_to_the_clip_name():
    """Its own scene, so Compare never presents it as sharing a rig pose."""
    assert live.mjpeg(URL, name="wall").scene == "wall"


def test_the_notes_say_it_is_live_and_where_it_is_reachable_from():
    notes = " ".join(live.mjpeg(URL, name="w").notes)
    assert "nothing to scrub" in notes
    assert "127.0.0.1:8768" in notes


def test_a_non_http_url_is_refused():
    for bad in ("rtsp://host/stream", "/local/path", "ws://host/s"):
        with pytest.raises(ValueError, match="not an http"):
            live.mjpeg(bad, name="w")


# ---------------------------------------------------------- the wire format ---


def test_validation_rejects_a_stream_without_a_url():
    with pytest.raises(ValueError, match="needs a url"):
        bundle.validate(
            bundle.Clip(name="w", representation="pixels", stream={"protocol": "mjpeg"})
        )


def test_validation_rejects_a_protocol_the_client_cannot_play():
    """Otherwise it renders nothing, with no error a producer would see."""
    with pytest.raises(ValueError, match="not one the client plays"):
        bundle.validate(
            bundle.Clip(
                name="w",
                representation="pixels",
                stream={"url": URL, "protocol": "webrtc"},
            )
        )


def test_validation_rejects_a_live_clip_that_also_lists_frames():
    with pytest.raises(ValueError, match="no frame list"):
        bundle.validate(
            bundle.Clip(
                name="w",
                representation="pixels",
                frames=["w/f0.jpg"],
                stream={"url": URL, "protocol": "mjpeg"},
            )
        )


def test_validation_rejects_a_clip_with_neither(tmp_path):
    with pytest.raises(ValueError, match="either frames or a stream"):
        bundle.write(
            tmp_path,
            title="t",
            source="s",
            clips=[bundle.Clip(name="empty", representation="pixels")],
        )


def test_a_live_clip_round_trips_through_a_manifest(tmp_path):
    clip = live.mjpeg(URL, name="wall", scene="Vega live wall")
    bundle.write(tmp_path, title="t", source="s", clips=[clip])
    stored = bundle.read(tmp_path)["clips"][0]
    assert stored["stream"]["url"] == URL
    assert stored["frames"] == []


def test_live_and_static_clips_coexist_in_one_bundle(tmp_path):
    static = bundle.Clip(
        name="s", representation="pixels", scene="rig", camera=0, frames=["s/f0.jpg"]
    )
    bundle.write(
        tmp_path,
        title="t",
        source="s",
        clips=[static, live.mjpeg(URL, name="wall", scene="live")],
    )
    stored = bundle.read(tmp_path)["clips"]
    assert [c.get("stream") is not None for c in stored] == [False, True]


# ------------------------------------------------------------- the client ---


def _cut(name: str) -> str:
    page = viewer_path().read_text()
    for prefix in (f"function {name}(", f"const {name} ="):
        start = page.find(prefix)
        if start < 0:
            continue
        line = page[start : page.index("\n", start)]
        if line.count("{") and line.count("{") == line.count("}"):
            return line
        end = (
            page.index(";\n", start) + 2
            if prefix.startswith("const")
            else page.index("\n}\n", start) + 3
        )
        return page[start:end]
    raise AssertionError(f"{name} is not defined in the viewer")


@requires_node
def test_the_client_recognises_a_live_clip(tmp_path):
    script = tmp_path / "live.mjs"
    script.write_text(
        _cut("isLive")
        + """
        process.stdout.write(JSON.stringify([
          isLive({stream: {url: "http://h/s", protocol: "mjpeg"}}),
          isLive({frames: ["a.jpg"]}),
          isLive({}),
          isLive(null),
        ]));
        """
    )
    finished = subprocess.run(
        [NODE, str(script)], capture_output=True, text=True, timeout=120
    )
    assert finished.returncode == 0, finished.stderr
    assert json.loads(finished.stdout) == [True, False, False, False]


def test_a_live_pane_bypasses_the_scheduler():
    """Structural: the browser owns an MJPEG connection, so a cache would fight it."""
    page = viewer_path().read_text()
    start = page.index("  setClip(clip) {")
    body = page[start : page.index("\n  }\n", start)]
    start_of_branch = body.index("if (isLive(clip))")
    # The branch ends at its own early return; slicing past that would pick up
    # the static path and its Scheduler, which is what this is asserting about.
    live_branch = body[start_of_branch : body.index("return;", start_of_branch)]
    assert "this.source = null" in live_branch
    assert "clip.stream.url" in live_branch
    assert "new Scheduler" not in live_branch


def test_the_transport_bar_is_disabled_for_a_live_scene():
    page = viewer_path().read_text()
    start = page.index("function updateTransport(")
    body = page[start : page.index("\n}\n", start)]
    assert "scrubFrame.disabled" in body
    assert "playPause.disabled" in body
    assert '"live"' in body
