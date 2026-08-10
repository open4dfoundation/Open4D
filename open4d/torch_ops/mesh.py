"""Mesh operations on torch tensors, replacing the PyTorch3D functions Open4D used.

PyTorch3D is a compiled extension built against one exact torch build, so every
codec that imported it inherited that codec's torch and CUDA pins. Those pins are
what forced separate environments: n4mc wanted torch 2.6 to satisfy
pytorch3d 0.7.8 while tsmc wanted 2.7.0+cu126, and no single install satisfied
both. Open4D used five things from the library, all of them small, so they live
here instead and the pins collapse.

Two different jobs are served, and they are implemented differently on purpose:

- The training path (`chamfer_distance`, the normals, `sample_points_from_mesh`)
  is pure torch and differentiable, because n4mc backpropagates through it.
- The evaluation path (`point_face_distance`) is not differentiable. It needs the
  closest triangle to each of ~1e6 query points, which is O(points x triangles)
  if written directly, so it uses Open3D's BVH. Open3D is already required by the
  unified environment. A brute-force torch implementation is kept alongside it as
  `point_face_distance_bruteforce`, both as a fallback when Open3D is missing and
  as the oracle the fast path is tested against.

Squared distances are returned where PyTorch3D returned squared distances, so
call sites that take a square root afterwards keep working unchanged.
"""

from __future__ import annotations

import torch
import torch.nn.functional as F

__all__ = [
    "Meshes",
    "chamfer_distance",
    "face_normals",
    "point_face_distance",
    "point_face_distance_bruteforce",
    "sample_points_from_mesh",
    "vertex_normals",
]

_EPS = 1e-6


# ----------------------------
# Normals
# ----------------------------
def face_normals(verts: torch.Tensor, faces: torch.Tensor) -> torch.Tensor:
    """Unit normal per face, following PyTorch3D's corner ordering."""
    tri = verts[faces]
    normals = torch.cross(
        tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0], dim=1
    )
    return F.normalize(normals, eps=_EPS, dim=1)


def vertex_normals(verts: torch.Tensor, faces: torch.Tensor) -> torch.Tensor:
    """Area-weighted unit normal per vertex.

    Each face contributes its cross product -- whose magnitude is twice the face
    area -- to all three of its corners, so larger faces count for more. This is
    the weighting PyTorch3D uses, reproduced here so results do not shift.
    """
    result = torch.zeros_like(verts)
    tri = verts[faces]
    v0, v1, v2 = tri[:, 0], tri[:, 1], tri[:, 2]
    result.index_add_(0, faces[:, 0], torch.cross(v1 - v0, v2 - v0, dim=1))
    result.index_add_(0, faces[:, 1], torch.cross(v2 - v1, v0 - v1, dim=1))
    result.index_add_(0, faces[:, 2], torch.cross(v0 - v2, v1 - v2, dim=1))
    return F.normalize(result, eps=_EPS, dim=1)


# ----------------------------
# Surface sampling
# ----------------------------
def sample_points_from_mesh(
    verts: torch.Tensor,
    faces: torch.Tensor,
    num_samples: int,
    normals: torch.Tensor | None = None,
):
    """Sample points uniformly over surface area.

    Faces are drawn in proportion to their area, then a point is placed inside
    the chosen triangle using the square-root barycentric trick, which keeps the
    distribution uniform rather than clustering at the first corner.
    """
    tri = verts[faces]
    areas = 0.5 * torch.linalg.norm(
        torch.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0], dim=1), dim=1
    )
    cdf = torch.cumsum(areas / areas.sum().clamp_min(_EPS), dim=0)

    num_samples = int(num_samples)
    picked = torch.searchsorted(
        cdf, torch.rand(num_samples, device=verts.device)
    ).clamp_max(len(faces) - 1)
    chosen = faces[picked]

    u = torch.rand(num_samples, 1, device=verts.device)
    v = torch.rand(num_samples, 1, device=verts.device)
    root = v.sqrt()
    w0, w1, w2 = 1.0 - root, (1.0 - u) * root, u * root

    points = (
        verts[chosen[:, 0]] * w0
        + verts[chosen[:, 1]] * w1
        + verts[chosen[:, 2]] * w2
    )
    if normals is None:
        return points, None
    sampled = (
        normals[chosen[:, 0]] * w0
        + normals[chosen[:, 1]] * w1
        + normals[chosen[:, 2]] * w2
    )
    return points, F.normalize(sampled, eps=_EPS, dim=1)


