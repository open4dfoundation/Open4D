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

#: Interchange form: Open4D's only first-party mesh format needing no optional
#: dependency, and readable by anything.
FRAME_FORMAT = "ply"

#: Delivery form. Draco compresses this repository's mesh sequence 12.9x -- 761
#: kB a frame to 59 kB, which is 1.8 MB/s at 30 fps rather than 23 -- and the
#: client decodes it with the WASM decoder vendored under `client/vendor/draco`.
#: Lossy in two bounded ways: positions are quantised (at 14 bits the worst
#: vertex moved 0.0046% of the model's diagonal on that sequence) and duplicate
#: vertices are merged. Delivery, not archive.
DRACO_FORMAT = "draco"
FORMATS = (FRAME_FORMAT, DRACO_FORMAT)

#: Position quantisation. 14 is DracoPy's own default and the knee of the curve
#: measured here: 11 bits saves a further 20% for eight times the error.
DRACO_QUANTIZATION_BITS = 14


def representation_of(sequence: Sequence) -> Representation:
    """Whether a sequence is a mesh or a point cloud, from its first frame.

    Read from the geometry rather than the file extension, because the same
    ``.ply`` carries either. A sequence whose frames disagree is not something
    this guesses at -- the first frame decides, and the bundle records it.
    """
    if not len(sequence):
        raise ValueError("sequence has no frames")
    return sequence[0].geometry.representation


def _write_draco(sequence: Sequence, frames_at: Path, bits: int) -> list[Path]:
    """One ``.drc`` per frame, encoded with the same Draco this repository vendors.

    Per frame because a streaming client fetches frames. DracoPy is an optional
    dependency of this exporter, independent of the public codec registry.
    """
    try:
        import DracoPy
    except ImportError as error:  # pragma: no cover - depends on the environment
        raise RuntimeError(
            "Draco frames need the DracoPy binding: pip install 'DracoPy'"
        ) from error

    written: list[Path] = []
    for index in range(len(sequence)):
        geometry = sequence[index].geometry
        triangles = getattr(geometry, "triangles", None)
        payload = DracoPy.encode(
            geometry.positions.astype("float32"),
            None if triangles is None else triangles.astype("uint32"),
            quantization_bits=bits,
        )
        target = frames_at / f"frame_{index:06d}.drc"
        target.write_bytes(payload)
        written.append(target)
    return written


def from_sequence(
    sequence: Sequence,
    out_dir: Path | str,
    *,
    name: str,
    frame_format: str = FRAME_FORMAT,
    quantization_bits: int = DRACO_QUANTIZATION_BITS,
    scene: str | None = None,
    method: str | None = None,
    notes: list[str] | None = None,
    detail: dict[str, Any] | None = None,
) -> bundle.Clip:
    """Write ``sequence`` into ``out_dir`` as one clip and describe it.

    ``frame_format`` is ``"ply"`` for the interchange form or ``"draco"`` for the
    compressed one; see :data:`DRACO_FORMAT` for what the second costs.
    """
    from open4d.io import write_sequence

    if frame_format not in FORMATS:
        raise ValueError(
            f"unknown frame format {frame_format!r}; expected one of "
            + ", ".join(FORMATS)
        )
    out_dir = Path(out_dir).expanduser().resolve()
    representation = representation_of(sequence)
    frames_at = bundle.frame_dir(out_dir, name)
    clip_name = frames_at.name

    if frame_format == DRACO_FORMAT:
        paths = _write_draco(sequence, frames_at, quantization_bits)
        written = sorted(str(path.relative_to(out_dir)) for path in paths)
    else:
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
        notes=(notes or []) + ([
            f"frames are Draco at {quantization_bits}-bit position quantisation: "
            "a delivery form, decoded in the browser, lossy in position and in "
            "merging duplicate vertices",
        ] if frame_format == DRACO_FORMAT else []),
        detail={
            **(detail or {}),
            "frame_format": frame_format,
            **({"quantization_bits": quantization_bits}
               if frame_format == DRACO_FORMAT else {}),
        },
    )


def from_source(
    source: Path | str,
    out_dir: Path | str,
    *,
    fps: float | None = None,
    name: str | None = None,
    frame_format: str = FRAME_FORMAT,
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
        playback_fps = sequence.fps or 30.0
        clip = from_sequence(
            sequence,
            out_dir,
            name=name or source.stem or source.name,
            frame_format=frame_format,
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
        fps=playback_fps,
    )
    return out_dir
