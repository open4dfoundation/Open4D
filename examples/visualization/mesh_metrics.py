"""Geometric error between two meshes that share no vertex correspondence.

A decoded mesh has its own vertex count and connectivity, so error cannot be a
per-vertex subtraction. Both distances here are nearest-neighbour distances, in
the two forms the compression literature uses:

- **point-to-point** (C2C) — the distance to the nearest reference vertex.
- **point-to-plane** (C2P) — that same offset projected onto the reference
  surface normal at the nearest vertex, so error that slides *along* the surface
  is not counted. This is the MPEG point-cloud-compression definition.

Both are one-sided, so every summary is reported in both directions and the
symmetric figure is the worse of the two. A codec that deletes a whole limb
scores well in the decoded->reference direction alone.

`nearest_neighbors` is SciPy's `cKDTree`, which is what TVMC's own
`evaluation.py` uses for the same query. The tests still check it against brute
force, since the thing worth verifying is the metric built on top of it.

    distances = point_to_point(decoded_positions, reference)
    summary = compare_meshes(ref_positions, ref_triangles, dec_positions, ...)

One honest limitation: these are per-vertex figures, not area-weighted surface
integrals. A region meshed densely counts for more than an equally large region
meshed coarsely. Comparing two codecs on the same reference is sound; comparing
absolute numbers against a tool that integrates over faces is not.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import NamedTuple

import numpy as np

# Import first: this puts the repository on sys.path for uninstalled clones.
from _common import require


class NearestNeighbors(NamedTuple):
    """Result of a nearest-neighbour query."""

    distances: np.ndarray  # (Q,) float64, Euclidean
    indices: np.ndarray    # (Q,) int64, into the reference positions


def nearest_neighbors(queries: np.ndarray, reference: np.ndarray) -> NearestNeighbors:
    """Exact nearest reference point for every query point, through SciPy.

    `cKDTree` rather than a hand-rolled search: TVMC's own `evaluation.py` uses
    it for this exact query, so depending on it here is no stricter than the
    repository already is.
    """
    queries = np.ascontiguousarray(queries, dtype=np.float64)
    reference = np.ascontiguousarray(reference, dtype=np.float64)
    if queries.ndim != 2 or queries.shape[1] != 3:
        raise ValueError(f"queries must have shape (Q, 3); got {queries.shape}")
    if reference.ndim != 2 or reference.shape[1] != 3:
        raise ValueError(
            f"reference must have shape (N, 3); got {reference.shape}"
        )
    if len(reference) == 0:
        raise ValueError("reference is empty; nothing to measure against")
    if len(queries) == 0:
        return NearestNeighbors(
            distances=np.empty(0), indices=np.empty(0, dtype=np.int64)
        )

    spatial = require("scipy.spatial", "player")
    distances, indices = spatial.cKDTree(reference).query(
        queries, k=1, workers=-1
    )
    return NearestNeighbors(
        distances=np.atleast_1d(distances),
        indices=np.atleast_1d(indices).astype(np.int64),
    )


def vertex_normals(positions: np.ndarray, triangles: np.ndarray) -> np.ndarray:
    """Area-weighted per-vertex normals, unit length where they are defined.

    Face normals are accumulated onto their corners with `np.bincount`, which is
    the fast path for this: `np.add.at` is the unbuffered ufunc and runs an order
    of magnitude slower. Vertices touched by no face, or by faces that cancel
    exactly, come back as zero vectors — `point_to_plane` treats those as having
    no usable surface orientation rather than inventing one.
    """
    positions = np.asarray(positions, dtype=np.float64)
    triangles = np.asarray(triangles, dtype=np.int64)
    normals = np.zeros_like(positions)
    if len(triangles) == 0:
        return normals

    corners = positions[triangles]
    face = np.cross(
        corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0]
    )
    for column in range(3):
        for axis in range(3):
            normals[:, axis] += np.bincount(
                triangles[:, column], weights=face[:, axis], minlength=len(positions)
            )

    lengths = np.linalg.norm(normals, axis=1, keepdims=True)
    return np.divide(normals, lengths, out=np.zeros_like(normals), where=lengths > 0)


def point_to_point(
    queries: np.ndarray, reference: np.ndarray
) -> np.ndarray:
    """Distance from each query point to the nearest reference vertex."""
    return nearest_neighbors(queries, reference).distances


def point_to_plane(
    queries: np.ndarray,
    reference: np.ndarray,
    reference_normals: np.ndarray,
) -> np.ndarray:
    """Nearest-vertex offset projected onto the reference surface normal.

    Where the reference normal is undefined (an unreferenced vertex, or exactly
    cancelling faces) this falls back to the point-to-point distance, which is
    an upper bound on it — quietly reporting zero there would flatter the codec.
    """
    result = nearest_neighbors(queries, reference)
    reference_normals = np.asarray(reference_normals, dtype=np.float64)
    if len(reference_normals) != len(reference):
        raise ValueError("reference_normals must have one row per reference vertex")

    delta = np.asarray(queries, dtype=np.float64) - np.asarray(
        reference, dtype=np.float64
    )[result.indices]
    normals = reference_normals[result.indices]
    projected = np.abs(np.einsum("ij,ij->i", delta, normals))
    usable = np.linalg.norm(normals, axis=1) > 0
    return np.where(usable, projected, result.distances)


@dataclass(frozen=True)
class DirectionalError:
    """Summary of one direction's per-vertex distances."""

    rms: float
    mean: float
    maximum: float
    psnr_db: float

    @classmethod
    def summarize(cls, distances: np.ndarray, peak: float) -> "DirectionalError":
        """Reduce per-vertex distances, with PSNR against a signal peak.

        `peak` is the reference bounding-box diagonal, the convention the MPEG
        metric software uses, so PSNR is scale-free. Two meshes that agree
        exactly give infinite PSNR; that is reported as `inf` rather than
        clamped, because a finite-looking ceiling reads as a real measurement.

        A degenerate reference — one vertex, so no bounding box — leaves PSNR
        with no peak to normalise against, and that is reported as `nan`. `inf`
        there would claim a perfect match when only the scale is missing.
        """
        if len(distances) == 0:
            return cls(rms=0.0, mean=0.0, maximum=0.0, psnr_db=float("inf"))
        squared = float(np.mean(np.square(distances)))
        rms = float(np.sqrt(squared))
        if squared <= 0.0:
            psnr = float("inf")
        elif peak <= 0.0:
            psnr = float("nan")
        else:
            psnr = float(10.0 * np.log10(peak * peak / squared))
        return cls(
            rms=rms,
            mean=float(np.mean(distances)),
            maximum=float(np.max(distances)),
            psnr_db=psnr,
        )


