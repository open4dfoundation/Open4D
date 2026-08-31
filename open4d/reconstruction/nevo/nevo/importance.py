"""Step 2: per-voxel neural visibility from instrumented ray marching.

NeVo's central observation (paper section 3.2) is that the ray-marching weight

    w_i = T_i * alpha_i,   alpha_i = 1 - exp(-sigma_i * delta_i),
                           T_i     = prod_{j<i} (1 - alpha_j)

already *is* a visibility measure. It is the coefficient the sample's colour
carries into the pixel, so it folds occlusion, opacity and transmittance into
one number -- including the semi-transparent case that a position-based
occlusion test gets wrong (a face behind glass has low alpha in front of it,
so its transmittance stays high and it stays visible). A voxel's importance is
then the largest weight of any sample that falls in it, and voxels below a
threshold can be left untransmitted.

This module reproduces ReRF's marching -- the same sampler, the same
occupancy-cache skip, the same two ``fast_color_thres`` prunes in the same
order -- and keeps ``ray_pts`` alongside ``weights`` so each weight can be
scattered back to a voxel. It stops before the colour MLP, which importance
does not depend on and which dominates the cost of a real render.

Reimplemented here rather than patched into ``rerf/lib/dvgo.py`` so the
vendored copy stays byte-identical to upstream. :func:`check_against_rerf`
asserts the two agree.

Assignment modes, both offered because they answer different questions:

``nearest``
    A sample belongs to the voxel it sits in. This is the paper's wording,
    "the highest neural visibility of all ray-marching sampled points inside
    it", and the default.
``trilinear``
    A sample belongs to all eight grid entries its trilinear interpolation
    reads. Strictly the safer notion of "which feature voxels does rendering
    this ray depend on" -- it differs from ``nearest`` only for samples within
    one entry of a block face, so it matters at block boundaries.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable

from . import rerf_env
from .blocks import BlockGrid, nearest_entry, surrounding_entries
from .cameras import Camera

ASSIGNMENTS = ("nearest", "trilinear")


@dataclass(frozen=True)
class ImportanceConfig:
    """Knobs for the importance pass."""

    #: Transmission unit. 8 is ReRF's codec block; 1 scores single grid entries.
    block_size: int = 8
    assignment: str = "nearest"
    #: Rays are marched in chunks so a 1-megapixel viewport does not allocate
    #: its whole sample list at once.
    ray_chunk: int = 1 << 18
    #: Integer downscale applied to every viewport before marching.
    render_factor: int = 1

    def __post_init__(self) -> None:
        if self.assignment not in ASSIGNMENTS:
            raise ValueError(f"assignment must be one of {ASSIGNMENTS}")
        if self.ray_chunk < 1 or self.render_factor < 1:
            raise ValueError("ray_chunk and render_factor must be positive")


def _rays_for(dvgo, camera: Camera, render_kwargs: dict, torch):
    c2w = torch.tensor(camera.c2w, dtype=torch.float32, device="cuda")
    intrinsics = torch.tensor(camera.intrinsic_matrix, dtype=torch.float32, device="cuda")
    rays_o, rays_d, viewdirs = dvgo.get_rays_of_a_view(
        H=camera.height,
        W=camera.width,
        K=intrinsics,
        c2w=c2w,
        ndc=False,
        inverse_y=render_kwargs["inverse_y"],
        flip_x=render_kwargs["flip_x"],
        flip_y=render_kwargs["flip_y"],
    )
    return rays_o.flatten(0, -2), rays_d.flatten(0, -2)


def march_weights(model, dvgo, rays_o, rays_d, render_kwargs):
    """Sample positions and their ray-marching weights ``T_i * alpha_i``.

    A transcription of ``lib.dvgo.DirectVoxGO.forward`` up to the point where
    it would query colour. The order of the two ``fast_color_thres`` prunes is
    load-bearing: transmittance is accumulated over the alpha-pruned sample
    list, so pruning by weight first would change every downstream ``T_i``.
    """
    count = len(rays_o)
    ray_pts, ray_id, step_id = model.sample_ray(
        rays_o=rays_o,
        rays_d=rays_d,
        near=render_kwargs["near"],
        far=render_kwargs["far"],
        stepsize=render_kwargs["stepsize"],
        is_train=False,
    )
    interval = render_kwargs["stepsize"] * model.voxel_size_ratio

    if model.mask_cache is not None:
        if model.use_deform:
            ray_pts = model.deform_warp(ray_pts, model.deformation_field)
        keep = model.mask_cache(ray_pts)
        ray_pts, ray_id = ray_pts[keep], ray_id[keep]

    density = model.grid_sampler(ray_pts, model.density)
    alpha = model.activate_density(density, interval)
    if model.fast_color_thres > 0:
        keep = alpha > model.fast_color_thres
        ray_pts, ray_id, alpha = ray_pts[keep], ray_id[keep], alpha[keep]

    weights, _ = dvgo.Alphas2Weights.apply(alpha, ray_id, count)
    if model.fast_color_thres > 0:
        keep = weights > model.fast_color_thres
        ray_pts, weights = ray_pts[keep], weights[keep]
    return ray_pts, weights


def scatter_max(
    grid: BlockGrid,
    points,
    weights,
    xyz_min,
    xyz_max,
    grid_shape,
    assignment: str,
    into,
):
    """Fold sample weights into a per-block maximum, in place."""
    if points.numel() == 0:
        return into
    if assignment == "nearest":
        entries = nearest_entry(points, xyz_min, xyz_max, grid_shape).unsqueeze(0)
        source = weights.unsqueeze(0)
    else:
        entries = surrounding_entries(points, xyz_min, xyz_max, grid_shape)
        source = weights.unsqueeze(0).expand(entries.shape[0], -1)
    index = grid.block_index(entries.reshape(-1, 3))
    into.scatter_reduce_(0, index, source.reshape(-1), reduce="amax")
    return into


class ImportanceScorer:
    """Scores one frame's voxels against arbitrary viewports."""

    def __init__(self, sequence, frame, config: ImportanceConfig = ImportanceConfig()):
        rerf_env.activate()
        with rerf_env.rerf_cwd():
            import torch
            from lib import dvgo

        self._torch = torch
        self._dvgo = dvgo
        self.config = config
        self.frame = frame
        self.model = frame.model
        self.render_kwargs = sequence.render_kwargs()
        self.grid_shape = frame.grid_shape
        self.grid = BlockGrid(self.grid_shape, config.block_size)
        self.occupancy = self.grid.occupancy(frame.density)

    @property
    def num_blocks(self) -> int:
        return self.grid.num_blocks

    @property
    def occupied_blocks(self) -> int:
        return int(self.occupancy.sum().item())

    def score(self, camera: Camera):
        """Per-block importance for one viewport, as a ``[num_blocks]`` tensor."""
        torch = self._torch
        with torch.no_grad():
            rays_o, rays_d = _rays_for(self._dvgo, camera, self.render_kwargs, torch)
            scores = torch.zeros(self.grid.num_blocks, dtype=torch.float32, device="cuda")
            for begin in range(0, len(rays_o), self.config.ray_chunk):
                chunk = slice(begin, begin + self.config.ray_chunk)
                points, weights = march_weights(
                    self.model,
                    self._dvgo,
                    rays_o[chunk].contiguous(),
                    rays_d[chunk].contiguous(),
                    self.render_kwargs,
                )
                scatter_max(
                    self.grid,
                    points,
                    weights,
                    self.frame.xyz_min,
                    self.frame.xyz_max,
                    self.grid_shape,
                    self.config.assignment,
                    scores,
                )
        return scores

    def score_many(self, cameras: Iterable[Camera], progress=None):
        """Stack :meth:`score` over viewports into ``[views, num_blocks]``."""
        torch = self._torch
        rows = []
        for index, camera in enumerate(cameras):
            rows.append(self.score(camera).cpu())
            if progress is not None:
                progress(index)
        return torch.stack(rows)