# ----------------------------
# Chamfer distance
# ----------------------------
def _nearest_square_distance(a: torch.Tensor, b: torch.Tensor) -> torch.Tensor:
    """Squared distance from each point of *a* to its nearest point in *b*."""
    return torch.cdist(a, b).min(dim=2).values ** 2


def chamfer_distance(
    x: torch.Tensor,
    y: torch.Tensor,
    batch_reduction: str | None = "mean",
    point_reduction: str | None = "mean",
    single_directional: bool = False,
):
    """Chamfer distance between two batched point sets, in squared distances.

    Signature and reduction semantics follow `pytorch3d.loss.chamfer_distance`,
    including its return of a `(loss, normals_loss)` pair. Normals are not
    computed here -- no Open4D call site requested them -- so the second element
    is always None, which is what those call sites already discard.
    """
    if x.dim() != 3 or y.dim() != 3:
        raise ValueError("chamfer_distance expects (N, P, 3) tensors")

    def reduce_points(values: torch.Tensor) -> torch.Tensor:
        if point_reduction == "mean":
            return values.mean(dim=1)
        if point_reduction == "sum":
            return values.sum(dim=1)
        if point_reduction == "max":
            return values.max(dim=1).values
        if point_reduction is None:
            return values
        raise ValueError(f"unknown point_reduction {point_reduction!r}")

    loss = reduce_points(_nearest_square_distance(x, y))
    if not single_directional:
        loss = loss + reduce_points(_nearest_square_distance(y, x))

    if point_reduction is None:
        return loss, None
    if batch_reduction == "mean":
        return loss.mean(), None
    if batch_reduction == "sum":
        return loss.sum(), None
    if batch_reduction is None:
        return loss, None
    raise ValueError(f"unknown batch_reduction {batch_reduction!r}")


# ----------------------------
# Point-to-face distance
# ----------------------------
def _closest_point_on_triangle(
    points: torch.Tensor, tri: torch.Tensor
) -> torch.Tensor:
    """Closest point on each triangle to each query point, shape (P, T, 3).

    The standard region test: project onto the triangle's plane, then clamp into
    whichever vertex, edge, or interior region the projection falls in.
    """
    a, b, c = tri[:, 0], tri[:, 1], tri[:, 2]
    ab, ac = (b - a).unsqueeze(0), (c - a).unsqueeze(0)
    ap = points.unsqueeze(1) - a.unsqueeze(0)

    d1 = (ab * ap).sum(-1)
    d2 = (ac * ap).sum(-1)
    bp = points.unsqueeze(1) - b.unsqueeze(0)
    d3 = (ab * bp).sum(-1)
    d4 = (ac * bp).sum(-1)
    cp = points.unsqueeze(1) - c.unsqueeze(0)
    d5 = (ab * cp).sum(-1)
    d6 = (ac * cp).sum(-1)

    va = d3 * d6 - d5 * d4
    vb = d5 * d2 - d1 * d6
    vc = d1 * d4 - d3 * d2
    denom = (va + vb + vc).clamp_min(_EPS)

    v = (vb / denom).unsqueeze(-1)
    w = (vc / denom).unsqueeze(-1)

    # The reference algorithm is a chain of early returns, so an earlier region
    # wins outright over a later one. torch.where has no such ordering, so the
    # regions are applied lowest priority first and the interior seeds the
    # result -- the last write for any given point is its highest-priority
    # region. Applying them in source order would let the edge tests overwrite a
    # vertex answer wherever floating point makes two conditions overlap.
    result = a.unsqueeze(0) + ab * v + ac * w

    denom_bc = ((d4 - d3) + (d5 - d6)).clamp_min(_EPS)
    t_bc = ((d4 - d3) / denom_bc).unsqueeze(-1)
    result = torch.where(
        ((va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0)).unsqueeze(-1),
        b.unsqueeze(0) + (c - b).unsqueeze(0) * t_bc,
        result,
    )

    t_ac = (d2 / (d2 - d6).clamp_min(_EPS)).unsqueeze(-1)
    result = torch.where(
        ((vb <= 0) & (d2 >= 0) & (d6 <= 0)).unsqueeze(-1),
        a.unsqueeze(0) + ac * t_ac,
        result,
    )

    result = torch.where(
        ((d6 >= 0) & (d5 <= d6)).unsqueeze(-1), c.unsqueeze(0), result
    )

    t_ab = (d1 / (d1 - d3).clamp_min(_EPS)).unsqueeze(-1)
    result = torch.where(
        ((vc <= 0) & (d1 >= 0) & (d3 <= 0)).unsqueeze(-1),
        a.unsqueeze(0) + ab * t_ab,
        result,
    )

    result = torch.where(
        ((d3 >= 0) & (d4 <= d3)).unsqueeze(-1), b.unsqueeze(0), result
    )
    result = torch.where(
        ((d1 <= 0) & (d2 <= 0)).unsqueeze(-1), a.unsqueeze(0), result
    )
    return result


