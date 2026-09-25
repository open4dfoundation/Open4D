import errno

import numpy as np
import pytest

from open4d import Frame, GaussianCloud, MemoryFrameProvider, PointCloud, Sequence, TriangleMesh
from open4d.demo import mesh_sequence
from open4d.io import UnsupportedFeatureError, open_sequence, write_sequence


def sequence(geometry):
    return Sequence(MemoryFrameProvider([Frame(0, 0, geometry)],
                    has_vertex_correspondence=True))


def test_gaussians_are_not_silently_exported_as_meshes(tmp_path):
    cloud = GaussianCloud([[0., 0., 0.]], [[1., 1., 1.]], [[1., 0., 0., 0.]], [0.5])
    with pytest.raises(UnsupportedFeatureError, match="Gaussian"):
        write_sequence(sequence(cloud), tmp_path / "frames")
    assert not (tmp_path / "frames").exists()


def test_point_usd_export_has_supported_error(tmp_path):
    with pytest.raises(UnsupportedFeatureError, match="triangle.mesh"):
        write_sequence(sequence(PointCloud([[0., 0., 0.]])), tmp_path / "points.usda")


def test_stl_requires_lossy_opt_in_and_clears_correspondence(tmp_path):
    pytest.importorskip("trimesh")
    mesh = TriangleMesh([[0., 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0], [2, 2, 2]],
                        [[0, 1, 2], [0, 2, 3]])
    with pytest.raises(UnsupportedFeatureError, match="allow_lossy"):
        write_sequence(sequence(mesh), tmp_path / "strict", format="stl")
    path = write_sequence(sequence(mesh), tmp_path / "lossy", format="stl", allow_lossy=True)
    with open_sequence(path) as decoded:
        assert decoded.has_vertex_correspondence is False
        assert decoded.has_constant_vertex_count is None


@pytest.mark.parametrize("suffix", ["stl", "glb", "gltf"])
def test_face_only_formats_reject_empty_connectivity(tmp_path, suffix):
    mesh = TriangleMesh([[0., 0., 0.]], np.empty((0, 3), dtype=np.uint32))
    with pytest.raises(UnsupportedFeatureError, match="triangles"):
        write_sequence(sequence(mesh), tmp_path / "frames", format=suffix, allow_lossy=True)


@pytest.mark.parametrize("suffix", ["usda", "usdc", "usdz"])
def test_reopening_replaced_usd_reads_new_file_with_old_handle_open(tmp_path, suffix):
    pytest.importorskip("pxr")
    path = write_sequence(mesh_sequence(side=2, frames=2), tmp_path / f"take.{suffix}")
    with open_sequence(path) as old:
        write_sequence(mesh_sequence(side=2, frames=5), path, overwrite=True)
        with open_sequence(path) as new:
            assert len(new) == 5
            assert len(old) == 2
            assert old[1].frame_index == 1


def test_publish_without_hard_links_preserves_existing_files(tmp_path, monkeypatch):
    from open4d import _files

    def unsupported(*args):
        raise OSError(errno.EOPNOTSUPP, "hard links unsupported")

    monkeypatch.setattr(_files.os, "link", unsupported)
    temporary, destination = tmp_path / "temporary", tmp_path / "destination"
    temporary.write_bytes(b"new")
    _files.publish_file(temporary, destination)
    assert destination.read_bytes() == b"new"
    temporary.write_bytes(b"other")
    with pytest.raises(FileExistsError):
        _files.publish_file(temporary, destination)
    assert destination.read_bytes() == b"new"


def test_usd_snapshot_resolves_relative_sublayers(tmp_path):
    pytest.importorskip("pxr")
    from pxr import Sdf

    write_sequence(mesh_sequence(side=2, frames=2), tmp_path / "geometry.usda")
    root = Sdf.Layer.CreateNew(str(tmp_path / "root.usda"))
    root.subLayerPaths = ["geometry.usda"]
    root.Save()
    with open_sequence(tmp_path / "root.usda") as decoded:
        assert len(decoded) == 2