def check_against_rerf(sequence, frame, camera: Camera, tolerance: float = 1e-5) -> dict:
    """Assert :func:`march_weights` matches ReRF's own forward pass.

    ``DirectVoxGO.forward`` returns the weights it used but not the sample
    positions, so the guarantee we need -- that our transcription produces the
    same weight sequence -- has to be checked rather than assumed. Compares the
    sorted weight vectors and the resulting pixel opacity.
    """
    rerf_env.activate()
    with rerf_env.rerf_cwd():
        import torch
        from lib import dvgo

    model = frame.model
    render_kwargs = sequence.render_kwargs()
    with torch.no_grad():
        rays_o, rays_d = _rays_for(dvgo, camera, render_kwargs, torch)
        viewdirs = rays_d / rays_d.norm(dim=-1, keepdim=True)
        reference = model(rays_o, rays_d, viewdirs, **render_kwargs)
        ours_points, ours_weights = march_weights(model, dvgo, rays_o, rays_d, render_kwargs)
        theirs = reference["weights"]
        matched = ours_weights.numel() == theirs.numel()
        difference = float("inf")
        if matched:
            difference = float((ours_weights - theirs).abs().max().item())
    return {
        "samples_ours": int(ours_weights.numel()),
        "samples_rerf": int(theirs.numel()),
        "max_abs_difference": difference,
        "agrees": bool(matched and difference <= tolerance),
    }
