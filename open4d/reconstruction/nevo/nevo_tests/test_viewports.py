"""Viewport sampling and 6DoF trace decoding."""
from __future__ import annotations

import math
import textwrap

import numpy as np
import pytest

from nevo import viewports
from nevo.cameras import Camera

BOUNDS_MIN = (-0.3, -0.6, -0.3)
BOUNDS_MAX = (0.3, 0.6, 0.3)

TRACE = textwrap.dedent(
    """\
    t_s,scene,streaming,broadcast_id,x,y,z,yaw,pitch,roll,pred_valid,pred_made_at_s,pred_target_s,pred_x,pred_y,pred_z,pred_yaw,pred_pitch,pred_roll
    0.000,stage,0,b0,0.0,1.6,-2.0,0,0,0,0,,,,,,,,
    0.033,stage,1,b0,0.01,1.6,-2.0,5,1,0,1,0.000,0.033,0.02,1.61,-2.01,6,1,0
    0.066,stage,1,b0,0.03,1.6,-1.98,9,2,0,0,,,,,,,,
    """
)


def test_sampled_viewports_stay_in_their_shell_and_face_the_content():
    cameras = viewports.sample_viewports(
        64, BOUNDS_MIN, BOUNDS_MAX, reference_radius=2.0, width=64, height=48, focal=55.0
    )
    centre = (np.asarray(BOUNDS_MIN) + np.asarray(BOUNDS_MAX)) * 0.5
    spread = viewports.ViewportSpread()
    for camera in cameras:
        eye = camera.c2w[:3, 3]
        radius = np.linalg.norm(eye - centre)
        assert 2.0 * spread.radius_scale[0] - 1e-9 <= radius <= 2.0 * spread.radius_scale[1] + 1e-9
        forward = camera.c2w[:3, 2]
        assert np.dot(forward, centre - eye) > 0
        assert np.allclose(camera.c2w[:3, :3] @ camera.c2w[:3, :3].T, np.eye(3), atol=1e-9)


def test_sampling_is_reproducible_and_seed_dependent():
    common = dict(
        xyz_min=BOUNDS_MIN, xyz_max=BOUNDS_MAX, reference_radius=2.0,
        width=64, height=48, focal=55.0,
    )
    first = viewports.sample_viewports(8, seed=3, **common)
    again = viewports.sample_viewports(8, seed=3, **common)
    other = viewports.sample_viewports(8, seed=4, **common)
    assert np.allclose([c.c2w for c in first], [c.c2w for c in again])
    assert not np.allclose([c.c2w for c in first], [c.c2w for c in other])


def test_elevation_spread_actually_leaves_the_horizontal_ring():
    cameras = viewports.sample_viewports(
        200, BOUNDS_MIN, BOUNDS_MAX, reference_radius=2.0, width=64, height=48, focal=55.0
    )
    centre = (np.asarray(BOUNDS_MIN) + np.asarray(BOUNDS_MAX)) * 0.5
    heights = np.asarray([camera.c2w[1, 3] - centre[1] for camera in cameras])
    assert heights.min() < -0.2
    assert heights.max() > 0.8


def _ray_direction(camera: Camera, u: float, v: float) -> np.ndarray:
    """Direction of the ray through a normalised image coordinate, OpenCV."""
    x = (u * camera.width - 0.5 - camera.cx) / camera.fx
    y = (v * camera.height - 0.5 - camera.cy) / camera.fy
    direction = camera.c2w[:3, :3] @ np.asarray((x, y, 1.0))
    return direction / np.linalg.norm(direction)


def test_downscale_preserves_the_view_frustum():
    """Halving resolution must not quietly change what the camera sees."""
    original = viewports.sample_viewports(
        4, BOUNDS_MIN, BOUNDS_MAX, reference_radius=2.0, width=64, height=48, focal=55.0
    )
    smaller = viewports.downscale(original, 2)
    for full, half in zip(original, smaller):
        assert (half.width, half.height) == (32, 24)
        for u, v in ((0.0, 0.0), (0.5, 0.5), (1.0, 1.0), (0.25, 0.75)):
            assert np.allclose(_ray_direction(full, u, v), _ray_direction(half, u, v), atol=1e-9)


def test_downscale_by_one_is_identity():
    original = viewports.sample_viewports(
        2, BOUNDS_MIN, BOUNDS_MAX, reference_radius=2.0, width=64, height=48, focal=55.0
    )
    assert [c.c2w.tolist() for c in viewports.downscale(original, 1)] == [
        c.c2w.tolist() for c in original
    ]


