"""Dynamicity-based object filtering — paper §5.3, Eq. 1-4.

Estimates, for a residual frame trained against the frame immediately after
a key frame, how "dynamic" each object is: dynamic objects produce larger
gradient magnitudes during those first few training iterations. Objects
below the dynamicity threshold are considered static and can be skipped
entirely in the residual frame (their attributes are simply reused from the
key frame) — see `vega.filtering`.
"""
from __future__ import annotations

import dataclasses

import torch

from vega.cameras import Camera
from vega.gaussians import GaussianSet
from vega.metrics import vega_loss
from vega.rasterize import render


@dataclasses.dataclass
class DynamicityResult:
    per_gaussian_m: torch.Tensor          # m(g), Eq. 3 (left)
    per_object_M: dict[int, float]        # M(O), Eq. 3 (right)
    per_object_dyn: dict[int, float]      # dyn(O), Eq. 4
    dynamic_objects: set[int]             # objects with dyn(O) > threshold


def estimate_dynamicity(
    residual_gaussians: GaussianSet,
    cameras: list[Camera],
    gt_images: list[torch.Tensor],
    bg_color: torch.Tensor,
    n_iters: int = 20,
    lr: float = 5e-3,
    alpha: float = 0.8,
    beta: float = 0.2,
    threshold: float = 0.5,
) -> DynamicityResult:
    """Runs `n_iters` optimization steps of the residual frame against the
    key frame's (already-trained) starting point, and accumulates the
    per-Gaussian non-color gradient magnitude at each iteration (Eq. 3).

    `residual_gaussians` should already require grad (i.e. be a trainable
    copy initialized from the key frame's attributes).
    """
    params = residual_gaussians.non_color_leaf_tensors() + [residual_gaussians.sh_dc, residual_gaussians.sh_rest]
    for p in params:
        p.requires_grad_(True)
    optimizer = torch.optim.Adam(params, lr=lr)

    n = len(residual_gaussians)
    accum = torch.zeros(n, device=residual_gaussians.get_xyz.device)

    n_views = len(cameras)
    for k in range(n_iters):
        cam = cameras[k % n_views]
        gt = gt_images[k % n_views]
        optimizer.zero_grad(set_to_none=True)
        out = render(cam, residual_gaussians, bg_color)
        loss = vega_loss(out["render"], gt, alpha=alpha, beta=beta)
        loss.backward()
        g = residual_gaussians.per_gaussian_noncolor_grad_norm()
        accum += g.detach()
        optimizer.step()

    m_g = accum / n_iters  # Eq. 3 (left): m(g)

    per_object_M: dict[int, float] = {}
    for oid in residual_gaussians.object_ids().tolist():
        mask = residual_gaussians.object_mask(oid)
        per_object_M[oid] = m_g[mask].mean().item() if mask.any() else 0.0

    m_max = max(per_object_M.values()) if per_object_M else 1.0
    m_max = max(m_max, 1e-12)
    per_object_dyn = {oid: m / m_max for oid, m in per_object_M.items()}  # Eq. 4
    dynamic_objects = {oid for oid, d in per_object_dyn.items() if d > threshold}

    return DynamicityResult(
        per_gaussian_m=m_g.detach(),
        per_object_M=per_object_M,
        per_object_dyn=per_object_dyn,
        dynamic_objects=dynamic_objects,
    )
