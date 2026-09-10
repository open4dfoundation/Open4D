from concurrent.futures import ThreadPoolExecutor
import importlib
import json
import socket

import numpy as np
import pytest

from open4d.core import Frame, TriangleMesh
from open4d.demo import mesh_sequence
from open4d.streaming import receive, reconstruct, send
from open4d.streaming import _transport


def test_stream_preserves_frames_and_attributes():
    mesh = TriangleMesh(
        [[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]], [[0, 1, 2]],
        colors=np.full((3, 3), 0.5), normals=np.tile([0., 0., 1.], (3, 1)),
        texture_coordinates=np.array([[0., 0.], [1., 0.], [0., 1.]]),
        attributes={"confidence": np.array([0.1, 0.5, 0.9])},
    )
    expected = [Frame(8, 1.25, mesh, {"camera": "left", "tags": [1, "rgbd"]}),
                Frame(9, 1.5, mesh)]
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, expected, *receiver.address, realtime=False)
        actual = list(receiver)
        assert future.result(timeout=3) == 2
    assert [frame.frame_index for frame in actual] == [8, 9]
    assert [frame.timestamp for frame in actual] == [1.25, 1.5]
    assert actual[0].metadata == expected[0].metadata
    for name in ("positions", "triangles", "colors", "normals", "texture_coordinates"):
        np.testing.assert_array_equal(getattr(actual[0].geometry, name), getattr(mesh, name))
    np.testing.assert_array_equal(actual[0].geometry.attributes["confidence"],
                                  mesh.attributes["confidence"])


def test_stream_synthetic_sequence_and_empty_mesh():
    sample = mesh_sequence(side=4, frames=3)
    empty = Frame(3, 1, TriangleMesh(np.empty((0, 3)), np.empty((0, 3), dtype=np.uint32)))
    expected = [*sample, empty]
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, expected, *receiver.address, realtime=False)
        actual = list(receiver)
        assert future.result(timeout=3) == 4
    for original, decoded in zip(expected, actual):
        np.testing.assert_array_equal(original.geometry.positions, decoded.geometry.positions)
        np.testing.assert_array_equal(original.geometry.triangles, decoded.geometry.triangles)


def test_empty_stream_finishes():
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, [], *receiver.address)
        assert list(receiver) == []
        assert future.result(timeout=3) == 0


def test_receiver_closes_early_and_releases_port():
    with receive(port=0, timeout=3) as receiver:
        address = receiver.address
    with receive(*address, timeout=0.01) as second:
        with pytest.raises(TimeoutError):
            next(second)
    assert list(receiver) == []


@pytest.mark.parametrize("payload,error", [
    (_transport._HEADER.pack(b"BAD!", 0, 0), ValueError),
    (_transport._HEADER.pack(_transport._MAGIC, 10, 1000), ValueError),
    (_transport._HEADER.pack(_transport._MAGIC, 10, 10) + b"{}", EOFError),
])
def test_receiver_rejects_invalid_or_truncated_messages(payload, error):
    with receive(port=0, timeout=3, max_frame_bytes=100) as receiver:
        with socket.create_connection(receiver.address, timeout=3) as sender:
            sender.sendall(payload)
        with pytest.raises(error):
            next(receiver)


def test_receiver_rejects_object_arrays():
    header = json.dumps({"frame_index": 0, "timestamp": 0, "metadata": {},
                         "arrays": [{"name": "positions", "dtype": "O", "shape": [1, 3]}]}).encode()
    with receive(port=0, timeout=3) as receiver:
        with socket.create_connection(receiver.address, timeout=3) as sender:
            sender.sendall(_transport._HEADER.pack(_transport._MAGIC, len(header), 0) + header)
        with pytest.raises(ValueError, match="invalid array"):
            next(receiver)


def test_sender_rejects_large_frames_without_hanging_receiver():
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, mesh_sequence(side=4, frames=1), *receiver.address,
                             max_frame_bytes=1)
        with pytest.raises(ValueError, match="max_frame_bytes"):
            future.result(timeout=3)
        with pytest.raises(EOFError):
            next(receiver)


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
