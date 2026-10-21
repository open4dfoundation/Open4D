"""`open4d.stream` -- the one-line export-and-serve in Open4D's public API.

Tested from here rather than from Open4D's own suite because the verb only
does anything when this package is installed, and Open4D's tests must keep
passing without it.
"""

from __future__ import annotations

import json
import sys
from unittest import mock

import numpy as np
import pytest
from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh

import open4d

pytestmark = pytest.mark.cpu


def sequence_of(count: int = 3, fps: float = 30) -> Sequence:
    geometry = TriangleMesh(
        np.asarray([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32),
        np.asarray([[0, 1, 2]], dtype=np.uint32),
    )
    return Sequence(
        MemoryFrameProvider([Frame(i, i / fps, geometry) for i in range(count)])
    )


def test_a_sequence_is_exported_and_served(tmp_path):
    server = open4d.stream(
        sequence_of(),
        out_dir=tmp_path,
        name="capture",
        rungs=["ply"],
        open_browser=False,
        block=False,
    )
    try:
        index = json.loads((tmp_path / "view.json").read_text())
        assert [clip["name"] for clip in index["clips"]] == ["capture"]
        assert server.bundle_dir == tmp_path
        # The counters the server records into are what makes a playback
        # measurable; the verb must not hide them.
        assert server.monitor is not None
    finally:
        server.shutdown()


@pytest.mark.parametrize("as_path", [False, True])
def test_browser_stream_uses_source_timestamps(tmp_path, as_path):
    source = sequence_of(fps=23.976)
    if as_path:
        from open4d.io import write_sequence
        source = write_sequence(source, tmp_path / "source")
    server = open4d.stream(source, out_dir=tmp_path / "bundle", name="timed",
                           fps=10, open_browser=False, block=False)
    try:
        index = json.loads((server.bundle_dir / "view.json").read_text())
        assert index["fps"] == pytest.approx(23.976)
    finally:
        server.shutdown()


def test_a_ladder_becomes_variants(tmp_path):
    server = open4d.stream(
        sequence_of(),
        out_dir=tmp_path,
        name="capture",
        rungs=["ply", "draco@8"],
        open_browser=False,
        block=False,
    )
    try:
        index = json.loads((tmp_path / "view.json").read_text())
        clip = index["clips"][0]
        assert [variant["name"] for variant in clip["variants"]] == ["draco@8"]
        assert clip["variants"][0]["bytes"] > 0
    finally:
        server.shutdown()


def test_streaming_a_loaded_sequence_needs_a_name(tmp_path):
    # A path supplies one; an in-memory sequence has nothing to take it from,
    # and the name is both the pane's label and the frame directory.
    with pytest.raises(ValueError, match="needs a name"):
        open4d.stream(sequence_of(), out_dir=tmp_path, block=False)


def test_a_missing_streamer_says_how_to_install_it(tmp_path):
    # `sys.modules[name] = None` is what makes `import name` raise, so this
    # exercises the real except branch rather than a stubbed one.
    with mock.patch.dict(sys.modules, {"streamer": None}):
        with pytest.raises(open4d.StreamerDependencyError) as caught:
            open4d.stream(sequence_of(), out_dir=tmp_path, name="capture")
    assert "pip install -e open4d/streamer" in str(caught.value)


def splats(offset: float = 0.0):
    return open4d.GaussianSplats(
        positions=np.arange(6, dtype=np.float32).reshape(2, 3) + offset,
        scales=np.full((2, 3), 0.5, dtype=np.float32),
        rotations=np.tile(np.array([1, 0, 0, 0], dtype=np.float32), (2, 1)),
        opacities=np.array([0.2, 0.8], dtype=np.float32),
        spherical_harmonics=np.zeros((2, 1, 3), dtype=np.float32),
    )


def test_gaussian_frames_are_streamed_as_ply_by_default(tmp_path):
    # No browser keyword: a Gaussian sequence has no TCP meaning, so it must
    # not fall through to `send`.
    server = open4d.stream([splats(), splats(1.0)], name="run", fps=12,
                           out_dir=tmp_path, open_browser=False, block=False)
    try:
        index = json.loads((tmp_path / "view.json").read_text())
        clip = index["clips"][0]
        assert clip["representation"] == "gaussians"
        assert clip["detail"]["rung"] == "ply"
        assert index["fps"] == 12
    finally:
        server.shutdown()


def test_gaussian_frames_reach_the_browser_without_browser_keywords(monkeypatch):
    called = {}
    import open4d._streamer as bridge

    monkeypatch.setattr(bridge, "stream", lambda source, **options: called.update(options))
    open4d.stream([splats()])
    assert called == {}


@pytest.mark.parametrize("source, message", [
    (splats(), "is one frame"),
    (TriangleMesh(np.zeros((3, 3), dtype=np.float32),
                  np.asarray([[0, 1, 2]], dtype=np.uint32)), "is one frame"),
    (42, "cannot stream a int"),
    ([], "cannot stream a list"),
])
def test_what_cannot_be_streamed_is_a_type_error(tmp_path, source, message):
    with pytest.raises(TypeError, match=message):
        open4d.stream(source, name="x", out_dir=tmp_path, block=False)


def test_a_neural_field_is_refused_before_anything_is_written(tmp_path):
    native = open4d.NativeSequence.__new__(open4d.NativeSequence)
    native.codec, native.representation = "rerf", "neural_field"
    with pytest.raises(TypeError, match="no browser can decode"):
        open4d.stream(native, name="x", out_dir=tmp_path / "bundle", block=False)
    assert not (tmp_path / "bundle").exists()


def test_the_link_and_monitor_reach_the_server(tmp_path):
    import streamer

    link, monitor = streamer.Link(capacity=5e6), streamer.Monitor()
    server = open4d.stream(sequence_of(), name="capture", rungs=["ply"],
                           link=link, monitor=monitor, out_dir=tmp_path,
                           open_browser=False, block=False)
    try:
        assert server.link is link
        assert server.monitor is monitor
    finally:
        server.shutdown()


def test_score_fills_in_each_rungs_quality(tmp_path):
    pytest.importorskip("scipy")
    from streamer import score

    server = open4d.stream(sequence_of(), name="capture", rungs=["ply"],
                           score=True, out_dir=tmp_path,
                           open_browser=False, block=False)
    try:
        clip = json.loads((tmp_path / "view.json").read_text())["clips"][0]
        assert clip["detail"]["quality"][score.METRIC] == score.LOSSLESS_DB
    finally:
        server.shutdown()


@pytest.mark.parametrize("representation", ["gaussian_splats", "neural_gaussians"])
def test_gaussian_native_sequences_are_accepted(representation):
    # Vega's NativeSequence says neural_gaussians, not gaussian_splats; the
    # benchmark found the check refusing it.
    from open4d._streamer import kind_of

    native = open4d.NativeSequence.__new__(open4d.NativeSequence)
    native.codec, native.representation = "vega", representation
    assert kind_of(native) == "gaussians"
