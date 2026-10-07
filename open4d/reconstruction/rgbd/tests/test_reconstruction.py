import importlib
import json
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


@pytest.mark.parametrize("kwargs,error,match", [
    ({"device": "gpu"}, ValueError, "device must be"),
    ({"device": "cuda:x"}, ValueError, "device must be"),
    ({"device": "cuda:-1"}, ValueError, "device must be"),
    ({"device": 0}, TypeError, "device must be None or a string"),
    ({"block_count": 100}, TypeError, "block_count applies only with device"),
    ({"device": "cpu", "block_count": 0}, ValueError, "block_count"),
    ({"timestamps": (0, 1)}, ValueError, "one per frame"),
    ({"timestamps": (float("nan"),)}, ValueError, "finite"),
    ({"timestamps": ("0",)}, ValueError, "real numbers"),
    ({"timestamps": (0,), "fps": 30}, TypeError, "mutually exclusive"),
    ({"refine_poses": 1}, TypeError, "refine_poses must be bool"),
])
def test_reconstruct_validates_new_options_before_loading_open3d(monkeypatch, kwargs, error, match):
    monkeypatch.setitem(sys.modules, "open3d", None)
    with pytest.raises(error, match=match):
        reconstruct(np.ones((1, 32, 32)), intrinsics=(20, 20, 16, 16), **kwargs)


def test_reconstruct_refinement_needs_fixed_poses(monkeypatch):
    monkeypatch.setitem(sys.modules, "open3d", None)
    poses = np.broadcast_to(np.eye(4), (2, 2, 4, 4)).copy()
    poses[1, 1, 0, 3] = 0.1
    with pytest.raises(ValueError, match="fixed across frames"):
        reconstruct(np.ones((2, 2, 32, 32)), intrinsics=(20, 20, 16, 16),
                    camera_poses=poses, refine_poses=True)


@pytest.mark.open3d
def test_reconstruct_timestamps_replace_fps():
    pytest.importorskip("open3d")
    timestamps = (0.0, 0.05, 0.15)
    sequence = reconstruct(np.ones((3, 8, 8)), intrinsics=(20, 20, 4, 4), timestamps=timestamps)
    assert sequence.timestamps == timestamps
    assert "fps" not in sequence.metadata
    assert sequence.fps == pytest.approx(2 / 0.15)
    with pytest.raises(ValueError, match="strictly increasing"):
        reconstruct(np.ones((3, 8, 8)), intrinsics=(20, 20, 4, 4), timestamps=(0, 0.1, 0.1))


def plane_mesh_distance(mesh, reference):
    o3d = pytest.importorskip("open3d")
    cloud = o3d.geometry.PointCloud(o3d.utility.Vector3dVector(mesh.positions.astype(np.float64)))
    target = o3d.geometry.PointCloud(o3d.utility.Vector3dVector(reference.positions.astype(np.float64)))
    return float(np.mean(cloud.compute_point_cloud_distance(target)))


@pytest.mark.open3d
def test_reconstruct_tensor_cpu_matches_legacy_volume():
    pytest.importorskip("open3d")
    depth = np.stack([np.full((48, 48), 1000), np.full((48, 48), 1500)])
    color = np.zeros((*depth.shape, 3), dtype=np.uint8)
    color[..., 0] = 255
    pose = np.eye(4)
    pose[0, 3] = 2
    settings = {"intrinsics": (60, 60, 24, 24), "camera_poses": pose,
                "voxel_size": 0.025, "truncation": 0.1, "fps": 5}
    legacy = reconstruct(depth, color, **settings)
    # A tiny initial capacity also exercises Open3D growing the block hash map.
    tensor = reconstruct(depth, color, device="cpu:0", block_count=2, **settings)
    assert tensor.timestamps == legacy.timestamps
    assert tensor.metadata == {**legacy.metadata, "device": "CPU:0"}
    for index in range(2):
        mesh, reference = tensor[index].geometry, legacy[index].geometry
        assert len(mesh.triangles) > 100
        assert mesh.positions[:, 2] == pytest.approx(1 + index * 0.5, abs=0.015)
        assert plane_mesh_distance(mesh, reference) < 0.025
        assert plane_mesh_distance(reference, mesh) < 0.025
        assert np.mean(mesh.colors[:, 0]) > 0.95
        assert np.max(mesh.colors[:, 1:]) < 0.05
        assert mesh.normals is None