@dataclass(frozen=True)
class MeshComparison:
    """Both directions of error between one reference and one decoded mesh."""

    forward: DirectionalError            # decoded -> reference
    backward: DirectionalError           # reference -> decoded
    decoded_distances: np.ndarray        # per decoded vertex
    reference_distances: np.ndarray      # per reference vertex
    peak: float
    metric: str                          # "point" or "plane"

    @property
    def symmetric_rms(self) -> float:
        """The worse RMS of the two directions."""
        return max(self.forward.rms, self.backward.rms)

    @property
    def hausdorff(self) -> float:
        """The worse maximum of the two directions."""
        return max(self.forward.maximum, self.backward.maximum)

    @property
    def symmetric_psnr_db(self) -> float:
        """The worse (lower) PSNR of the two directions."""
        return min(self.forward.psnr_db, self.backward.psnr_db)


def bounding_box_diagonal(positions: np.ndarray) -> float:
    """Diagonal length of the axis-aligned bounding box."""
    positions = np.asarray(positions, dtype=np.float64)
    if len(positions) == 0:
        return 0.0
    return float(
        np.linalg.norm(positions.max(axis=0) - positions.min(axis=0))
    )


def compare_meshes(
    reference_positions: np.ndarray,
    reference_triangles: np.ndarray,
    decoded_positions: np.ndarray,
    decoded_triangles: np.ndarray,
    metric: str = "point",
    peak: float | None = None,
) -> MeshComparison:
    """Measure one decoded mesh against one reference mesh, both directions.

    `metric` is "point" for point-to-point or "plane" for point-to-plane.
    `peak` defaults to the reference bounding-box diagonal; pass a sequence-wide
    value to keep PSNR comparable across frames.
    """
    if metric not in ("point", "plane"):
        raise ValueError(f"metric must be 'point' or 'plane'; got {metric!r}")

    reference_positions = np.asarray(reference_positions, dtype=np.float64)
    decoded_positions = np.asarray(decoded_positions, dtype=np.float64)
    if peak is None:
        peak = bounding_box_diagonal(reference_positions)

    if metric == "point":
        decoded_distances = point_to_point(
            decoded_positions, reference_positions
        )
        reference_distances = point_to_point(
            reference_positions, decoded_positions
        )
    else:
        decoded_distances = point_to_plane(
            decoded_positions,
            reference_positions,
            vertex_normals(reference_positions, reference_triangles),
        )
        reference_distances = point_to_plane(
            reference_positions,
            decoded_positions,
            vertex_normals(decoded_positions, decoded_triangles),
        )

    return MeshComparison(
        forward=DirectionalError.summarize(decoded_distances, peak),
        backward=DirectionalError.summarize(reference_distances, peak),
        decoded_distances=decoded_distances,
        reference_distances=reference_distances,
        peak=float(peak),
        metric=metric,
    )
