"""Object-level early culling — paper §6.2.

Instead of frustum-culling individual Gaussians during final rendering (by
which point they have already paid for hash lookups + MLP inference), Vega
tests each *object's* precomputed bounding box against the view frustum
before any decoding work happens, and skips decoding entirely for objects
that are not visible.
"""
from __future__ import annotations

import torch

from vega.cameras import Camera
from vega.gaussians import GaussianSet


def aabb_intersects_frustum(box_min: torch.Tensor, box_max: torch.Tensor,
                             planes: torch.Tensor) -> bool:
    """Standard AABB-vs-frustum test: for each plane, take the box corner
    most aligned with the plane normal (the "positive vertex"); if that
    corner is still outside the plane, the whole box is outside it.
    """
    box_min = box_min.to(planes.device)
    box_max = box_max.to(planes.device)
    for i in range(planes.shape[0]):
        normal = planes[i, :3]
        d = planes[i, 3]
        p = torch.where(normal >= 0, box_max, box_min)
        if torch.dot(normal, p) + d < 0:
            return False
    return True


def early_cull(gaussians: GaussianSet, camera: Camera) -> tuple[set[int], set[int]]:
    """Returns (visible_object_ids, culled_object_ids) using each object's
    precomputed bounding box (`GaussianSet.object_bounding_boxes`) against
    `camera`'s view frustum.
    """
    boxes = gaussians.object_bounding_boxes()
    planes = camera.frustum_planes_world()
    visible, culled = set(), set()
    for oid, (bmin, bmax) in boxes.items():
        if aabb_intersects_frustum(bmin, bmax, planes):
            visible.add(oid)
        else:
            culled.add(oid)
    return visible, culled


def early_cull_from_boxes(boxes: dict[int, tuple[torch.Tensor, torch.Tensor]],
                           camera: Camera) -> tuple[set[int], set[int]]:
    """Same as `early_cull` but reusing already-computed bounding boxes
    (avoids recomputing them every frame when geometry hasn't changed)."""
    planes = camera.frustum_planes_world()
    visible, culled = set(), set()
    for oid, (bmin, bmax) in boxes.items():
        if aabb_intersects_frustum(bmin, bmax, planes):
            visible.add(oid)
        else:
            culled.add(oid)
    return visible, culled