def point_face_distance_bruteforce(
    points: torch.Tensor, verts: torch.Tensor, faces: torch.Tensor,
    chunk: int = 4096,
):
    """Exact squared distance to the nearest face, and that face's index.

    Every point is compared against every triangle, chunked only to bound peak
    memory. Correct at any size and slow at most of them -- this exists as the
    reference the accelerated path is checked against, and as the fallback when
    Open3D is unavailable.
    """
    tri = verts[faces]
    best_distance, best_index = [], []
    for start in range(0, len(points), chunk):
        block = points[start : start + chunk]
        closest = _closest_point_on_triangle(block, tri)
        squared = ((closest - block.unsqueeze(1)) ** 2).sum(-1)
        distance, index = squared.min(dim=1)
        best_distance.append(distance)
        best_index.append(index)
    return torch.cat(best_distance), torch.cat(best_index)


def point_face_distance(
    points: torch.Tensor, verts: torch.Tensor, faces: torch.Tensor
):
    """Squared distance to the nearest face, and that face's index.

    Uses Open3D's bounding-volume hierarchy, which turns the O(points x
    triangles) scan into something that finishes on a full evaluation sweep.
    Not differentiable, and the tensors make a round trip through the CPU;
    both are fine for the evaluation-only call sites that need it.
    """
    try:
        import numpy as np
        import open3d as o3d
    except ImportError:
        return point_face_distance_bruteforce(points, verts, faces)

    scene = o3d.t.geometry.RaycastingScene()
    scene.add_triangles(
        o3d.core.Tensor(
            verts.detach().cpu().numpy().astype(np.float32), dtype=o3d.core.float32
        ),
        o3d.core.Tensor(
            faces.detach().cpu().numpy().astype(np.uint32), dtype=o3d.core.uint32
        ),
    )
    query = o3d.core.Tensor(
        points.detach().cpu().numpy().astype(np.float32), dtype=o3d.core.float32
    )
    found = scene.compute_closest_points(query)

    closest = torch.as_tensor(
        found["points"].numpy(), device=points.device, dtype=points.dtype
    )
    index = torch.as_tensor(
        found["primitive_ids"].numpy().astype("int64"), device=points.device
    )
    return ((closest - points) ** 2).sum(-1), index


# ----------------------------
# Minimal Meshes stand-in
# ----------------------------
class Meshes:
    """The slice of `pytorch3d.structures.Meshes` that Open4D actually used.

    A real Meshes batches ragged meshes into padded and packed layouts. Every
    Open4D call site builds one from a single mesh and immediately asks for that
    mesh back, so this stores the list and computes normals on demand.
    """

    def __init__(self, verts, faces):
        self._verts = list(verts)
        self._faces = list(faces)
        if len(self._verts) != len(self._faces):
            raise ValueError("verts and faces must have the same length")

    def __len__(self) -> int:
        return len(self._verts)

    def verts_list(self):
        return self._verts

    def faces_list(self):
        return self._faces

    def verts_normals_list(self):
        return [vertex_normals(v, f) for v, f in zip(self._verts, self._faces)]

    def faces_normals_list(self):
        return [face_normals(v, f) for v, f in zip(self._verts, self._faces)]

    def verts_packed(self):
        return torch.cat(self._verts, dim=0)

    def verts_normals_packed(self):
        return torch.cat(self.verts_normals_list(), dim=0)

    def faces_normals_packed(self):
        return torch.cat(self.faces_normals_list(), dim=0)