@pytest.mark.open3d
def test_reconstruct_tensor_without_color_and_with_empty_depth():
    pytest.importorskip("open3d")
    with reconstruct(np.full((1, 48, 48), 1.0), intrinsics=(60, 60, 24, 24), depth_scale=1,
                     voxel_size=0.025, truncation=0.1, device="CPU") as sequence:
        mesh = sequence[0].geometry
        assert len(mesh.triangles) > 100 and mesh.colors is None
    color = np.zeros((1, 48, 48, 3), dtype=np.uint8)
    with reconstruct(np.zeros((1, 48, 48)), color, intrinsics=(60, 60, 24, 24),
                     device="cpu") as sequence:
        mesh = sequence[0].geometry
        assert len(mesh.positions) == 0 and mesh.colors.shape == (0, 3)


@pytest.mark.open3d
def test_reconstruct_rejects_cuda_without_cuda_before_any_work(monkeypatch):
    o3d = pytest.importorskip("open3d")
    from open4d.reconstruction.rgbd import _reconstruction

    monkeypatch.setattr(o3d.core.cuda, "is_available", lambda: False)
    monkeypatch.setattr(_reconstruction, "_refine_poses", lambda *args: pytest.fail("ICP ran"))
    poses = np.stack([np.eye(4), np.eye(4)])
    with pytest.raises(RuntimeError, match=r"CUDA:1.*is_available\(\) is False"):
        reconstruct(np.ones((1, 2, 32, 32)), intrinsics=(20, 20, 16, 16), camera_poses=poses,
                    refine_poses=True, device="CUDA:1")


def look_at(eye, target):
    eye = np.asarray(eye, dtype=np.float64)
    forward = np.asarray(target) - eye
    forward /= np.linalg.norm(forward)
    right = np.cross((0, 1, 0), forward)
    right /= np.linalg.norm(right)
    pose = np.eye(4)
    pose[:3, :3] = np.column_stack((right, np.cross(forward, right), forward))
    pose[:3, 3] = eye
    return pose


def box_scene(o3d):
    # A wall, a floor and three boxes at different depths constrain all six
    # degrees of freedom of point-to-plane ICP. Camera axes: y down, z forward.
    scene = o3d.geometry.TriangleMesh()
    for size, center in (((3, 2, 0.05), (0, 0, 2.6)), ((3, 0.05, 3), (0, 0.55, 1.5)),
                         ((0.4, 0.5, 0.4), (-0.5, 0.3, 1.8)), ((0.3, 0.3, 0.3), (0.35, 0.4, 1.5)),
                         ((0.25, 0.7, 0.25), (0.1, 0.2, 2.2))):
        box = o3d.geometry.TriangleMesh.create_box(*size)
        box.translate(np.asarray(center) - np.asarray(size) / 2)
        scene += box
    raycaster = o3d.t.geometry.RaycastingScene()
    raycaster.add_triangles(o3d.t.geometry.TriangleMesh.from_legacy(scene))
    return raycaster


def render_depth(o3d, scene, pose, intrinsics, width, height):
    fx, fy, cx, cy = intrinsics
    rows, columns = np.mgrid[0:height, 0:width]
    # Rays with unit z in the camera frame make the hit distance the depth.
    rays = np.stack(((columns - cx) / fx, (rows - cy) / fy, np.ones((height, width))), -1)
    directions = rays.reshape(-1, 3) @ pose[:3, :3].T
    origins = np.broadcast_to(pose[:3, 3], directions.shape)
    hits = scene.cast_rays(o3d.core.Tensor(np.hstack((origins, directions)).astype(np.float32)))
    depth = hits["t_hit"].numpy().reshape(height, width)
    depth[~np.isfinite(depth)] = 0
    return depth


