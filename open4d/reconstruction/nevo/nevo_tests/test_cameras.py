"""Rig geometry: the part that silently ruins a NeRF if it is wrong.

A camera convention error does not crash -- it trains a plausible-looking
model of the wrong scene -- so these check the invariants directly: rotations
stay orthonormal, the rig frames the whole bounding box, and normalisation
lands every camera on the radius ReRF's near/far heuristic assumes.
"""
from __future__ import annotations

import math

import numpy as np
import pytest

from nevo.cameras import (
    NORMALISED_RADIUS,
    Camera,
    fit_radius,
    look_at_c2w,
    orbit_rig,
)

BOUNDS_MIN = (-327.3, -480.0, -447.9)
BOUNDS_MAX = (331.5, 1395.0, 197.4)
"""A real ORBIT frame: basketball_player_fr0001, in native millimetres."""


def test_look_at_is_a_rigid_transform():
    c2w = look_at_c2w(
        np.asarray((3.0, 1.0, 0.0)), np.asarray((0.0, 1.0, 0.0)), np.asarray((0.0, 1.0, 0.0))
    )
    rotation = c2w[:3, :3]
    assert np.allclose(rotation @ rotation.T, np.eye(3), atol=1e-12)
    assert np.isclose(np.linalg.det(rotation), 1.0, atol=1e-12)


def test_look_at_uses_opencv_axes():
    """Column 2 points at the target, column 1 points down, column 0 right."""
    eye = np.asarray((0.0, 0.0, -5.0))
    target = np.zeros(3)
    up = np.asarray((0.0, 1.0, 0.0))
    c2w = look_at_c2w(eye, target, up)
    assert np.allclose(c2w[:3, 2], (0.0, 0.0, 1.0), atol=1e-12)
    assert np.allclose(c2w[:3, 1], (0.0, -1.0, 0.0), atol=1e-12)
    assert np.dot(np.cross(c2w[:3, 0], c2w[:3, 1]), c2w[:3, 2]) > 0


def test_look_at_survives_a_straight_down_camera():
    """The naive right = forward x up is degenerate when they are parallel."""
    c2w = look_at_c2w(
        np.asarray((0.0, 5.0, 0.0)), np.zeros(3), np.asarray((0.0, 1.0, 0.0))
    )
    assert np.all(np.isfinite(c2w))
    assert np.allclose(c2w[:3, :3] @ c2w[:3, :3].T, np.eye(3), atol=1e-9)


def _project(camera: Camera, point: np.ndarray) -> np.ndarray:
    world_to_camera = np.linalg.inv(camera.c2w)
    local = world_to_camera[:3, :3] @ point + world_to_camera[:3, 3]
    assert local[2] > 0, "point fell behind the camera"
    return camera.intrinsic_matrix @ (local / local[2])


def test_rig_frames_the_whole_bounding_box():
    cameras, _, _ = orbit_rig(BOUNDS_MIN, BOUNDS_MAX, 1280, 960)
    lower, upper = np.asarray(BOUNDS_MIN), np.asarray(BOUNDS_MAX)
    corners = np.asarray(
        [
            (lower[0] if x else upper[0], lower[1] if y else upper[1], lower[2] if z else upper[2])
            for x in (0, 1)
            for y in (0, 1)
            for z in (0, 1)
        ]
    )
    for camera in cameras:
        for corner in corners:
            pixel = _project(camera, corner)
            assert -0.5 <= pixel[0] <= camera.width - 0.5, (camera.camera_id, pixel)
            assert -0.5 <= pixel[1] <= camera.height - 0.5, (camera.camera_id, pixel)


def test_rig_actually_fills_the_frame():
    """Framing has to be tight enough to be worth 48 renders.

    A rig that fits the bbox by sitting a kilometre away also "frames" it.
    """
    cameras, _, _ = orbit_rig(BOUNDS_MIN, BOUNDS_MAX, 1280, 960)
    lower, upper = np.asarray(BOUNDS_MIN), np.asarray(BOUNDS_MAX)
    centre = (lower + upper) * 0.5
    top = centre.copy()
    top[1] = upper[1]
    bottom = centre.copy()
    bottom[1] = lower[1]
    for camera in cameras:
        height = abs(_project(camera, top)[1] - _project(camera, bottom)[1])
        assert height > 0.5 * camera.height, (camera.camera_id, height)


def test_normalisation_puts_every_camera_on_the_reference_radius():
    cameras, centre, scale = orbit_rig(BOUNDS_MIN, BOUNDS_MAX, 1280, 960)
    radii = [
        np.linalg.norm(camera.scaled_translation(centre, scale)[:3, 3]) for camera in cameras
    ]
    assert np.allclose(radii, NORMALISED_RADIUS, atol=1e-9)


def test_normalisation_is_a_similarity_so_projection_is_unchanged():
    cameras, centre, scale = orbit_rig(BOUNDS_MIN, BOUNDS_MAX, 1280, 960)
    probe = np.asarray((100.0, 300.0, -50.0))
    for camera in cameras[:6]:
        world_pixel = _project(camera, probe)
        normalised = Camera(
            camera.camera_id,
            camera.width,
            camera.height,
            camera.fx,
            camera.fy,
            camera.cx,
            camera.cy,
            camera.scaled_translation(centre, scale),
        )
        normalised_pixel = _project(normalised, (probe - centre) * scale)
        assert np.allclose(world_pixel, normalised_pixel, atol=1e-6)


def test_elevation_rows_are_offset_in_azimuth():
    """Stacked rows would see nearly the same silhouette three times over."""
    cameras, centre, _ = orbit_rig(
        BOUNDS_MIN, BOUNDS_MAX, 1280, 960, azimuths=8, elevations=(0.0, 30.0)
    )
    def azimuth(camera):
        offset = camera.c2w[:3, 3] - centre
        return math.atan2(offset[2], offset[0]) % (2 * math.pi)

    assert not np.isclose(azimuth(cameras[0]), azimuth(cameras[8]), atol=1e-6)


def test_fit_radius_grows_with_the_subject():
    small = fit_radius(np.asarray((0.5, 1.0, 0.5)), 1280, 960, 60.0)
    large = fit_radius(np.asarray((1.0, 2.0, 1.0)), 1280, 960, 60.0)
    assert large > small
    assert np.isclose(large / small, 2.0, rtol=1e-9)


@pytest.mark.parametrize("azimuths,elevations", [(2, (0.0,)), (8, ())])
def test_rig_rejects_degenerate_requests(azimuths, elevations):
    with pytest.raises(ValueError):
        orbit_rig(BOUNDS_MIN, BOUNDS_MAX, 1280, 960, azimuths=azimuths, elevations=elevations)
