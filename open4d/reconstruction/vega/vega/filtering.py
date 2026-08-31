"""Dynamicity-based object filtering — paper §5.3 (second half).

Objects classified as static (`dyn(O) <= threshold`, see `vega.dynamicity`)
have their non-color attributes (position, rotation, scale, opacity) dropped
from the residual frame entirely and are reconstructed by reusing the key
frame's attributes for that object instead. Only dynamic objects' non-color
attributes are actually transmitted in the residual frame.
"""
from __future__ import annotations

import dataclasses

import torch

from vega.gaussians import GaussianSet


@dataclasses.dataclass
class FilterPlan:
    transmitted_objects: set[int]   # dynamic: non-color attrs sent this frame
    reused_objects: set[int]        # static: reuse key frame's attrs


def plan_filtering(all_object_ids: list[int], dynamic_objects: set[int]) -> FilterPlan:
    all_ids = set(all_object_ids)
    return FilterPlan(
        transmitted_objects=all_ids & dynamic_objects,
        reused_objects=all_ids - dynamic_objects,
    )


def apply_filtering(residual_gs: GaussianSet, key_gs: GaussianSet, plan: FilterPlan) -> GaussianSet:
    """Reconstructs what a client would render for this residual frame after
    filtering: dynamic objects keep the residual frame's own (freshly
    trained) non-color attributes; static objects fall back to the key
    frame's attributes for the same object id.

    Objects are reassembled by concatenating whole per-object slices (rather
    than a masked in-place overwrite), since the *number* of Gaussians per
    object can differ between the key frame and a residual frame — each
    frame's point cloud here is independently subsampled from a real RGBD
    fusion, so per-object counts naturally drift frame to frame even though
    the object identity (id) stays consistent via `vega.segmentation`. This
    mirrors exactly what the client (`vega.player.StreamingPlayer`) does when
    reassembling frames from the wire format.
    """
    parts = []
    for oid in sorted(plan.reused_objects):
        mask = key_gs.object_mask(oid)
        if mask.any():
            parts.append(key_gs.subset(mask))
    for oid in sorted(plan.transmitted_objects):
        mask = residual_gs.object_mask(oid)
        if mask.any():
            parts.append(residual_gs.subset(mask))
    return GaussianSet.cat(parts)


def non_color_bytes_per_gaussian() -> int:
    """xyz(3) + scale(3) + rotation(4) + opacity(1) floats, fp16 on the wire."""
    return (3 + 3 + 4 + 1) * 2


def residual_non_color_size_bytes(residual_gs: GaussianSet, plan: FilterPlan) -> int:
    n_transmitted = sum(int(residual_gs.object_mask(oid).sum()) for oid in plan.transmitted_objects)
    return n_transmitted * non_color_bytes_per_gaussian()
