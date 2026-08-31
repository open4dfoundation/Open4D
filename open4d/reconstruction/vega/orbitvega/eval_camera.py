"""Evaluation/playback cameras placed at a distance this environment's
rasterizer actually renders at.

Vega's encoder scores every frame by rendering it from one shared evaluation
camera (`vega.encoder.default_eval_camera`) and treating that image as the
captured photo — distortion D(i) for the GOV rate-distortion optimizer
(Eq. 6), the dynamicity estimate (Eq. 1-2) and the reported PSNR all come
from it. So if that camera renders nothing, the encode still "succeeds" but
every number it produces is meaningless.

That is a live hazard on this workstation. The installed
`diff_gaussian_rasterization` build drops **every** Gaussian once the camera
sits closer than roughly 3.5-4 world units, whatever the scene's own size —
measured here by sweeping camera distance against both the ORBIT corpus and
the repo's own `vega.synthetic` scene rescaled to several extents:

    camera distance   1.0   2.0   3.0   3.9   4.0   4.5   5.0   8.0
    extent  1.9 m       0     0     0   24%   47%   99%  100%  100%
    extent  4.0 m       0     0    2%   40%   47%   91%   98%  100%
    extent 17.0 m       3%    6%   15%  39%   40%   59%   72%   93%

The floor is absolute, not relative: it does not move when the scene is
scaled, and it does not move with field of view. (The extent-17 row falls
short of 100% simply because that cloud is large enough to extend past the
frustum, which is ordinary culling.)

`default_eval_camera` derives its distance purely from scene extent
(`radius = extent.max() * 1.6`, eye offset `[r, 0.3r, r]`, so distance
`= 1.446 r`). For the ORBIT objects, which are human-sized (~1.9 m), that
lands at ~4.3 units — inside the partial-visibility band, which is why this
module exists rather than just calling it. `MIN_CAMERA_DISTANCE` below is set
past the measured floor with margin.
"""
from __future__ import annotations

import math

import numpy as np
import torch

from vega.cameras import Camera, look_at_RT

MIN_CAMERA_DISTANCE = 6.0
"""World units. Past the ~4-unit floor measured above, with margin."""


def eval_camera_for_bounds(bbox_min: torch.Tensor, bbox_max: torch.Tensor, device: str,
                           width: int = 256, height: int = 256, fov_deg: float = 55.0,
                           extent_multiple: float = 1.6,
                           min_distance: float = MIN_CAMERA_DISTANCE) -> Camera:
    """`vega.encoder.default_eval_camera`'s camera, pushed out to at least
    `min_distance` from the scene centre along the same diagonal direction.

    Field of view is narrowed to compensate, so the subject subtends roughly
    the same fraction of the frame as it would have at the nominal distance —
    the encoder's distortion term stays comparable to the RGBD path's instead
    of being computed over a subject shrunk to a few pixels.
    """
    centre = ((bbox_min + bbox_max) / 2).detach().cpu().numpy().astype(np.float32)
    extent = (bbox_max - bbox_min).detach().cpu().numpy()
    radius = float(max(extent.max() * extent_multiple, 1e-2))

    direction = np.array([1.0, 0.3, 1.0], dtype=np.float32)
    direction /= np.linalg.norm(direction)
    nominal_distance = radius * float(np.linalg.norm([1.0, 0.3, 1.0]))
    distance = max(nominal_distance, min_distance)

    fov = math.radians(fov_deg)
    if distance > nominal_distance:
        half = math.atan(math.tan(fov / 2) * nominal_distance / distance)
        fov = 2 * half

    eye = (centre + direction * distance).astype(np.float32)
    R, T = look_at_RT(eye, centre)
    return Camera(R=R, T=T, fovx=fov, fovy=fov, width=width, height=height, device=device)


def playback_fov_deg(bbox_min: torch.Tensor, bbox_max: torch.Tensor, radius: float,
                     fill: float = 0.8, max_fov_deg: float = 90.0) -> float:
    """Field of view that makes the subject span `fill` of the half-frame at
    orbit distance `radius`.

    Needed because `MIN_CAMERA_DISTANCE` forces the camera further out than
    framing alone would want. The subject's bounding sphere is taken as half
    the bbox diagonal about the bbox centre, which the orbit cameras target —
    so this holds at every orbit angle and for every frame in the sequence
    (the bbox is sequence-wide, so it already contains all of the motion).
    """
    obj_radius = float((bbox_max - bbox_min).norm().item()) * 0.5
    subtended = math.atan(obj_radius / max(radius, 1e-6))
    return min(max_fov_deg, math.degrees(2 * subtended / max(fill, 1e-3)))


def safe_orbit_radius(bbox_min: torch.Tensor, bbox_max: torch.Tensor,
                      extent_multiple: float = 3.15,
                      min_distance: float = MIN_CAMERA_DISTANCE) -> float:
    """Orbit radius for playback: the demo's usual multiple of scene extent,
    floored at `min_distance` so close-in scenes still render."""
    extent = float((bbox_max - bbox_min).max().item())
    return max(extent * extent_multiple, min_distance)
