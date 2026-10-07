"""A repeatable RGB-D example: the same capture always exports the same meshes.

The capture is a sphere moving in front of a checkered wall, seen by two
cameras and rendered analytically with NumPy, so its depth and colour bytes do
not depend on Open3D. Run this file as a script to print the digest of its
reconstruction.
"""

import hashlib
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

import open4d

INTRINSICS = (80.0, 80.0, 47.5, 39.5)
WIDTH, HEIGHT = 96, 80
RADIUS, WALL = 0.3, 2.0
SETTINGS = {"voxel_size": 0.02, "truncation": 0.06, "fps": 10}


def look_at(eye, target):
    eye = np.asarray(eye, dtype=np.float64)
    forward = np.asarray(target, dtype=np.float64) - eye
    forward /= np.linalg.norm(forward)
    right = np.cross((0, 1, 0), forward)
    right /= np.linalg.norm(right)
    pose = np.eye(4)
    pose[:3, :3] = np.column_stack((right, np.cross(forward, right), forward))
    pose[:3, 3] = eye
    return pose


CAMERA_POSES = np.stack([look_at((0, 0, 0), (0, 0, 1.4)), look_at((0.5, -0.1, 0.1), (0, 0, 1.4))])


def sphere_center(frame):
    return np.array((-0.15 + 0.1 * frame, 0.02 * frame, 1.4))


def render(pose, center):
    """Millimetre depth and RGB of the sphere and the wall z = WALL."""
    fx, fy, cx, cy = INTRINSICS
    rows, columns = np.mgrid[0:HEIGHT, 0:WIDTH].astype(np.float64)
    # Camera rays with unit z, so the hit distance along a ray is its depth.
    rays = np.stack(((columns - cx) / fx, (rows - cy) / fy, np.ones_like(rows)), -1)
    rays = rays @ pose[:3, :3].T
    origin = pose[:3, 3]
    with np.errstate(divide="ignore"):
        wall = np.where(rays[..., 2] > 0, (WALL - origin[2]) / rays[..., 2], np.inf)
    offset = origin - center
    a = (rays ** 2).sum(-1)
    b = 2 * rays @ offset
    discriminant = b * b - 4 * a * (offset @ offset - RADIUS ** 2)
    sphere = np.where(discriminant >= 0,
                      (-b - np.sqrt(np.maximum(discriminant, 0))) / (2 * a), np.inf)
    sphere = np.where(sphere > 0, sphere, np.inf)
    distance = np.minimum(wall, sphere)
    depth = np.where(np.isfinite(distance), np.rint(distance * 1000), 0).astype(np.uint16)
    hit = origin + rays * np.where(np.isfinite(distance), distance, 0)[..., None]
    checker = (np.floor(hit[..., 0] / 0.1) + np.floor(hit[..., 1] / 0.1)) % 2
    on_sphere = sphere < wall
    color = np.stack((np.where(on_sphere, 220, 40), 60 + 120 * checker,
                      np.where(on_sphere, 30, 200)), -1).astype(np.uint8)
    color[depth == 0] = 0
    return depth, color


def capture(frames=3):
    """(frames, 2, H, W) depth in millimetres and matching RGB."""
    images = [[render(pose, sphere_center(frame)) for pose in CAMERA_POSES] for frame in range(frames)]
    depth = np.array([[d for d, _ in views] for views in images])
    color = np.array([[c for _, c in views] for views in images])
    return depth, color


def reconstruct(**options):
    depth, color = capture()
    return open4d.reconstruct(depth, color, intrinsics=INTRINSICS, camera_poses=CAMERA_POSES,
                              **SETTINGS, **options)


def digest(sequence):
    """SHA-256 over every frame's timestamp, positions, triangles and colours."""
    hasher = hashlib.sha256()
    for frame in sequence:
        mesh = frame.geometry
        hasher.update(np.float64(frame.timestamp).tobytes())
        for array in (mesh.positions, mesh.triangles, mesh.colors):
            array = np.ascontiguousarray(array)
            hasher.update(f"{array.dtype.str}{array.shape}".encode())
            hasher.update(array.tobytes())
    return hasher.hexdigest()


def assert_same(sequence, reference):
    assert sequence.timestamps == reference.timestamps
    assert len(sequence) == len(reference)
    for frame, expected in zip(sequence, reference):
        mesh, other = frame.geometry, expected.geometry
        for name in ("positions", "triangles", "colors"):
            mine, theirs = getattr(mesh, name), getattr(other, name)
            assert mine.dtype == theirs.dtype, name
            assert np.array_equal(mine, theirs), name


