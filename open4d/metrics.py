"""Nearest-vertex error for meshes and sequences in the same coordinate system.

Distances are measured at vertices, not over triangle surfaces. Results depend
on vertex density. SciPy is required only when a comparison runs.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
from typing import NamedTuple

import numpy as np

from .core import Sequence, TriangleMesh


class NearestNeighbors(NamedTuple):
    """Result of a nearest-neighbour query."""

    distances: np.ndarray  # (Q,) float64, Euclidean
    indices: np.ndarray    # (Q,) int64, into the reference positions


def nearest_neighbors(queries: np.ndarray, reference: np.ndarray) -> NearestNeighbors:
    """Exact nearest reference point for every query point, through SciPy."""
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

    try:
        from scipy.spatial import cKDTree
    except ModuleNotFoundError as error:
        if error.name != "scipy":
            raise
        raise ImportError(
            'Mesh comparisons require SciPy. Install it with pip install "open4d[metrics]".'
        ) from error
    distances, indices = cKDTree(reference).query(
        queries, k=1, workers=-1
    )
    return NearestNeighbors(
        distances=np.atleast_1d(distances),
        indices=np.atleast_1d(indices).astype(np.int64),
    )


def vertex_normals(positions: np.ndarray, triangles: np.ndarray) -> np.ndarray:
    """Area-weighted unit normals; loose or cancelling vertices get zeros."""
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

    Normals must be unit vectors. A zero normal falls back to point distance.
    """
    result = nearest_neighbors(queries, reference)
    reference_normals = np.asarray(reference_normals, dtype=np.float64)
    if reference_normals.shape != (len(reference), 3):
        raise ValueError("reference_normals must have one row per reference vertex, with 3 columns")

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
        """Summarize distances. Exact matches have infinite PSNR."""
        if len(distances) == 0:
            return cls(rms=0.0, mean=0.0, maximum=0.0, psnr_db=float("inf"))
        squared = float(np.mean(np.square(distances)))
        rms = float(np.sqrt(squared))
        return cls(
            rms=rms,
            mean=float(np.mean(distances)),
            maximum=float(np.max(distances)),
            psnr_db=_psnr(rms, peak),
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
        """Maximum error; vertex-set Hausdorff distance for metric='point'."""
        return max(self.forward.maximum, self.backward.maximum)

    @property
    def symmetric_psnr_db(self) -> float:
        """The worse (lower) PSNR of the two directions."""
        return _psnr(self.symmetric_rms, self.peak)


def _psnr(rms: float, peak: float) -> float:
    if rms == 0.0:
        return float("inf")
    if peak == 0.0:
        return float("nan")
    return float(20.0 * (np.log10(peak) - np.log10(rms)))


def _validate_options(metric: str, peak: float | None) -> None:
    if metric not in ("point", "plane"):
        raise ValueError(f"metric must be 'point' or 'plane'; got {metric!r}")
    if peak is not None and (not np.isfinite(peak) or peak < 0):
        raise ValueError("peak must be finite and nonnegative")


def bounding_box_diagonal(positions: np.ndarray) -> float:
    """Diagonal length of the axis-aligned bounding box."""
    positions = np.asarray(positions, dtype=np.float64)
    if len(positions) == 0:
        return 0.0
    return float(
        np.linalg.norm(positions.max(axis=0) - positions.min(axis=0))
    )


def compare_meshes(
    reference: TriangleMesh,
    decoded: TriangleMesh,
    *,
    metric: str = "point",
    peak: float | None = None,
) -> MeshComparison:
    """Measure one decoded mesh against one reference mesh, both directions.

    metric='point' measures nearest-vertex distances. metric='plane' projects
    those offsets onto normals computed from the receiving mesh's triangles.
    peak defaults to the reference bounding-box diagonal and only affects PSNR.
    Both meshes must contain vertices and use the same coordinates and units.
    """
    _validate_options(metric, peak)
    if not isinstance(reference, TriangleMesh) or not isinstance(decoded, TriangleMesh):
        raise TypeError("reference and decoded must be TriangleMesh objects")
    if len(reference.positions) == 0 or len(decoded.positions) == 0:
        raise ValueError("both meshes must contain at least one vertex")

    reference_positions = np.asarray(reference.positions, dtype=np.float64)
    decoded_positions = np.asarray(decoded.positions, dtype=np.float64)
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
            vertex_normals(reference_positions, reference.triangles),
        )
        reference_distances = point_to_plane(
            reference_positions,
            decoded_positions,
            vertex_normals(decoded_positions, decoded.triangles),
        )

    return MeshComparison(
        forward=DirectionalError.summarize(decoded_distances, peak),
        backward=DirectionalError.summarize(reference_distances, peak),
        decoded_distances=decoded_distances,
        reference_distances=reference_distances,
        peak=float(peak),
        metric=metric,
    )


