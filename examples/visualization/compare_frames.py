"""Pair two sequences frame by frame and measure the error between them.

Renderer-neutral, like `render_frames`: this module decides *what* the error is
and leaves drawing to the viewer, so `--info` and `--csv` need no Qt and no
display.

    comparison = compare_sequences(reference, decoded, stride=1, order=[0, 1, 2])
    print(comparison.summary())
    frame = comparison.frames[0]
    frame.decoded_distances        # one distance per decoded vertex

Pairing is by ordinal position — frame *i* of the decoded sequence against frame
*i* of the reference — because a decoded sequence carries no reliable identifier
tying it back to a source frame. Unequal lengths pair up to the shorter one and
say so, since a codec that dropped the tail should not silently look complete.

The error scale is fixed once for the whole comparison rather than per frame.
Rescaling every frame would make a still frame prettier and the animation a lie:
colours would no longer be comparable between frames, so a frame that got worse
could look identical.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

# Import first: this puts the repository on sys.path for uninstalled clones.
import _common  # noqa: F401

import colormaps
import mesh_metrics
import render_frames
from render_frames import RenderFrame

# Fraction of all measured distances kept below the top of the colour scale. The
# largest distance in a sequence is usually one stray vertex, and scaling to it
# compresses everything real into the bottom of the ramp.
DEFAULT_PERCENTILE = 99.0


@dataclass(frozen=True)
class FrameComparison:
    """One reference frame, one decoded frame, and the error between them."""

    reference: RenderFrame
    decoded: RenderFrame
    error: mesh_metrics.MeshComparison

    @property
    def decoded_distances(self) -> np.ndarray:
        """Distance from each decoded vertex to the reference surface."""
        return self.error.decoded_distances

    @property
    def reference_distances(self) -> np.ndarray:
        """Distance from each reference vertex to the decoded surface."""
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
    """Decode both sequences into aligned render frames.

    Returns the pairs and, when the two sources disagreed on length, the two
    original counts so the caller can report the truncation.
    """
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

    `progress` is called with (done, total) after each frame, so a CLI can print
    a counter without this module knowing about one.
    """
    if metric not in ("point", "plane"):
        raise ValueError(f"metric must be 'point' or 'plane'; got {metric!r}")

    pairs, truncated = pair_frames(reference, decoded, stride, order)

    # One peak for the whole sequence, so PSNR is comparable frame to frame. Per
    # frame it would drift with the subject's own bounding box.
    peak = max(
        mesh_metrics.bounding_box_diagonal(reference_frame.positions)
        for reference_frame, _ in pairs
    )

    frames: list[FrameComparison] = []
    for done, (reference_frame, decoded_frame) in enumerate(pairs, start=1):
        error = mesh_metrics.compare_meshes(
            reference_frame.positions,
            reference_frame.triangles,
            decoded_frame.positions,
            decoded_frame.triangles,
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
    """Decide the top of the colour scale.

    Zero is returned when the sequences match exactly; `colormaps.normalize`
    treats a zero span as "all at the bottom of the ramp", which is the honest
    rendering of no error.
    """
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
    """Per-vertex diffuse term for the shared fixed light, in 0..1.

    Reuses `mesh_metrics.vertex_normals`, so an error pane and the metrics agree
    on which way the surface faces. A point cloud has no normals and comes back
    fully lit, which leaves its colours untouched by shading.
    """
    normals = mesh_metrics.vertex_normals(frame.positions, frame.triangles)
    lengths = np.linalg.norm(normals, axis=1)
    if not np.any(lengths > 0):
        return np.ones(len(frame.positions))

    direction = np.asarray(render_frames.LIGHT, dtype=np.float64)
    direction = direction / np.linalg.norm(direction)
    # abs, like render_frames.shade: reconstructed meshes are not consistently
    # wound, and a back-facing triangle should not read as zero error.
    intensity = np.abs(normals @ direction)
    return np.where(lengths > 0, intensity, 1.0)


def error_vertex_colors(
    frame: FrameComparison,
    which: str,
    clamp: float,
    shading: float = 0.25,
) -> np.ndarray:
    """RGBA per vertex, colouring one side of a pair by its distance.

    `shading` is how far the fixed light is allowed to modulate the result, 0 to
    1. It defaults low because lightness is already carrying the magnitude: at 1
    a dark patch is ambiguous between deep shadow and large error.
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
