"""What each rung of a mesh or point-cloud clip is worth, in geometry.

`session.Bundle.add` leaves ``quality`` empty because it knows what a rung cost
and not what it was worth, and `metrics` scores *pixels* against a captured
reference -- which a mesh clip exported from an `open4d.Sequence` does not have.
What it does have is the sequence itself, so this scores each rung's frames, as
written to disk and as the client will decode them, against it.

The arithmetic is Open4D's, not this package's: `open4d.compare_sequences`
(symmetric nearest-vertex error, PSNR against the largest reference bounding-box
diagonal) for meshes, and the same distances through `open4d.metrics` for point
clouds, which `compare_sequences` does not accept. A second implementation here
would be a second opinion about the same number.

The key is ``point_psnr``, not ``psnr``. `metrics` writes ``psnr`` for image
PSNR, and the two are different quantities on different scales; a `policy`
ladder mixing them would rank a mesh against a splat on a meaningless axis. Pass
``metric=METRIC`` to `policy.choose` and `playback.Playback` for a scored mesh
bundle.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Any

import numpy as np

from open4d.core import Frame, PointCloud, Representation, Sequence, TriangleMesh

from . import bundle

METRIC = "point_psnr"

#: What a lossless rung scores. Its PSNR is infinite, which JSON cannot carry --
#: Python writes ``Infinity`` and a browser's ``JSON.parse`` rejects the whole
#: manifest -- so it is capped, at a value no lossy rung here comes near.
LOSSLESS_DB = 100.0


class _RungFrames:
    """A rung's frame files as a `FrameProvider`, decoded on demand."""

    def __init__(self, root: Path, frames: list[str], timestamps, representation):
        self._root = root
        self._frames = list(frames)
        self.timestamps = tuple(timestamps)
        self._representation = representation

    @property
    def frame_count(self) -> int:
        return len(self._frames)

    def get_frame(self, index: int) -> Frame:
        path = self._root / self._frames[index]
        return Frame(index, self.timestamps[index], _read(path, self._representation))


def _read(path: Path, representation: Representation):
    if path.suffix == ".drc":
        import DracoPy

        decoded = DracoPy.decode(path.read_bytes())
        positions = np.asarray(decoded.points, dtype=np.float32)
        if representation is Representation.MESH:
            return TriangleMesh(positions, np.asarray(decoded.faces, dtype=np.uint32))
        return PointCloud(positions)
    import open4d

    with open4d.load(path) as sequence:
        return sequence[0].geometry


def _finite(value: float) -> float:
    return LOSSLESS_DB if math.isinf(value) else min(float(value), LOSSLESS_DB)


def fidelity(reference: Sequence, decoded: Sequence) -> dict[str, float]:
    """``point_psnr``, ``point_rms`` and ``hausdorff`` of ``decoded``."""
    from open4d import compare_sequences

    first = reference[0].geometry
    if isinstance(first, TriangleMesh):
        result = compare_sequences(reference, decoded, metric="point")
        rms, worst, peak = result.symmetric_rms, result.hausdorff, result.peak
    else:
        from open4d.metrics import bounding_box_diagonal, point_to_point

        squares, worst, peak = [], 0.0, 0.0
        for index in range(len(reference)):
            ref = np.asarray(reference[index].geometry.positions, dtype=np.float64)
            got = np.asarray(decoded[index].geometry.positions, dtype=np.float64)
            forward, backward = point_to_point(got, ref), point_to_point(ref, got)
            frame_rms = max(math.sqrt(float(np.mean(forward ** 2))),
                            math.sqrt(float(np.mean(backward ** 2))))
            squares.append(frame_rms ** 2)
            worst = max(worst, float(forward.max()), float(backward.max()))
            peak = max(peak, bounding_box_diagonal(ref))
        rms = math.sqrt(float(np.mean(squares)))
    psnr = math.inf if rms == 0 else 20 * math.log10(peak / rms) if peak else math.nan
    return {METRIC: _finite(psnr), "point_rms": float(rms), "hausdorff": float(worst)}


def score_clip(reference: Sequence, clip: bundle.Clip, root: Path | str) -> bundle.Clip:
    """Fill in ``quality`` for ``clip``'s default rendition and every variant.

    The default's goes in ``detail["quality"]``, where `policy.rungs_of` reads
    it; each variant's in its own ``quality``.
    """
    representation = Representation(clip.representation)
    if representation not in (Representation.MESH, Representation.POINTS):
        raise ValueError(
            f"{clip.name} is {clip.representation}; geometric scoring applies to "
            "meshes and point clouds. Score other clips against rendered "
            "references with streamer.metrics"
        )
    root = Path(root).expanduser().resolve()
    timestamps = reference.timestamps

    def measured(frames: list[str]) -> dict[str, float]:
        decoded = Sequence(_RungFrames(root, frames, timestamps, representation))
        return fidelity(reference, decoded)

    clip.detail["quality"] = measured(clip.frames)
    for variant in clip.variants:
        variant["quality"] = measured(variant["frames"])
    return clip
