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


def sequence_of(count: int = 3) -> Sequence:
    geometry = TriangleMesh(
        np.asarray([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32),
        np.asarray([[0, 1, 2]], dtype=np.uint32),
    )
    return Sequence(
        MemoryFrameProvider([Frame(i, i / 30, geometry) for i in range(count)])
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
