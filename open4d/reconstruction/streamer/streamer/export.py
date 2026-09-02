"""Any `open4d.Sequence` as a bundle, so the client can play it.

This is where the two halves of the repository meet. Open4D's own sequence model
reads a dozen mesh formats and every codec artifact it ships -- `open4d.load`
resolves them all, and #47 showed the pattern by making a raw V-DMC bitstream a
playable source without adding a viewer. What was missing was a way to hand one
of those sequences to a browser: `gs_tools` exports Gaussians, and nothing
exported meshes at all, so Open4D's mesh sequences could only be seen through
the Qt window that needs a display the GPU machine does not have.

The conversion is small because the shapes already match. A `Sequence` is frames
in order; a bundle clip is frames in order plus a declared representation. The
frames themselves are written by `open4d.io.write_sequence`, which already emits
one ``frame_NNNNNN.ply`` per frame -- so this chooses the representation, records
the bounds and hands the rest to code that already existed.

Lives here rather than in `gs_tools` because a mesh has nothing to do with
Gaussian splatting, and because `streamer` already depends on `open4d` for the
representation vocabulary. The dependency direction is unchanged: this imports
Open4D's public API, and no reconstruction module.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from open4d.core import Representation, Sequence

from . import bundle

#: Frames are written in this format. PLY is Open4D's only first-party mesh
#: format needing no optional dependency, and the one the client parses.
FRAME_FORMAT = "ply"


def representation_of(sequence: Sequence) -> Representation:
    """Whether a sequence is a mesh or a point cloud, from its first frame.

    Read from the geometry rather than the file extension, because the same
    ``.ply`` carries either. A sequence whose frames disagree is not something
    this guesses at -- the first frame decides, and the bundle records it.
    """
    if not len(sequence):
        raise ValueError("sequence has no frames")
    return sequence[0].geometry.representation


def from_sequence(
    sequence: Sequence,
    out_dir: Path | str,
    *,
    name: str,
    scene: str | None = None,
    method: str | None = None,
    notes: list[str] | None = None,
    detail: dict[str, Any] | None = None,
) -> bundle.Clip:
    """Write ``sequence`` into ``out_dir`` as one clip and describe it."""
    from open4d.io import write_sequence

    out_dir = Path(out_dir).expanduser().resolve()
    representation = representation_of(sequence)
    frames_at = bundle.frame_dir(out_dir, name)
    clip_name = frames_at.name

    write_sequence(sequence, frames_at, format=FRAME_FORMAT, overwrite=True)
    written = sorted(
        str(path.relative_to(out_dir))
        for path in frames_at.glob(f"frame_*.{FRAME_FORMAT}")
    )
    if not written:
        raise ValueError(f"{name} produced no frames")

    lower = [float("inf")] * 3
    upper = [float("-inf")] * 3
    counts: list[int] = []
    for index in range(len(sequence)):
        positions = sequence[index].geometry.positions
        counts.append(int(len(positions)))
        for axis in range(3):
            lower[axis] = min(lower[axis], float(positions[:, axis].min()))
            upper[axis] = max(upper[axis], float(positions[:, axis].max()))

    return bundle.Clip(
        name=clip_name,
        representation=representation.value,
        scene=scene or clip_name,
        method=method or representation.value,
        # Carried through from the provider rather than assumed: a Sequence whose
        # frames need a key frame first says so, and the client is then able to
        # plan a seek instead of requesting a frame and hoping. Writing whole
        # frames per file makes this independent in practice today -- the field
        # is the path by which that stops being the only option.
        dependency=bundle.dependency_field(sequence.dependency),
        # Vertices per frame. The field is named for the Gaussian case that
        # needed it first; for a mesh it is the vertex count, which is the same
        # thing the viewer reports.
        counts=counts,
        frames=written,
        bounds_min=lower,
        bounds_max=upper,
        notes=notes or [],
        detail={**(detail or {}), "frame_format": FRAME_FORMAT},
    )


def from_source(
    source: Path | str,
    out_dir: Path | str,
    *,
    fps: float | None = None,
    name: str | None = None,
    scene: str | None = None,
    method: str | None = None,
) -> Path:
    """Load whatever `open4d.load` accepts at ``source`` and serve it as a bundle.

    ``fps`` applies only to a source that carries no timing of its own -- a bare
    directory of per-frame meshes. A source that does carry it, such as one
    `open4d.io.write_sequence` wrote, keeps its own timestamps and `fps` is
    ignored rather than made an error: asking to play a clip at a given rate is
    a reasonable thing to say, and refusing the whole export over it would not
    be.
    """
    import open4d
    from open4d.io import inspect_sequence

    source = Path(source).expanduser().resolve()
    out_dir = Path(out_dir).expanduser().resolve()
    declared = inspect_sequence(source).timing_source
    with open4d.load(source, fps=fps if declared == "default" else None) as sequence:
        clip = from_sequence(
            sequence,
            out_dir,
            name=name or source.stem or source.name,
            scene=scene,
            method=method,
            notes=[
                f"{len(sequence)} frames loaded from {source.name} through "
                "open4d.load — whatever format it was in",
            ],
            detail={"source": str(source)},
        )
    bundle.write(
        out_dir,
        title=f"{clip.representation} — {clip.name}",
        source=str(source),
        clips=[clip],
        fps=int(fps) if fps else 30,
    )
    return out_dir