def pose_error(pose, truth):
    delta = np.linalg.inv(truth) @ pose
    angle = np.degrees(np.arccos(np.clip((np.trace(delta[:3, :3]) - 1) / 2, -1, 1)))
    return angle, np.linalg.norm(pose[:3, 3] - truth[:3, 3])


@pytest.mark.open3d
def test_reconstruct_refines_perturbed_camera_pose_with_icp():
    o3d = pytest.importorskip("open3d")
    scene = box_scene(o3d)
    intrinsics, width, height = (120, 120, 80, 60), 160, 120
    truth = np.stack([look_at((-0.3, -0.2, 0), (0, 0.2, 1.9)),
                      look_at((0.6, -0.3, 0.2), (0, 0.2, 1.9))])
    depth = np.stack([render_depth(o3d, scene, pose, intrinsics, width, height) for pose in truth])
    perturbation = np.eye(4)
    perturbation[:3, :3] = o3d.geometry.get_rotation_matrix_from_axis_angle(
        np.radians(2.5) * np.array((1, 2, 0.5)) / np.linalg.norm((1, 2, 0.5)))
    perturbation[:3, 3] = (0.02, -0.015, 0.02)
    poses = truth.copy()
    poses[1] = perturbation @ truth[1]
    angle, offset = pose_error(poses[1], truth[1])
    assert angle == pytest.approx(2.5) and offset > 0.025
    settings = {"intrinsics": intrinsics, "camera_poses": poses, "depth_scale": 1,
                "voxel_size": 0.02, "truncation": 0.06}
    frames = np.stack([depth, depth])
    with reconstruct(frames, refine_poses=True, **settings) as refined:
        metadata = json.loads(json.dumps(dict(refined.metadata)))
        estimate = np.asarray(metadata["refined_camera_poses"])
        np.testing.assert_array_equal(estimate[0], truth[0])
        refined_angle, refined_offset = pose_error(estimate[1], truth[1])
        assert refined_angle < angle / 10 and refined_offset < offset / 3
        (fit,) = metadata["pose_refinement"]
        assert fit["camera"] == 1 and fit["fitness"] > 0.5 and 0 < fit["inlier_rmse"] < 0.02
        with reconstruct(frames, **settings) as unrefined:
            for index in range(2):
                error = [np.abs(scene.compute_signed_distance(o3d.core.Tensor(
                    sequence[index].geometry.positions)).numpy()).mean()
                    for sequence in (refined, unrefined)]
                assert error[0] < 0.6 * error[1]
    with reconstruct(np.ones((1, 32, 32)), intrinsics=(20, 20, 16, 16), refine_poses=True) as single:
        assert "pose_refinement" not in single.metadata


@pytest.mark.open3d
def test_reconstruct_cuda_matches_legacy_volume():
    o3d = pytest.importorskip("open3d")
    if not o3d.core.cuda.is_available():
        pytest.skip("needs an Open3D build with CUDA")
    depth = np.full((1, 2, 48, 48), 1000)
    color = np.zeros((*depth.shape, 3), dtype=np.uint8)
    color[:, 0, ..., 0] = color[:, 1, ..., 2] = 255
    poses = np.stack([np.eye(4), np.eye(4)])
    poses[1, 0, 3] = 0.5
    settings = {"intrinsics": (60, 60, 24, 24), "camera_poses": poses,
                "voxel_size": 0.025, "truncation": 0.1}
    reference = reconstruct(depth, color, **settings)[0].geometry
    for device in ("cuda", "CUDA:0"):
        mesh = reconstruct(depth, color, device=device, **settings)[0].geometry
        assert mesh.positions[:, 2] == pytest.approx(1, abs=0.015)
        assert plane_mesh_distance(mesh, reference) < 0.025
        assert plane_mesh_distance(reference, mesh) < 0.025
        assert 0 <= mesh.colors.min() and mesh.colors.max() <= 1
        assert mesh.colors[mesh.positions[:, 0] < -0.3, 0].mean() > 0.95
        assert mesh.colors[mesh.positions[:, 0] > 0.8, 2].mean() > 0.95
