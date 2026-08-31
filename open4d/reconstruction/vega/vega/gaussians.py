"""Core per-frame Gaussian container.

Mirrors the standard 3DGS attribute set used across the field (position,
rotation, scale, opacity, SH color) plus the one piece of metadata Vega adds
on top: a per-Gaussian ``object_id`` used for object-level selective
computation (paper §4.1).
"""
from __future__ import annotations

import dataclasses
from typing import Optional

import torch
import torch.nn.functional as F

from vega.sh import MAX_SH_DEGREE


def _n_sh_rest(deg: int) -> int:
    return (deg + 1) ** 2 - 1


@dataclasses.dataclass
class GaussianSet:
    """A set of 3D Gaussians for a single frame, with per-Gaussian object ids.

    Raw (unactivated) parameters are stored so this can be used directly in a
    training loop; use the ``get_*`` properties for activated values.
    """

    xyz: torch.Tensor            # (N, 3)
    scale_raw: torch.Tensor      # (N, 3), scale = exp(scale_raw)
    rot_raw: torch.Tensor        # (N, 4), rotation = normalize(rot_raw)
    opacity_raw: torch.Tensor    # (N, 1), opacity = sigmoid(opacity_raw)
    sh_dc: torch.Tensor          # (N, 1, 3)
    sh_rest: torch.Tensor        # (N, n_sh_rest(deg), 3)
    object_id: torch.Tensor      # (N,) long, non-differentiable
    sh_degree: int = 3

    # ---- activated attribute accessors -------------------------------
    @property
    def get_xyz(self) -> torch.Tensor:
        return self.xyz

    @property
    def get_scaling(self) -> torch.Tensor:
        return torch.exp(self.scale_raw)

    @property
    def get_rotation(self) -> torch.Tensor:
        return F.normalize(self.rot_raw, dim=-1)

    @property
    def get_opacity(self) -> torch.Tensor:
        return torch.sigmoid(self.opacity_raw)

    @property
    def get_features(self) -> torch.Tensor:
        return torch.cat([self.sh_dc, self.sh_rest], dim=1)

    def __len__(self) -> int:
        return self.xyz.shape[0]

    @property
    def num_objects(self) -> int:
        return int(self.object_id.max().item()) + 1 if len(self) else 0

    def object_ids(self) -> torch.Tensor:
        return torch.unique(self.object_id)

    # ---- leaf/grad utilities ------------------------------------------
    def leaf_tensors(self):
        """The tensors that participate in autograd (excludes object_id)."""
        return [self.xyz, self.scale_raw, self.rot_raw, self.opacity_raw,
                self.sh_dc, self.sh_rest]

    def requires_grad_(self, flag: bool = True) -> "GaussianSet":
        for t in self.leaf_tensors():
            t.requires_grad_(flag)
        return self

    def detach(self) -> "GaussianSet":
        return GaussianSet(
            xyz=self.xyz.detach().clone(),
            scale_raw=self.scale_raw.detach().clone(),
            rot_raw=self.rot_raw.detach().clone(),
            opacity_raw=self.opacity_raw.detach().clone(),
            sh_dc=self.sh_dc.detach().clone(),
            sh_rest=self.sh_rest.detach().clone(),
            object_id=self.object_id.clone(),
            sh_degree=self.sh_degree,
        )

    def to(self, device) -> "GaussianSet":
        return GaussianSet(
            xyz=self.xyz.to(device),
            scale_raw=self.scale_raw.to(device),
            rot_raw=self.rot_raw.to(device),
            opacity_raw=self.opacity_raw.to(device),
            sh_dc=self.sh_dc.to(device),
            sh_rest=self.sh_rest.to(device),
            object_id=self.object_id.to(device),
            sh_degree=self.sh_degree,
        )

    def subset(self, mask: torch.Tensor) -> "GaussianSet":
        """Return a new GaussianSet containing only Gaussians where mask is True."""
        return GaussianSet(
            xyz=self.xyz[mask],
            scale_raw=self.scale_raw[mask],
            rot_raw=self.rot_raw[mask],
            opacity_raw=self.opacity_raw[mask],
            sh_dc=self.sh_dc[mask],
            sh_rest=self.sh_rest[mask],
            object_id=self.object_id[mask],
            sh_degree=self.sh_degree,
        )

    def object_mask(self, oid: int) -> torch.Tensor:
        return self.object_id == oid

    def per_gaussian_grad_norm(self) -> Optional[torch.Tensor]:
        """L2 norm of the gradient across all attributes, per Gaussian.

        Used by ``vega.dynamicity`` to compute m(g) in Eq. 3 of the paper.
        Returns None if gradients have not been populated (call after
        `.backward()`).
        """
        grads = []
        for t in self.leaf_tensors():
            if t.grad is None:
                return None
            g = t.grad.reshape(t.shape[0], -1)
            grads.append(g)
        all_grads = torch.cat(grads, dim=1)  # (N, D)
        return all_grads.norm(dim=1)

    def non_color_leaf_tensors(self):
        """xyz, scale, rotation, opacity — excludes SH color.

        Per paper §10 ("Robustness to dynamic lighting"): the dynamicity
        metric is computed from the gradient of *non-color* attributes only,
        so that lighting changes (which mainly affect SH) are not mistaken
        for physical motion.
        """
        return [self.xyz, self.scale_raw, self.rot_raw, self.opacity_raw]

    def per_gaussian_noncolor_grad_norm(self) -> Optional[torch.Tensor]:
        grads = []
        for t in self.non_color_leaf_tensors():
            if t.grad is None:
                return None
            grads.append(t.grad.reshape(t.shape[0], -1))
        return torch.cat(grads, dim=1).norm(dim=1)

    def object_bounding_boxes(self) -> dict[int, tuple[torch.Tensor, torch.Tensor]]:
        """Per-object axis-aligned bounding box, expanded by 3-sigma of scale.

        This is the "bounding box, precomputed on the server" mentioned in
        §6.2 for object-level early culling.
        """
        boxes = {}
        scales = self.get_scaling
        radius = (scales.max(dim=-1).values * 3.0).unsqueeze(-1)  # (N, 1)
        lo = self.xyz - radius
        hi = self.xyz + radius
        for oid in self.object_ids().tolist():
            m = self.object_mask(oid)
            if m.any():
                boxes[oid] = (lo[m].min(dim=0).values.detach().cpu(),
                              hi[m].max(dim=0).values.detach().cpu())
        return boxes

    @staticmethod
    def empty(sh_degree: int = 3, device="cuda") -> "GaussianSet":
        n_rest = _n_sh_rest(sh_degree)
        z3 = torch.zeros((0, 3), device=device)
        return GaussianSet(
            xyz=z3.clone(), scale_raw=z3.clone(), rot_raw=torch.zeros((0, 4), device=device),
            opacity_raw=torch.zeros((0, 1), device=device),
            sh_dc=torch.zeros((0, 1, 3), device=device),
            sh_rest=torch.zeros((0, n_rest, 3), device=device),
            object_id=torch.zeros((0,), dtype=torch.long, device=device),
            sh_degree=sh_degree,
        )

    @staticmethod
    def cat(sets: list["GaussianSet"]) -> "GaussianSet":
        sets = [s for s in sets if len(s) > 0]
        if not sets:
            raise ValueError("cannot concatenate zero GaussianSets")
        return GaussianSet(
            xyz=torch.cat([s.xyz for s in sets], dim=0),
            scale_raw=torch.cat([s.scale_raw for s in sets], dim=0),
            rot_raw=torch.cat([s.rot_raw for s in sets], dim=0),
            opacity_raw=torch.cat([s.opacity_raw for s in sets], dim=0),
            sh_dc=torch.cat([s.sh_dc for s in sets], dim=0),
            sh_rest=torch.cat([s.sh_rest for s in sets], dim=0),
            object_id=torch.cat([s.object_id for s in sets], dim=0),
            sh_degree=sets[0].sh_degree,
        )
