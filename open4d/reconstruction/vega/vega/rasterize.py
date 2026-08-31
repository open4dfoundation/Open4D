"""Thin adapter around the `diff_gaussian_rasterization` CUDA extension
(the same rasterizer used by the original 3DGS paper and by the Queen /
3DGStream repos already present in this environment).
"""
from __future__ import annotations

import math

import torch

from vega.cameras import Camera
from vega.gaussians import GaussianSet
from vega.sh import eval_sh


def render(camera: Camera, gaussians: GaussianSet, bg_color: torch.Tensor,
           sh_degree: int | None = None, colors_override: torch.Tensor | None = None):
    """Render `gaussians` from `camera`.

    Args:
        colors_override: (N, 3) precomputed per-Gaussian RGB in [0, 1] to use
            instead of evaluating SH (used by hierarchical color encoding,
            where color comes from the hash+MLP decoder rather than SH).
    Returns:
        dict with "render" (3, H, W) and "radii" (N,) (screen-space radius,
        used for early-culling sanity checks / visibility).
    """
    import diff_gaussian_rasterization as dgr

    sh_degree = gaussians.sh_degree if sh_degree is None else sh_degree

    screenspace_points = torch.zeros_like(gaussians.get_xyz, requires_grad=True)
    try:
        screenspace_points.retain_grad()
    except Exception:
        pass

    tanfovx = math.tan(camera.FoVx * 0.5)
    tanfovy = math.tan(camera.FoVy * 0.5)

    raster_settings = dgr.GaussianRasterizationSettings(
        image_height=camera.image_height,
        image_width=camera.image_width,
        tanfovx=tanfovx,
        tanfovy=tanfovy,
        bg=bg_color,
        scale_modifier=1.0,
        viewmatrix=camera.world_view_transform,
        projmatrix=camera.full_proj_transform,
        sh_degree=sh_degree,
        campos=camera.camera_center,
        prefiltered=False,
        debug=False,
    )
    rasterizer = dgr.GaussianRasterizer(raster_settings=raster_settings)

    means3D = gaussians.get_xyz
    means2D = screenspace_points
    opacity = gaussians.get_opacity
    scales = gaussians.get_scaling
    rotations = gaussians.get_rotation

    shs = None
    colors_precomp = None
    if colors_override is not None:
        colors_precomp = colors_override
    else:
        shs = gaussians.get_features

    rendered_image, radii = rasterizer(
        means3D=means3D,
        means2D=means2D,
        shs=shs,
        colors_precomp=colors_precomp,
        opacities=opacity,
        scales=scales,
        rotations=rotations,
        cov3D_precomp=None,
    )
    return {"render": rendered_image, "radii": radii, "viewspace_points": screenspace_points}


def view_directions(camera: Camera, xyz: torch.Tensor) -> torch.Tensor:
    """Unit view directions from each Gaussian to the camera center."""
    d = xyz - camera.camera_center.unsqueeze(0)
    return torch.nn.functional.normalize(d, dim=-1)
