"""Synthetic multi-object dynamic scene generator.

There is no real multi-view capture available yet (see README). This module
builds a small full-scene-like synthetic sequence — a static background plus
several foreground objects, some static and some moving/rotating — with
*known* ground-truth per-Gaussian object ids and non-color attributes, so the
rest of the Vega pipeline (segmentation, color encoding, dynamicity
filtering, GOV optimization, rendering pipeline) can be exercised and
validated end to end on real tensors and a real CUDA rasterizer, without
waiting on a dataset.

Swap this out for a real reconstructed sequence (e.g. from `run_queen` /
`run_3dgstream` in this environment) by producing the same
`list[GaussianSet]` + camera representation; nothing downstream depends on
this module.
"""
from __future__ import annotations

import dataclasses
import math

import torch

from vega.cameras import Camera, orbit_cameras
from vega.gaussians import GaussianSet
from vega.sh import rgb_to_sh0


@dataclasses.dataclass
class ObjectSpec:
    object_id: int
    center: torch.Tensor      # (3,) base center
    n_gaussians: int
    spread: float
    color: torch.Tensor       # (3,) base RGB in [0,1]
    is_dynamic: bool
    motion_axis: torch.Tensor  # (3,) translation direction
    motion_amp: float
    motion_freq: float         # cycles over the whole sequence
    spin_axis: torch.Tensor
    spin_amp: float            # radians


def _make_object_specs(n_objects: int, seed: int, device: str) -> list[ObjectSpec]:
    g = torch.Generator(device="cpu").manual_seed(seed)
    specs = []
    # object 0: static full-scene background
    specs.append(ObjectSpec(
        object_id=0,
        center=torch.zeros(3),
        n_gaussians=1500,
        spread=2.2,
        color=torch.tensor([0.55, 0.55, 0.6]),
        is_dynamic=False,
        motion_axis=torch.zeros(3), motion_amp=0.0, motion_freq=0.0,
        spin_axis=torch.tensor([0., 1., 0.]), spin_amp=0.0,
    ))
    n_fg = n_objects - 1
    for k in range(n_fg):
        theta = 2 * math.pi * k / max(n_fg, 1)
        radius = 0.9
        center = torch.tensor([radius * math.cos(theta), (torch.rand(1, generator=g).item() - 0.5) * 0.6,
                               radius * math.sin(theta)])
        is_dynamic = (k % 2 == 0)  # alternate static/dynamic foreground objects
        color = torch.rand(3, generator=g) * 0.8 + 0.1
        motion_axis = torch.nn.functional.normalize(torch.rand(3, generator=g) - 0.5, dim=0)
        spin_axis = torch.nn.functional.normalize(torch.rand(3, generator=g) - 0.5, dim=0)
        specs.append(ObjectSpec(
            object_id=k + 1,
            center=center,
            n_gaussians=300,
            spread=0.22,
            color=color,
            is_dynamic=is_dynamic,
            motion_axis=motion_axis,
            motion_amp=0.35 if is_dynamic else 0.0,
            motion_freq=1.0 + 0.3 * k,
            spin_axis=spin_axis,
            spin_amp=(math.pi * 0.6) if is_dynamic else 0.0,
        ))
    return specs


def _rotation_matrix(axis: torch.Tensor, angle: float) -> torch.Tensor:
    axis = axis / axis.norm().clamp_min(1e-8)
    x, y, z = axis
    c, s = math.cos(angle), math.sin(angle)
    C = 1 - c
    return torch.tensor([
        [x * x * C + c, x * y * C - z * s, x * z * C + y * s],
        [y * x * C + z * s, y * y * C + c, y * z * C - x * s],
        [z * x * C - y * s, z * y * C + x * s, z * z * C + c],
    ])


def _quat_mul(q1: torch.Tensor, q2: torch.Tensor) -> torch.Tensor:
    w1, x1, y1, z1 = q1.unbind(-1)
    w2, x2, y2, z2 = q2.unbind(-1)
    return torch.stack([
        w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
        w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
        w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
        w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
    ], dim=-1)


