"""Camera rigs and world normalisation for the NeVo ReRF corpus.

Two coordinate frames appear throughout this baseline and mixing them up is
the single easiest way to train a NeRF that renders noise:

*world*       ORBIT's own metric frame, metres, as the OBJ sequences store it.
              The subjects stand tens of metres from the origin (a basketball
              player sits at x~2.9, z~14.9).
*normalised*  What ReRF trains in. The rig centre becomes the origin and the
              rig radius becomes exactly ``NORMALISED_RADIUS``.

The normalisation matters because ReRF inherits DVGO's scale-sensitive
defaults -- ``inward_nearfar_heuristic`` derives near/far purely from how far
apart the camera positions are, ``alpha_init``/``fast_color_thres`` are tuned
for a unit-ish scene, and upstream's own ``data_util.py`` does the same thing
(mean-centre the cameras, then scale so the furthest sits at 2.0). Rendering
is invariant under a similarity transform applied to cameras *and* geometry
together, so we rasterise in the world frame with real metric cameras and
write the *normalised* extrinsics next to the images. Nothing ever has to
transform a mesh.
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Iterable, List, Sequence, Tuple

import numpy as np

NORMALISED_RADIUS = 2.0
"""Rig radius in the normalised frame. Matches upstream ReRF's data_util.py,
which scales camera positions so the furthest lands at 2.0."""


@dataclass(frozen=True)
class Camera:
    """A pinhole camera. ``c2w`` is OpenCV camera-to-world (x right, y down,
    z forward), which is what ReRF's NHR loader expects when the config sets
    ``inverse_y=True``."""

    camera_id: int
    width: int
    height: int
    fx: float
    fy: float
    cx: float
    cy: float
    c2w: np.ndarray

    @property
    def intrinsic_matrix(self) -> np.ndarray:
        return np.asarray(
            ((self.fx, 0.0, self.cx), (0.0, self.fy, self.cy), (0.0, 0.0, 1.0)),
            dtype=np.float64,
        )

    def scaled_translation(self, centre: np.ndarray, scale: float) -> np.ndarray:
        """This camera's ``c2w`` re-expressed in the normalised frame."""
        out = np.array(self.c2w, dtype=np.float64, copy=True)
        out[:3, 3] = (out[:3, 3] - centre) * scale
        return out


def look_at_c2w(eye: np.ndarray, target: np.ndarray, world_up: np.ndarray) -> np.ndarray:
    """OpenCV camera-to-world for a camera at ``eye`` aimed at ``target``."""
    forward = target - eye
    norm = np.linalg.norm(forward)
    if norm < 1e-9:
        raise ValueError("camera and target coincide")
    forward = forward / norm
    right = np.cross(forward, world_up)
    right_norm = np.linalg.norm(right)
    if right_norm < 1e-6:
        # Looking straight along the up axis: any right vector in the plane
        # orthogonal to forward will do, so pick one deterministically rather
        # than emitting a degenerate rotation.
        fallback = np.asarray((1.0, 0.0, 0.0)) if abs(world_up[0]) < 0.9 else np.asarray((0.0, 0.0, 1.0))
        right = np.cross(forward, fallback)
        right_norm = np.linalg.norm(right)
    right = right / right_norm
    down = np.cross(forward, right)
    c2w = np.eye(4, dtype=np.float64)
    c2w[:3, 0] = right
    c2w[:3, 1] = down
    c2w[:3, 2] = forward
    c2w[:3, 3] = eye
    return c2w


def fit_radius(
    extent: np.ndarray,
    width: int,
    height: int,
    hfov_degrees: float,
    margin: float = 1.12,
) -> float:
    """Smallest orbit radius that keeps the whole bbox inside every frame.

    ``margin`` leaves headroom so a subject that swings an arm mid-sequence
    does not clip the border -- the bbox is the sequence-wide union, but the
    projection of a corner is only bounded by this expression when the camera
    looks at the centre.
    """
    hfov = math.radians(hfov_degrees)
    vfov = 2.0 * math.atan(math.tan(hfov / 2.0) * height / width)
    # Worst case the bbox presents its diagonal to the camera, and the camera
    # is only `radius - depth/2` away from the near face.
    lateral = 0.5 * math.hypot(extent[0], extent[2])
    required = max(
        lateral / math.tan(hfov / 2.0),
        0.5 * extent[1] / math.tan(vfov / 2.0),
    )
    return float((required + lateral) * margin)


def orbit_rig(
    bounds_min: Iterable[float],
    bounds_max: Iterable[float],
    width: int,
    height: int,
    *,
    azimuths: int = 12,
    elevations: Sequence[float] = (-15.0, 5.0, 25.0, 45.0),
    hfov_degrees: float = 60.0,
    up_axis: int = 1,
    margin: float = 1.12,
) -> Tuple[List[Camera], np.ndarray, float]:
    """Build a multi-elevation orbit rig around a bounding box.

    Returns ``(cameras, centre, scale)`` where ``centre``/``scale`` define the
    world -> normalised map ``p' = (p - centre) * scale``.

    ORBIT's own 8-camera corpus (``ORBIT_datasets_gaussian``) puts every view
    on a single horizontal ring, which leaves a NeRF free to invent geometry
    above and below the subject -- concavities under a chin or between arm and
    torso are unconstrained. Since we rasterise the source meshes ourselves
    there is no reason to inherit that limitation, so the default rig spans
    four elevations. The azimuths are offset per elevation so the views do not
    stack into vertical columns.
    """
    lower = np.asarray(bounds_min, dtype=np.float64)
    upper = np.asarray(bounds_max, dtype=np.float64)
    if lower.shape != (3,) or upper.shape != (3,):
        raise ValueError("bounds must be 3-vectors")
    if np.any(upper < lower):
        raise ValueError("bounds_max must not be below bounds_min")
    if azimuths < 3 or not elevations:
        raise ValueError("need at least 3 azimuths and 1 elevation")

    centre = (lower + upper) * 0.5
    extent = upper - lower
    radius = fit_radius(extent, width, height, hfov_degrees, margin)
    fx = width / (2.0 * math.tan(math.radians(hfov_degrees) / 2.0))

    up = np.zeros(3)
    up[up_axis] = 1.0
    plane = [axis for axis in range(3) if axis != up_axis]

    cameras: List[Camera] = []
    for row, elevation_degrees in enumerate(elevations):
        elevation = math.radians(elevation_degrees)
        # Half-step offset on odd rows: with the rows aligned, a vertical
        # column of cameras sees almost the same silhouette three times and
        # the azimuths in between stay unconstrained.
        offset = (row % 2) * math.pi / azimuths
        for index in range(azimuths):
            angle = 2.0 * math.pi * index / azimuths + offset
            direction = np.zeros(3)
            direction[plane[0]] = math.cos(angle) * math.cos(elevation)
            direction[plane[1]] = math.sin(angle) * math.cos(elevation)
            direction[up_axis] = math.sin(elevation)
            eye = centre + direction * radius
            cameras.append(
                Camera(
                    camera_id=len(cameras),
                    width=width,
                    height=height,
                    fx=fx,
                    fy=fx,
                    cx=(width - 1) * 0.5,
                    cy=(height - 1) * 0.5,
                    c2w=look_at_c2w(eye, centre, up),
                )
            )

    scale = NORMALISED_RADIUS / radius
    return cameras, centre, scale