def test_read_quest_trace_keeps_streaming_rows_and_marks_missing_predictions(tmp_path):
    path = tmp_path / "viewport.csv"
    path.write_text(TRACE)
    trace = viewports.read_quest_trace(path)
    assert len(trace) == 2
    assert trace.times.tolist() == [0.033, 0.066]
    assert np.isfinite(trace.predicted[0]).all()
    assert np.isnan(trace.predicted[1]).all()
    assert trace.prediction_error[0] == pytest.approx(
        math.sqrt(0.01 ** 2 + 0.01 ** 2 + 0.01 ** 2), rel=1e-6
    )


def test_read_quest_trace_can_keep_the_non_streaming_rows(tmp_path):
    path = tmp_path / "viewport.csv"
    path.write_text(TRACE)
    assert len(viewports.read_quest_trace(path, streaming_only=False)) == 3


POSES = ((0, 1.6, -2, 0, 0, 0), (1, 1.5, 0.5, 37, -12, 5), (-1, 1, 2, 180, 0, 0))


@pytest.mark.parametrize("convention", viewports.CONVENTIONS)
def test_pose_to_c2w_is_rigid_and_right_handed(convention):
    """A determinant of -1 is the failure mode that matters here: it still
    looks like a rotation, but renders a mirrored view."""
    for pose in POSES:
        c2w = viewports.pose_to_c2w(pose, convention)
        rotation = c2w[:3, :3]
        assert np.allclose(rotation @ rotation.T, np.eye(3), atol=1e-9)
        assert np.isclose(np.linalg.det(rotation), 1.0, atol=1e-9)


@pytest.mark.parametrize("convention", viewports.CONVENTIONS)
def test_identity_pose_faces_negative_z_with_y_down(convention):
    """Both source frames put an unrotated camera looking down world -Z once
    they are in OpenCV axes -- Unity because the world Z flips, glTF because
    that is where its camera already points."""
    c2w = viewports.pose_to_c2w((0.0, 0.0, 0.0, 0.0, 0.0, 0.0), convention)
    assert np.allclose(c2w[:3, 2], (0.0, 0.0, -1.0), atol=1e-12)
    assert np.allclose(c2w[:3, 1], (0.0, -1.0, 0.0), atol=1e-12)


def test_a_left_handed_yaw_is_a_right_handed_yaw_of_the_other_sign():
    """The point of the world flip. If this ever stops holding, one of the two
    branches has picked up a mirror."""
    unity = viewports.pose_to_c2w((0.0, 0.0, 0.0, 90.0, 0.0, 0.0), "unity")
    right_handed = viewports.pose_to_c2w((0.0, 0.0, 0.0, -90.0, 0.0, 0.0), "right_handed")
    assert np.allclose(unity[:3, :3], right_handed[:3, :3], atol=1e-12)


def test_unity_position_z_is_flipped():
    c2w = viewports.pose_to_c2w((1.0, 2.0, 3.0, 0.0, 0.0, 0.0), "unity")
    assert np.allclose(c2w[:3, 3], (1.0, 2.0, -3.0))
    plain = viewports.pose_to_c2w((1.0, 2.0, 3.0, 0.0, 0.0, 0.0), "right_handed")
    assert np.allclose(plain[:3, 3], (1.0, 2.0, 3.0))


def test_unknown_convention_is_refused():
    with pytest.raises(ValueError):
        viewports.pose_to_c2w(POSES[0], "opengl")


def test_trace_viewports_applies_the_corpus_normalisation(tmp_path):
    path = tmp_path / "viewport.csv"
    path.write_text(TRACE)
    trace = viewports.read_quest_trace(path)
    centre = (0.5, 1.5, 0.0)
    scale = 4.0
    cameras = viewports.trace_viewports(
        trace, width=32, height=24, focal=30.0, centre=centre, scale=scale
    )
    assert len(cameras) == 2
    raw = viewports.pose_to_c2w(trace.actual[0], "unity")[:3, 3]
    assert np.allclose(cameras[0].c2w[:3, 3], (raw - np.asarray(centre)) * scale)


def test_trace_viewports_skips_rows_without_a_prediction(tmp_path):
    path = tmp_path / "viewport.csv"
    path.write_text(TRACE)
    trace = viewports.read_quest_trace(path)
    cameras = viewports.trace_viewports(
        trace, width=32, height=24, focal=30.0, centre=(0, 0, 0), scale=1.0, use_predicted=True
    )
    assert [camera.camera_id for camera in cameras] == [0]


def test_empty_trace_is_an_error(tmp_path):
    path = tmp_path / "viewport.csv"
    path.write_text(TRACE.splitlines()[0] + "\n")
    with pytest.raises(ValueError):
        viewports.read_quest_trace(path)