def _axis_angle_to_quat(axis: torch.Tensor, angle: float) -> torch.Tensor:
    axis = axis / axis.norm().clamp_min(1e-8)
    half = angle / 2
    return torch.cat([torch.tensor([math.cos(half)]), math.sin(half) * axis])


def _instantiate_object(spec: ObjectSpec, t: float, seed_offset: int, device: str,
                         sh_degree: int) -> GaussianSet:
    g = torch.Generator(device="cpu").manual_seed(1000 + seed_offset)
    n = spec.n_gaussians
    local = torch.randn(n, 3, generator=g) * spec.spread

    translation = spec.motion_axis * spec.motion_amp * math.sin(2 * math.pi * spec.motion_freq * t)
    angle = spec.spin_amp * math.sin(2 * math.pi * spec.motion_freq * t + 0.7)
    R = _rotation_matrix(spec.spin_axis, angle)
    xyz = (local @ R.T) + spec.center + translation

    base_quat = _axis_angle_to_quat(spec.spin_axis, angle)
    rot_raw = base_quat.unsqueeze(0).repeat(n, 1) + torch.randn(n, 4, generator=g) * 0.01

    scale = 0.03 + 0.015 * torch.rand(n, 3, generator=g)
    scale_raw = torch.log(scale)

    opacity = 0.7 + 0.25 * torch.rand(n, 1, generator=g)
    opacity_raw = torch.log(opacity / (1 - opacity))

    color = spec.color.unsqueeze(0) + (torch.rand(n, 3, generator=g) - 0.5) * 0.15
    color = color.clamp(0.02, 0.98)
    sh_dc = rgb_to_sh0(color).unsqueeze(1)  # (n, 1, 3)
    n_rest = (sh_degree + 1) ** 2 - 1
    sh_rest = torch.zeros(n, n_rest, 3)

    object_id = torch.full((n,), spec.object_id, dtype=torch.long)

    gs = GaussianSet(
        xyz=xyz.to(device), scale_raw=scale_raw.to(device), rot_raw=rot_raw.to(device),
        opacity_raw=opacity_raw.to(device), sh_dc=sh_dc.to(device), sh_rest=sh_rest.to(device),
        object_id=object_id.to(device), sh_degree=sh_degree,
    )
    return gs


def synthetic_sequence(n_frames: int = 16, n_objects: int = 6, device: str = "cuda",
                        sh_degree: int = 2, seed: int = 0):
    """Build a synthetic full-scene dynamic sequence.

    Returns:
        frames: list[GaussianSet], one per timestep, with ground-truth
            object_id and non-color attributes.
        train_cameras: list[Camera], a small fixed multi-view rig (shared
            across all frames), analogous to a real capture studio.
        object_dynamic_gt: dict[int, bool], ground-truth dynamic flag per
            object (for sanity-checking dynamicity estimation later).
    """
    specs = _make_object_specs(n_objects, seed, device)
    frames = []
    for f in range(n_frames):
        t = f / max(n_frames - 1, 1)
        objs = [_instantiate_object(s, t, seed_offset=s.object_id * 997 + f, device=device,
                                     sh_degree=sh_degree) for s in specs]
        frames.append(GaussianSet.cat(objs))
    train_cameras = orbit_cameras(n_cameras=6, radius=3.0, height=0.4, width=256, height_px=256,
                                   fov_deg=55.0, device=device)
    object_dynamic_gt = {s.object_id: s.is_dynamic for s in specs}
    return frames, train_cameras, object_dynamic_gt


def playback_camera_path(n_steps: int = 90, device: str = "cuda", width: int = 512, height: int = 512):
    """A smooth free-viewpoint camera path for the playback/rendering demo
    (distinct from the fixed multi-view training rig above)."""
    return orbit_cameras(n_cameras=n_steps, radius=3.2, height=0.5, width=width, height_px=height,
                          fov_deg=60.0, device=device)
