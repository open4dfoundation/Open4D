"""Prepare frame comparisons and error colours without importing Qt.

The viewer pairs frames by position, reports unequal lengths, and uses one
colour scale for the sequence. The public API in open4d.metrics requires
matching lengths and timestamps.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

# Import first: this puts the repository on sys.path for uninstalled clones.
import _common  # noqa: F401

import colormaps
from open4d import TriangleMesh, metrics as mesh_metrics
from open4d.visualization import _frames as render_frames
from open4d.visualization._frames import RenderFrame

# Limit the influence of outliers on the colour scale.
DEFAULT_PERCENTILE = 99.0


@dataclass(frozen=True)
class FrameComparison:
    """One reference frame, one decoded frame, and the error between them."""

    reference: RenderFrame
    decoded: RenderFrame
    error: mesh_metrics.MeshComparison

    @property
    def decoded_distances(self) -> np.ndarray:
        return self.error.decoded_distances

    @property
    def reference_distances(self) -> np.ndarray:
        return self.error.reference_distances

    def distances_for(self, which: str) -> np.ndarray:
        if which == "decoded":
            return self.decoded_distances
        if which == "reference":
            return self.reference_distances
        raise ValueError(f"which must be 'decoded' or 'reference'; got {which!r}")

    def frame_for(self, which: str) -> RenderFrame:
        if which == "decoded":
            return self.decoded
        if which == "reference":
            return self.reference
        raise ValueError(f"which must be 'decoded' or 'reference'; got {which!r}")


@dataclass(frozen=True)
class Comparison:
    """Every paired frame, plus the scale and metric they share."""

    frames: list[FrameComparison]
    metric: str
    peak: float
    clamp: float
    percentile: float | None
    truncated_from: tuple[int, int] | None  # (reference count, decoded count)

    def __len__(self) -> int:
        return len(self.frames)

    @property
    def all_distances(self) -> np.ndarray:
        """Every measured distance, both directions, across every frame."""
        parts = [frame.decoded_distances for frame in self.frames]
        parts += [frame.reference_distances for frame in self.frames]
        return np.concatenate(parts) if parts else np.empty(0)

    def summary(self) -> "DirectionalSummary":
        """Aggregate the per-frame figures over the whole comparison."""
        return DirectionalSummary(
            symmetric_rms=float(
                np.sqrt(np.mean([f.error.symmetric_rms ** 2 for f in self.frames]))
            ),
            hausdorff=max(f.error.hausdorff for f in self.frames),
            worst_frame=int(
                np.argmax([f.error.symmetric_rms for f in self.frames])
            ),
            mean_psnr_db=float(
                np.mean([f.error.symmetric_psnr_db for f in self.frames])
            ),
        )


@dataclass(frozen=True)
class DirectionalSummary:
    """Sequence-level figures. `worst_frame` indexes into `Comparison.frames`."""

    symmetric_rms: float
    hausdorff: float
    worst_frame: int
    mean_psnr_db: float


def pair_frames(
    reference,
    decoded,
    stride: int = 1,
    order: list[int] | None = None,
) -> tuple[list[tuple[RenderFrame, RenderFrame]], tuple[int, int] | None]:
    """Pair frames by position; return original counts if lengths differ."""
    order = order or [0, 1, 2]
    if stride < 1:
        raise ValueError("stride must be at least 1")

    counts = (len(reference), len(decoded))
    shortest = min(counts)
    if shortest == 0:
        raise ValueError("both sequences must contain at least one frame")

    indices = range(0, shortest, stride)
    pairs = [
        (
            render_frames.to_render_frame(reference[index], order),
            render_frames.to_render_frame(decoded[index], order),
        )
        for index in indices
    ]
    return pairs, (counts if counts[0] != counts[1] else None)


def compare_sequences(
    reference,
    decoded,
    stride: int = 1,
    order: list[int] | None = None,
    metric: str = "point",
    max_error: float | None = None,
    percentile: float | None = DEFAULT_PERCENTILE,
    progress=None,
) -> Comparison:
    """Measure a decoded sequence against a reference sequence.

    `max_error` fixes the top of the colour scale; leave it None to take the
    `percentile` of every measured distance. Pass `percentile=None` with no
    `max_error` to scale to the true maximum.

    `progress` receives (done, total) after each frame.
    """
    if metric not in ("point", "plane"):
        raise ValueError(f"metric must be 'point' or 'plane'; got {metric!r}")

    pairs, truncated = pair_frames(reference, decoded, stride, order)

    # Use the same PSNR scale for every frame.
    peak = max(
        mesh_metrics.bounding_box_diagonal(reference_frame.positions)
        for reference_frame, _ in pairs
    )

    frames: list[FrameComparison] = []
    for done, (reference_frame, decoded_frame) in enumerate(pairs, start=1):
        error = mesh_metrics.compare_meshes(
            TriangleMesh(reference_frame.positions, reference_frame.triangles),
            TriangleMesh(decoded_frame.positions, decoded_frame.triangles),
            metric=metric,
            peak=peak,
        )
        frames.append(
            FrameComparison(
                reference=reference_frame, decoded=decoded_frame, error=error
            )
        )
        if progress is not None:
            progress(done, len(pairs))

    clamp = resolve_clamp(frames, max_error, percentile)
    return Comparison(
        frames=frames,
        metric=metric,
        peak=peak,
        clamp=clamp,
        percentile=None if max_error is not None else percentile,
        truncated_from=truncated,
    )


def resolve_clamp(
    frames: list[FrameComparison],
    max_error: float | None,
    percentile: float | None,
) -> float:
    """Return the colour scale's upper limit, or zero for an exact match."""
    if max_error is not None:
        if max_error <= 0.0:
            raise ValueError("max_error must be greater than zero")
        return float(max_error)

    distances = np.concatenate(
        [frame.decoded_distances for frame in frames]
        + [frame.reference_distances for frame in frames]
    )
    if len(distances) == 0:
        return 0.0
    if percentile is None:
        return float(np.max(distances))
    return float(np.percentile(distances, percentile))


def diffuse_intensity(frame: RenderFrame) -> np.ndarray:
    """Diffuse light in [0, 1]; vertices without normals are fully lit."""
    normals = mesh_metrics.vertex_normals(frame.positions, frame.triangles)
    lengths = np.linalg.norm(normals, axis=1)
    if not np.any(lengths > 0):
        return np.ones(len(frame.positions))

    direction = np.asarray(render_frames.LIGHT, dtype=np.float64)
    direction = direction / np.linalg.norm(direction)
    # Shade both sides because reconstructed triangle winding can vary.
    intensity = np.abs(normals @ direction)
    return np.where(lengths > 0, intensity, 1.0)


def error_vertex_colors(
    frame: FrameComparison,
    which: str,
    clamp: float,
    shading: float = 0.25,
) -> np.ndarray:
    """RGBA per vertex, colouring one side of a pair by its distance.

    `shading` controls the light contribution, from 0 to 1. A low default keeps
    shadows from obscuring the error colours.
    """
    distances = frame.distances_for(which)
    render_frame = frame.frame_for(which)
    colors = colormaps.colorize(distances, 0.0, clamp)

    shading = float(np.clip(shading, 0.0, 1.0))
    if shading > 0.0:
        intensity = (1.0 - shading) + shading * diffuse_intensity(render_frame)
        colors = np.clip(colors * intensity[:, None], 0.0, 1.0)

    return np.column_stack(
        [colors, np.ones(len(colors), dtype=np.float32)]
    ).astype(np.float32)
