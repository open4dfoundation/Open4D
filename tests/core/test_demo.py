from __future__ import annotations

import numpy as np
import pytest

from open4d import TopologyMode
from open4d.demo import mesh_sequence, write_demo
from open4d.io import open_sequence


pytestmark = pytest.mark.cpu


def test_wave_moves_with_fixed_nondegenerate_connectivity():
    with mesh_sequence(side=5, frames=12, fps=24) as sequence:
        assert sequence.topology is TopologyMode.FIXED
        assert sequence.has_vertex_correspondence is True
        assert sequence.timestamps == tuple(index / 24 for index in range(12))
        first = sequence[0].geometry
        later = sequence[3].geometry
        assert first.positions.shape == (25, 3)
        assert first.triangles.shape == (32, 3)
        assert first.positions.dtype == np.float32
        assert first.triangles.dtype == np.uint32
        np.testing.assert_array_equal(first.triangles, later.triangles)
        np.testing.assert_array_equal(first.positions[:, :2], later.positions[:, :2])
        assert not np.allclose(first.positions[:, 2], later.positions[:, 2])
        corners = first.positions[first.triangles]
        assert np.all(np.cross(corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0])[:, 2] > 0)
        assert first.normals is None and first.colors is None and not first.attributes
        # Editing a frame must not affect later reads.
        first.positions[:] = 42
        assert not np.any(sequence[0].geometry.positions == 42)


def test_export_preserves_geometry_timing_and_provenance(tmp_path):
    path = write_demo(tmp_path / "wave", side=4, frames=3, fps=12.5)
    assert (path / "LICENSE").read_text().startswith("MIT License")
    assert "SINRG Lab" in (path / "LICENSE").read_text()
    assert "--side 4 --frames 3 --fps 12.5" in (path / "README.md").read_text()
    assert len(list(path.glob("*.ply"))) == 3
    with mesh_sequence(side=4, frames=3, fps=12.5) as expected, open_sequence(path) as actual:
        assert actual.metadata == expected.metadata
        assert actual.metadata["license"] == "MIT"
        assert actual.topology is TopologyMode.FIXED
        assert actual.has_vertex_correspondence is True
        assert actual.timestamps == expected.timestamps
        for left, right in zip(expected, actual):
            assert left.frame_index == right.frame_index
            np.testing.assert_array_equal(left.geometry.positions, right.geometry.positions)
            np.testing.assert_array_equal(left.geometry.triangles, right.geometry.triangles)


@pytest.mark.parametrize("options", [
    {"side": 1}, {"side": True}, {"side": 3.5}, {"frames": 0}, {"frames": False},
    {"fps": 0}, {"fps": -1}, {"fps": float("nan")}, {"fps": float("inf")}, {"fps": True},
])
def test_invalid_parameters_do_not_create_destination(tmp_path, options):
    with pytest.raises((TypeError, ValueError)):
        write_demo(tmp_path / "wave", **options)
    assert list(tmp_path.iterdir()) == []


@pytest.mark.parametrize("directory", [False, True])
def test_existing_destination_is_never_replaced(tmp_path, directory):
    path = tmp_path / "wave"
    if directory:
        path.mkdir()
        sentinel = path / "keep.txt"
    else:
        sentinel = path
    sentinel.write_text("original")
    with pytest.raises(FileExistsError, match="choose a new folder"):
        write_demo(path, side=3, frames=2)
    assert sentinel.read_text() == "original"


def test_failed_export_does_not_leave_partial_sample(tmp_path, monkeypatch):
    def fail(sequence, destination, **kwargs):
        destination.mkdir()
        (destination / "partial.ply").write_text("partial")
        raise OSError("disk full")

    monkeypatch.setattr("open4d.io.write_sequence", fail)
    with pytest.raises(OSError, match="disk full"):
        write_demo(tmp_path / "wave", side=3, frames=2)
    assert list(tmp_path.iterdir()) == []