def canonical(mesh):
    """Order-free form of a mesh: sorted vertex rows and sorted triangle corners."""
    vertices = np.column_stack((mesh.positions, mesh.colors))
    corners = mesh.positions[mesh.triangles]                      # (T, 3, 3)
    # Rotate each triangle to start at its lexicographically smallest corner,
    # keeping the winding, then sort the triangles.
    def less(a, b):
        return (a[:, 0] < b[:, 0]) | ((a[:, 0] == b[:, 0]) & (
            (a[:, 1] < b[:, 1]) | ((a[:, 1] == b[:, 1]) & (a[:, 2] < b[:, 2]))))

    rows = np.arange(len(corners))
    first = np.zeros(len(corners), dtype=np.intp)
    for k in (1, 2):
        first = np.where(less(corners[:, k], corners[rows, first]), k, first)
    rotated = np.stack([corners[rows, (first + k) % 3] for k in range(3)], 1)
    rotated = rotated.reshape(len(corners), 9)
    return (vertices[np.lexsort(vertices.T[::-1])], rotated[np.lexsort(rotated.T[::-1])])


def main():
    print(digest(reconstruct()))


if __name__ == "__main__":
    main()
else:
    pytest.importorskip("open3d")
    pytestmark = pytest.mark.open3d


def test_synthetic_capture_is_meaningful():
    depth, color = capture()
    assert depth.shape == (3, 2, HEIGHT, WIDTH) and depth.dtype == np.uint16
    assert color.shape == (3, 2, HEIGHT, WIDTH, 3) and color.dtype == np.uint8
    # Both cameras see the sphere in front of the wall, and the sphere moves.
    assert (depth > 0).all()
    for camera in range(2):
        assert depth[0, camera].min() < 1300 and depth[0, camera].max() > 1900
    assert not np.array_equal(depth[0], depth[1])
    with reconstruct() as sequence:
        assert sequence.timestamps == (0, 0.1, 0.2)
        for index, frame in enumerate(sequence):
            mesh = frame.geometry
            assert len(mesh.triangles) > 10_000
            distance = np.linalg.norm(mesh.positions - sphere_center(index), axis=1)
            on_sphere = np.abs(distance - RADIUS) < 0.02
            assert on_sphere.sum() > 1000
            # The red sphere and blue wall keep their colours in the mesh.
            assert mesh.colors[on_sphere, 0].mean() > 0.7
            assert mesh.colors[np.abs(mesh.positions[:, 2] - WALL) < 0.01, 2].mean() > 0.7


def test_default_cpu_reconstruction_is_bit_exact_in_process():
    first, second = reconstruct(), reconstruct()
    assert_same(first, second)
    assert digest(first) == digest(second)
    # A lazy sequence returns the same mesh when a frame is read again.
    assert np.array_equal(first[1].geometry.positions, first[1].geometry.positions)


def test_default_cpu_reconstruction_is_bit_exact_in_a_fresh_process(tmp_path):
    root = Path(open4d.__file__).resolve().parents[1]
    env = {**os.environ, "PYTHONPATH": os.pathsep.join(
        filter(None, (str(root), os.environ.get("PYTHONPATH"))))}
    # A different OpenMP thread count must not change the result either.
    env["OMP_NUM_THREADS"] = "1"
    result = subprocess.run([sys.executable, __file__], cwd=tmp_path, env=env,
                            capture_output=True, text=True, timeout=600)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == digest(reconstruct())


def test_tensor_cpu_reconstruction_is_repeatable_up_to_element_order():
    # Open3D's parallel voxel-block hash map assigns vertex and triangle
    # indices in thread order, so device="cpu" is not bit-exact. The meshes
    # are still the same surface: identical vertex values (bit for bit) and
    # identical triangles once both are put in a canonical order.
    first, second = reconstruct(device="cpu"), reconstruct(device="cpu")
    assert first.timestamps == second.timestamps
    for frame, other in zip(first, second):
        mesh, expected = frame.geometry, other.geometry
        assert mesh.positions.shape == expected.positions.shape
        assert mesh.triangles.shape == expected.triangles.shape
        for mine, theirs in zip(canonical(mesh), canonical(expected)):
            assert np.array_equal(mine, theirs)


def test_reconstruction_exports_and_reloads_identically(tmp_path):
    sequence = reconstruct()
    frames = open4d.io.write_sequence(sequence, tmp_path / "frames", format="ply")
    assert sorted(path.name for path in frames.glob("*.ply")) == [
        "frame_000000.ply", "frame_000001.ply", "frame_000002.ply"]
    with open4d.load(frames) as reloaded:
        assert_same(reloaded, sequence)
        assert reloaded.metadata["source"] == "rgbd"
        assert reloaded.metadata["fps"] == 10
        assert digest(reloaded) == digest(sequence)
    # Exporting again from a fresh reconstruction writes byte-identical files.
    again = open4d.io.write_sequence(reconstruct(), tmp_path / "again", format="ply")
    for path in sorted(frames.iterdir()):
        assert path.read_bytes() == (again / path.name).read_bytes(), path.name


def test_reconstruction_round_trips_through_usdc(tmp_path):
    pytest.importorskip("pxr")
    sequence = reconstruct()
    path = open4d.save(sequence, tmp_path / "sphere.usdc")
    with open4d.load(path) as reloaded:
        assert_same(reloaded, sequence)
        assert reloaded.metadata["fps"] == 10
        assert digest(reloaded) == digest(sequence)