@dataclass(frozen=True)
class SequenceComparison:
    """Per-frame errors with a shared PSNR peak. Each frame has equal weight."""

    frames: tuple[MeshComparison, ...]
    timestamps: tuple[float, ...]
    peak: float
    metric: str

    @property
    def symmetric_rms(self) -> float:
        """Square root of the mean squared per-frame symmetric RMS."""
        return float(np.sqrt(np.mean([frame.symmetric_rms ** 2 for frame in self.frames])))

    @property
    def hausdorff(self) -> float:
        """Largest error across all frames and both directions."""
        return max(frame.hausdorff for frame in self.frames)

    @property
    def symmetric_psnr_db(self) -> float:
        """PSNR from the sequence RMS and shared peak."""
        return _psnr(self.symmetric_rms, self.peak)

    @property
    def worst_frame(self) -> int:
        """Ordinal of the frame with the largest symmetric RMS; first on ties."""
        return int(np.argmax([frame.symmetric_rms for frame in self.frames]))


def compare_sequences(
    reference: Sequence,
    decoded: Sequence,
    *,
    metric: str = "point",
    peak: float | None = None,
    timestamp_tolerance: float = 1e-6,
) -> SequenceComparison:
    """Compare equal-length sequences with matching timestamps, in order.

    timestamp_tolerance is an absolute tolerance in seconds. No alignment or
    resampling is performed. By default, peak is the largest reference frame's
    bounding-box diagonal. Input sequences remain open; only errors are retained.
    """
    _validate_options(metric, peak)
    if not isinstance(reference, Sequence) or not isinstance(decoded, Sequence):
        raise TypeError("reference and decoded must be Sequence objects")
    if not np.isfinite(timestamp_tolerance) or timestamp_tolerance < 0:
        raise ValueError("timestamp_tolerance must be finite and nonnegative")
    if len(reference) != len(decoded):
        raise ValueError("sequences must have the same frame count")
    if len(reference) == 0:
        raise ValueError("both sequences must contain at least one frame")
    timestamps = reference.timestamps
    for index, (left, right) in enumerate(zip(timestamps, decoded.timestamps)):
        if abs(left - right) > timestamp_tolerance:
            raise ValueError(f"timestamps differ at frame {index}: {left} != {right}")

    frames = [
        compare_meshes(reference[index].geometry, decoded[index].geometry, metric=metric, peak=peak)
        for index in range(len(reference))
    ]
    shared_peak = float(peak) if peak is not None else max(frame.peak for frame in frames)
    # Apply one PSNR scale without decoding the reference geometry a second time.
    normalized = tuple(
        replace(
            frame,
            peak=shared_peak,
            forward=replace(frame.forward, psnr_db=_psnr(frame.forward.rms, shared_peak)),
            backward=replace(frame.backward, psnr_db=_psnr(frame.backward.rms, shared_peak)),
        )
        for frame in frames
    )
    return SequenceComparison(normalized, timestamps, shared_peak, metric)
