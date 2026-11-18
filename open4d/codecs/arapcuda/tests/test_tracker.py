import numpy as np
import pytest

import arapcuda

pytestmark = pytest.mark.skipif(arapcuda.device_count() == 0, reason="needs a CUDA device")

HALF = np.array([0.5, 0.3, 0.2], np.float32)   # half extents of the test box


def box():
    """Closed box mesh centered at the origin."""
    v = np.array([[x, y, z] for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)], np.float32) * HALF
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    f = [[a, b, c] for a, b, c, d in quads] + [[a, c, d] for a, b, c, d in quads]
    return v, np.array(f, np.int32)


def rot_z(theta):
    c, s = np.cos(theta), np.sin(theta)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]], np.float32)


def moving_box(frames, step=0.05):
    v, f = box()
    for k in range(frames):
        R, t = rot_z(step * k), np.array([0.03 * k, 0.01 * k, 0.0], np.float32)
        yield (v @ R.T + t).astype(np.float32), f, R, t


def test_rigid_motion_is_followed():
    tracker = arapcuda.Tracker(centers=150, grid_resolution=64)
    voxel = 2 * HALF.max() / 64
    first = None
    for k, (v, f, R, t) in enumerate(moving_box(5)):
        frame = tracker.add_frame(v, f)
        c = frame["centers"]
        assert c.shape == (150, 3) and c.dtype == np.float32
        local = (c - t) @ R            # back in the box's own frame
        assert np.all(np.abs(local) <= HALF + voxel)
        if k == 0:
            assert frame["transforms"] is None
            first = local
            continue
        # the centers ride along with the box ...
        assert np.median(np.linalg.norm(local - first, axis=1)) < 2 * voxel
        # ... and each center's transform is the frame-to-frame rotation
        T = frame["transforms"]
        assert T.shape == (150, 3, 4)
        assert np.median(np.linalg.norm(T[:, :, :3] - rot_z(0.05), axis=(1, 2))) < 0.03
        assert np.allclose(np.linalg.det(T[:, :, :3]), 1, atol=1e-4)
    assert tracker.frame_count == 5


def test_deterministic():
    runs = [arapcuda.track(((v, f) for v, f, _, _ in moving_box(3)), centers=100, grid_resolution=48) for _ in range(2)]
    assert np.array_equal(runs[0][0], runs[1][0])


def test_track_shapes():
    centers, transforms = arapcuda.track(((v, f) for v, f, _, _ in moving_box(3)), centers=80, grid_resolution=48)
    assert centers.shape == (3, 80, 3)
    assert transforms.shape == (3, 80, 3, 4)
    assert np.array_equal(transforms[0, :, :, :3], np.broadcast_to(np.eye(3), (80, 3, 3)))


def test_bad_input():
    v, f = box()
    tracker = arapcuda.Tracker(centers=10, grid_resolution=16)
    with pytest.raises(ValueError):
        tracker.add_frame(v[:, :2], f)
    with pytest.raises(ValueError):
        tracker.add_frame(v, f + 100)
    with pytest.raises(ValueError):
        arapcuda.Tracker(centers=10_000, grid_resolution=8).add_frame(v, f)
    with pytest.raises(TypeError):
        arapcuda.Tracker(center=10)


def test_obj_roundtrip(tmp_path):
    v, f = box()
    path = tmp_path / "box.obj"
    path.write_text("".join(f"v {x} {y} {z}\n" for x, y, z in v) + "".join(f"f {a + 1} {b + 1} {c + 1}\n" for a, b, c in f))
    v2, f2 = arapcuda.read_obj(path)
    assert np.allclose(v, v2) and np.array_equal(f, f2)
