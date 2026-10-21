"""Recorded-input and timing contracts; these do not qualify physical cameras."""

import json

import numpy as np
import pytest

from open4d.io._png import write_png
from open4d.reconstruction.rgbd._camera import CameraMode, preset, resolve_mode
from open4d.reconstruction.rgbd._reconstruction import _sync, SyncWarning
from open4d.reconstruction.rgbd._sequence import load_sequence


def test_camera_mode_is_explicit_and_roundtrips_from_capture_metadata(tmp_path):
    mode = preset("femto-bolt-nfov-unbinned-720p")
    assert mode.depth_size == (640, 576) and mode.depth_bytes == 640 * 576 * 2
    (tmp_path / "camera_mode.json").write_text(json.dumps(mode.to_dict()))
    actual, source = resolve_mode(calibration=tmp_path)
    assert actual == mode and source.endswith("camera_mode.json")
    assert CameraMode.from_dict(mode.to_dict()) == mode
    with pytest.raises(ValueError, match="depth_binning"):
        CameraMode(**{**mode.to_dict(), "depth_binning": 3})


def test_sync_policy_distinguishes_measured_failure_from_unknown_timing():
    with pytest.raises(ValueError, match="synchronization limit"):
        _sync([100, -5000], 1000, "error", (0, .1), 10, 2, 2)
    with pytest.warns(SyncWarning):
        record = _sync([100, -5000], 1000, "warn", (0, .1), 10, 2, 2)
    assert record["status"] == "exceeded" and record["exceeding_frames"] == [1]
    unknown = _sync(None, 1000, "error", (0, .1), 10, 2, 2)
    assert unknown["status"] == "unknown" and not unknown["timing_known"]


def test_tum_loader_preserves_depth_units_and_interpolates_recorded_poses(tmp_path):
    pytest.importorskip("PIL.Image")
    (tmp_path / "depth").mkdir()
    (tmp_path / "rgb").mkdir()
    for index in range(2):
        write_png(tmp_path / f"depth/{index}.png", np.full((2, 3), 5000, np.uint16))
        write_png(tmp_path / f"rgb/{index}.png", np.full((2, 3, 3), 128, np.uint8))
    (tmp_path / "depth.txt").write_text("1.05 depth/0.png\n1.15 depth/1.png\n")
    (tmp_path / "rgb.txt").write_text("1.055 rgb/0.png\n1.155 rgb/1.png\n")
    (tmp_path / "groundtruth.txt").write_text("1.0 0 0 0 0 0 0 1\n1.2 2 0 0 0 0 0 1\n")
    capture = load_sequence(tmp_path, intrinsics=(2, 2, 1, .5))
    assert capture.depth_scale == 5000
    assert capture.depth.shape == (2, 1, 2, 3)
    np.testing.assert_allclose(capture.timestamps, [0, .1])
    np.testing.assert_allclose(capture.camera_poses[:, 0, 0, 3], [.5, 1.5])
    assert capture.provenance["pose_source"] == "groundtruth.txt (interpolated)"
