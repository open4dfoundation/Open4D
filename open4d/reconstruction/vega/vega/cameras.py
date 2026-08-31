"""Pinhole camera model + view-frustum math, matching the conventions expected
by `diff_gaussian_rasterization` (world-to-view / OpenGL-style projection).
"""
from __future__ import annotations

import dataclasses
import math

import numpy as np
import torch


def get_world_to_view(R: np.ndarray, T: np.ndarray) -> np.ndarray:
    """4x4 world->camera matrix from rotation R (3x3, world axes in camera
    frame, i.e. columns are camera axes expressed in world coords... using the
    standard 3DGS convention where Rt = R^T, translation T is camera-frame)."""
    Rt = np.zeros((4, 4), dtype=np.float32)
    Rt[:3, :3] = R.transpose()
    Rt[:3, 3] = T
    Rt[3, 3] = 1.0
    return Rt


def get_projection_matrix(znear, zfar, fovx, fovy) -> np.ndarray:
    tan_half_fovy = math.tan(fovy / 2)
    tan_half_fovx = math.tan(fovx / 2)

    top = tan_half_fovy * znear
    bottom = -top
    right = tan_half_fovx * znear
    left = -right

    P = np.zeros((4, 4), dtype=np.float32)
    z_sign = 1.0
    P[0, 0] = 2.0 * znear / (right - left)
    P[1, 1] = 2.0 * znear / (top - bottom)
    P[0, 2] = (right + left) / (right - left)
    P[1, 2] = (top + bottom) / (top - bottom)
    P[3, 2] = z_sign
    P[2, 2] = z_sign * zfar / (zfar - znear)
    P[2, 3] = -(zfar * znear) / (zfar - znear)
    return P


@dataclasses.dataclass
class Camera:
    R: np.ndarray  # (3, 3)
    T: np.ndarray  # (3,)
    fovx: float
    fovy: float
    width: int
    height: int
    znear: float = 0.01
    zfar: float = 100.0
    device: str = "cuda"

    def __post_init__(self):
        w2v = get_world_to_view(self.R, self.T)
        proj = get_projection_matrix(self.znear, self.zfar, self.fovx, self.fovy)
        self.world_view_transform = torch.tensor(w2v, device=self.device).transpose(0, 1)
        self.projection_matrix = torch.tensor(proj, device=self.device).transpose(0, 1)
        self.full_proj_transform = (
            self.world_view_transform.unsqueeze(0).bmm(self.projection_matrix.unsqueeze(0))
        ).squeeze(0)
        self.camera_center = self.world_view_transform.inverse()[3, :3]

    @property
    def image_height(self):
        return self.height

    @property
    def image_width(self):
        return self.width

    @property
    def FoVx(self):
        return self.fovx

    @property
    def FoVy(self):
        return self.fovy

    def frustum_planes_world(self) -> torch.Tensor:
        """Six view-frustum planes in world space as (a, b, c, d) with
        ax+by+cz+d >= 0 for points inside the frustum.

        Extracted from the full (view @ proj) matrix following the standard
        Gribb/Hartmann plane-extraction method, then used by
        `vega.culling` for object-level early culling (paper §6.2).
        """
        m = self.full_proj_transform.transpose(0, 1)  # row-major (col-vector convention)
        rows = [m[i, :] for i in range(4)]
        planes = torch.stack([
            rows[3] + rows[0],  # left
            rows[3] - rows[0],  # right
            rows[3] + rows[1],  # bottom
            rows[3] - rows[1],  # top
            rows[3] + rows[2],  # near
            rows[3] - rows[2],  # far
        ], dim=0)
        norm = planes[:, :3].norm(dim=1, keepdim=True).clamp_min(1e-8)
        planes = planes / norm
        return planes  # (6, 4)


def look_at_RT(eye: np.ndarray, target: np.ndarray, up: np.ndarray = np.array([0, 1, 0])):
    """Build (R, T) in the 3DGS convention for a camera at `eye` looking at
    `target`."""
    forward = target - eye
    forward = forward / np.linalg.norm(forward)
    right = np.cross(forward, up)
    right = right / (np.linalg.norm(right) + 1e-8)
    true_up = np.cross(right, forward)

    # Camera-to-world rotation: columns are camera axes (right, down, forward).
    # Two convention notes vs. a "natural" OpenGL look-at:
    #  - +forward (not -forward): get_projection_matrix's z_sign=1 assumes
    #    the camera looks down its own +Z axis (the computer-vision/COLMAP
    #    convention the real ORBIT camera_to_world extrinsics use).
    #  - -true_up (not +true_up): that same convention is y-down (image row
    #    index increases downward — confirmed from the real dataset's own
    #    camera_to_world matrices, whose 2nd column consistently points
    #    toward world -Y). Using +true_up here rendered upright scenes
    #    upside down.
    c2w_R = np.stack([right, -true_up, forward], axis=1).astype(np.float32)
    # 3DGS convention: R is camera-to-world (as used in get_world_to_view above,
    # which transposes R to get world->camera).
    R = c2w_R
    T = (-R.transpose() @ eye).astype(np.float32)
    return R, T


def orbit_cameras(n_cameras: int, radius: float = 4.0, height: float = 0.0,
                   target=(0.0, 0.0, 0.0), width: int = 400, height_px: int = 400,
                   fov_deg: float = 60.0, device: str = "cuda") -> list[Camera]:
    """A ring of cameras orbiting the origin — a simple stand-in for a
    multi-view capture rig / free-viewpoint trajectory."""
    target = np.array(target, dtype=np.float32)
    fov = math.radians(fov_deg)
    cams = []
    for i in range(n_cameras):
        theta = 2 * math.pi * i / n_cameras
        eye = target + np.array([radius * math.cos(theta), height, radius * math.sin(theta)], dtype=np.float32)
        R, T = look_at_RT(eye, target)
        cams.append(Camera(R=R, T=T, fovx=fov, fovy=fov, width=width, height=height_px, device=device))
    return cams
