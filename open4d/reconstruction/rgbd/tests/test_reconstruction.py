import importlib
import sys
from types import SimpleNamespace

import numpy as np
import pytest

from open4d.reconstruction.rgbd import reconstruct


def test_reconstruct_reports_missing_open3d(monkeypatch):
    original = importlib.import_module

    def missing(name):
        if name == "open3d":
            raise ModuleNotFoundError("No module named 'open3d'", name="open3d")
        return original(name)

    monkeypatch.setattr(importlib, "import_module", missing)
    with pytest.raises(ModuleNotFoundError, match=r"open4d\[open3d\]"):
        reconstruct(np.ones((1, 4, 4)), intrinsics=(10, 10, 2, 2))


@pytest.mark.parametrize("kwargs,match", [
    ({"fps": 0}, "fps"),
    ({"intrinsics": (-1, 20, 16, 16)}, "intrinsics"),
    ({"camera_poses": np.zeros((4, 4))}, "rigid"),
    ({"color": np.zeros((1, 32, 32, 3))}, "uint8"),
])
def test_reconstruct_validates_before_loading_optional_dependencies(kwargs, match):
    settings = {"intrinsics": (20, 20, 16, 16), **kwargs}
    with pytest.raises(ValueError, match=match):
        reconstruct(np.ones((1, 32, 32)), **settings)


@pytest.mark.parametrize("version", ["0.18.0", "0.20.0"])
def test_reconstruct_rejects_unsupported_open3d_before_returning_a_sequence(monkeypatch, version):
    monkeypatch.setitem(sys.modules, "open3d", SimpleNamespace(__version__=version))
    with pytest.raises(RuntimeError, match=r"Open3D 0\.19\.x.*open3d>=0\.19,<0\.20"):
        reconstruct(np.ones((1, 32, 32)), intrinsics=(20, 20, 16, 16))


@pytest.mark.open3d
def test_reconstruct_depth_planes_preserves_motion_units_and_camera_pose():
    pytest.importorskip("open3d")
    depth = np.stack([np.full((48, 48), 1000), np.full((48, 48), 1500)])
    color = np.zeros((*depth.shape, 3), dtype=np.uint8)
    color[..., 0] = 255
    pose = np.eye(4)
    pose[0, 3] = 2
    with reconstruct(depth, color, intrinsics=(60, 60, 24, 24), camera_poses=pose,
                     voxel_size=0.025, truncation=0.1, fps=5) as sequence:
        assert sequence.timestamps == (0, 0.2)
        for index, frame in enumerate(sequence):
            mesh = frame.geometry
            assert len(mesh.triangles) > 100
            assert mesh.positions[:, 2] == pytest.approx(1 + index * 0.5, abs=0.015)
            assert np.median(mesh.positions[:, 0]) == pytest.approx(2, abs=0.08)
            assert np.mean(mesh.colors[:, 0]) > 0.95
            assert np.max(mesh.colors[:, 1:]) < 0.05


@pytest.mark.open3d
def test_reconstruct_two_cameras_and_empty_depth():
    pytest.importorskip("open3d")
    depth = np.full((1, 2, 48, 48), 1.0)
    poses = np.stack([np.eye(4), np.eye(4)])
    poses[1, 0, 3] = 0.5
    with reconstruct(depth, intrinsics=(60, 60, 24, 24), camera_poses=poses,
                     depth_scale=1, voxel_size=0.025, truncation=0.1) as sequence:
        mesh = sequence[0].geometry
        assert mesh.positions[:, 0].max() > 0.8
        assert mesh.positions[:, 0].min() < -0.3
        assert mesh.positions[:, 2] == pytest.approx(1, abs=0.015)
        assert mesh.colors is None
        assert mesh.normals is None  # Geometry-only output can go straight to a mesh codec.
    with reconstruct(np.zeros((1, 48, 48)), intrinsics=(60, 60, 24, 24)) as sequence:
        assert len(sequence[0].geometry.positions) == 0


@pytest.mark.open3d
@pytest.mark.parametrize("scale", [1e-50, 1e50])
def test_reconstruct_scales_depth_before_float32_conversion(scale):
    pytest.importorskip("open3d")
    depth = np.full((1, 48, 48), scale)
    with reconstruct(depth, intrinsics=(60, 60, 24, 24), depth_scale=scale,
                     voxel_size=0.025, truncation=0.1) as sequence:
        mesh = sequence[0].geometry
        assert len(mesh.triangles) > 100
        assert mesh.positions[:, 2] == pytest.approx(1, abs=0.015)
