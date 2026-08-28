from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

import open4d
from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh

pytestmark = pytest.mark.cpu


def sequence() -> Sequence:
    mesh = TriangleMesh(
        [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]],
        [[0, 1, 2]],
    )
    return Sequence(MemoryFrameProvider([Frame(7, 1.25, mesh)]))


def test_top_level_codec_round_trip_and_unload(tmp_path):
    artifact = open4d.save(sequence(), tmp_path / "capture.o4d")

    loaded = open4d.load(artifact)
    np.testing.assert_array_equal(loaded[0].geometry.triangles, [[0, 1, 2]])
    assert loaded.closed is False

    assert open4d.unload(loaded) is None
    assert loaded.closed is True
    open4d.unload(loaded)
    with pytest.raises(RuntimeError, match="closed"):
        _ = loaded[0]
    with pytest.raises(RuntimeError, match="closed"):
        _ = loaded.timestamps


def test_top_level_load_keeps_mesh_files_as_import_sources(tmp_path):
    path = tmp_path / "frame.obj"
    path.write_text(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="ascii"
    )

    loaded = open4d.load(path)

    assert len(loaded) == 1
    np.testing.assert_array_equal(loaded[0].geometry.triangles, [[0, 1, 2]])


def test_top_level_load_rejects_conflicting_dispatch_overrides(tmp_path):
    path = tmp_path / "frame.obj"
    path.write_text(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="ascii"
    )

    with pytest.raises(TypeError, match="format.*codec"):
        open4d.load(path, format="obj", codec="npz")


@pytest.mark.parametrize("name", ("capture", "frame.obj", "capture.unknown"))
def test_top_level_save_requires_a_sequence_file_extension(tmp_path, name):
    with pytest.raises(ValueError, match="sequence-file extension"):
        open4d.save(sequence(), tmp_path / name)


def test_top_level_save_requires_a_codec_for_ambiguous_codec_suffix(tmp_path):
    with pytest.raises(ValueError, match=r"ambiguous.*\.v4d.*codec"):
        open4d.save(sequence(), tmp_path / "capture.v4d")


def test_top_level_api_exports_visualize():
    assert callable(open4d.visualize)
