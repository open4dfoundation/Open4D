"""Collect clips into a bundle, and write its index once.

`export.from_sequence` writes one clip's frames and hands back a `Clip`; the
``view.json`` that makes them playable is a separate `bundle.write` with the
whole list. That split is right for a producer assembling clips across
processes -- which is what `bundle.add` exists for -- and wrong for the common
case of one caller and a few sequences, where forgetting the second call leaves
frames on disk that nothing will play. Nothing errors; the directory simply is
not a bundle.

This is the common case: a builder that holds the list and writes the index
when the block ends.

    with Bundle("out/", title="Capture") as clips:
        clips.add(sequence, name="capture", rungs=["draco", "draco@11"])
    server.serve("out/")

Rungs are the other half. A single encode cannot be adapted between -- a client
with one rendition has nothing to switch to -- so `add` takes a list, writes the
first as the clip's default and the rest as `bundle.Variant` entries with their
sizes measured off disk. `quality` is left empty on purpose: this knows what a
rung cost, not what it was worth, and `metrics` is what fills that in.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence as TypingSequence

from open4d.core import Sequence

from . import bundle, export

#: Separates a frame format from its quantisation in a rung spec: ``draco@11``.
RUNG_SEPARATOR = "@"


@dataclass(frozen=True)
class Rung:
    """One quality level to encode, parsed from a spec string."""

    #: What the variant is called in the manifest. The spec verbatim, so the
    #: same rung of two different clips carries the same name and a consumer
    #: can ask for "draco@11" of everything -- which is what `Variant.name`
    #: asks for and what `policy` needs to compare panes.
    id: str
    frame_format: str
    quantization_bits: int = export.DRACO_QUANTIZATION_BITS


def parse_rung(spec: str | Rung) -> Rung:
    """``"ply"``, ``"draco"`` or ``"draco@11"`` as a `Rung`.

    Quantisation on a format that does not quantise is an error rather than an
    ignored argument: ``ply@11`` is a caller believing they asked for something
    smaller, and silently writing the same bytes at the same size would hide
    that until someone compared the rungs and found them identical.
    """
    if isinstance(spec, Rung):
        return spec
    text = str(spec).strip()
    frame_format, _, bits = text.partition(RUNG_SEPARATOR)
    frame_format = frame_format.strip()
    if frame_format not in export.FORMATS:
        raise ValueError(
            f"unknown frame format {frame_format!r} in rung {spec!r}; "
            f"expected one of {', '.join(export.FORMATS)}"
        )
    if not bits:
        return Rung(id=text, frame_format=frame_format)
    if frame_format != export.DRACO_FORMAT:
        raise ValueError(
            f"rung {spec!r} sets quantisation on {frame_format!r}, which does "
            f"not quantise; only {export.DRACO_FORMAT!r} does"
        )
    try:
        quantization_bits = int(bits)
    except ValueError:
        raise ValueError(
            f"rung {spec!r} has a non-numeric quantisation {bits!r}"
        ) from None
    return Rung(
        id=text, frame_format=frame_format, quantization_bits=quantization_bits
    )


def _measure(out_dir: Path, frames: Iterable[str]) -> int:
    """Total bytes of ``frames``, which are relative to the bundle root.

    Measured rather than predicted, because the files already exist -- see the
    note in `bundle.Variant`.
    """
    return sum((out_dir / frame).stat().st_size for frame in frames)


class Bundle:
    """Clips accumulating into one bundle directory.

    Usable as a context manager, in which case the index is written on a clean
    exit and *not* written if the block raises. A half-built bundle with no
    index is a directory of frames, which is recoverable; a half-built bundle
    with an index is a manifest promising clips that are not all there, which
    a client reports as missing frames rather than as a failed export.
    """

    def __init__(
        self,
        out_dir: Path | str,
        *,
        title: str | None = None,
        source: Path | str | None = None,
        fps: float | None = None,
        scenes: dict[str, Any] | None = None,
        detail: dict[str, Any] | None = None,
    ) -> None:
        self.out_dir = Path(out_dir).expanduser().resolve()
        self.title = title or self.out_dir.name
        self.source = source
        self.fps = 30.0 if fps is None else fps
        self._infer_fps = fps is None
        self.scenes = scenes
        self.detail = detail
        self.clips: list[bundle.Clip] = []

    def __enter__(self) -> "Bundle":
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        if exc_type is None:
            self.write()

    @property
    def index_path(self) -> Path:
        return self.out_dir / bundle.INDEX_NAME

    def add_clip(self, clip: bundle.Clip) -> bundle.Clip:
        """A clip some other exporter produced, taken into this bundle."""
        self.clips.append(clip)
        return clip

    def add(
        self,
        sequence: Sequence,
        *,
        name: str,
        rungs: TypingSequence[str | Rung] = (export.FRAME_FORMAT,),
        scene: str | None = None,
        method: str | None = None,
        notes: list[str] | None = None,
        detail: dict[str, Any] | None = None,
    ) -> bundle.Clip:
        """Write ``sequence`` at every rung, as one clip with variants.

        The first rung is the clip's default rendition -- the one a reader that
        knows nothing about variants plays -- and the rest become `Variant`
        entries beside it. Order is the caller's: this does not sort by size,
        because which rendition should be the default is a delivery decision
        (interchange? cheapest? middle?) and not one a byte count settles.
        """
        if self._infer_fps and not self.clips:
            self.fps = sequence.fps or 30.0
        parsed = [parse_rung(rung) for rung in rungs]
        if not parsed:
            raise ValueError(f"{name}: needs at least one rung")
        seen: set[str] = set()
        for rung in parsed:
            if rung.id in seen:
                raise ValueError(f"{name}: rung {rung.id!r} is listed twice")
            seen.add(rung.id)

        default, *alternates = parsed
        clip = export.from_sequence(
            sequence,
            self.out_dir,
            name=name,
            frame_format=default.frame_format,
            quantization_bits=default.quantization_bits,
            scene=scene,
            method=method,
            notes=notes,
            detail={**(detail or {}), "rung": default.id},
        )
        for rung in alternates:
            # A separate clip export per rung, whose frame list is then folded
            # in as a variant and whose Clip is discarded. `from_sequence` is
            # the only thing that knows how to write each format, and the
            # alternative -- teaching it to write several at once -- would put
            # the rung loop inside the exporter, where a caller exporting one
            # rendition would pay for it.
            rendition = export.from_sequence(
                sequence,
                self.out_dir,
                name=f"{name}-{rung.id.replace(RUNG_SEPARATOR, '')}",
                frame_format=rung.frame_format,
                quantization_bits=rung.quantization_bits,
                scene=scene or name,
                method=method,
            )
            clip.variants.append(
                bundle.Variant(
                    name=rung.id,
                    frames=rendition.frames,
                    bytes=_measure(self.out_dir, rendition.frames),
                    detail={
                        "frame_format": rung.frame_format,
                        **(
                            {"quantization_bits": rung.quantization_bits}
                            if rung.frame_format == export.DRACO_FORMAT
                            else {}
                        ),
                    },
                ).as_dict()
            )
        return self.add_clip(clip)

    def add_source(
        self,
        source: Path | str,
        *,
        name: str | None = None,
        fps: float | None = None,
        **kwargs: Any,
    ) -> bundle.Clip:
        """Whatever `open4d.load` reads at ``source``, added as one clip.

        The `fps` rule is `export.from_source`'s, and deliberately the same: it
        applies only to a source carrying no timing of its own, and is ignored
        rather than rejected for one that does. Repeated here rather than
        shared because that function is the single-call path -- load, export
        and write in one -- and reaching into it for the middle third would
        make the simple case depend on the general one.
        """
        import open4d
        from open4d.io import inspect_sequence

        source = Path(source).expanduser().resolve()
        declared = inspect_sequence(source).timing_source
        with open4d.load(
            source, fps=fps if declared == "default" else None
        ) as sequence:
            return self.add(
                sequence, name=name or source.stem or source.name, **kwargs
            )

    def write(self) -> Path:
        """Write ``view.json`` for everything added so far."""
        if not self.clips:
            raise ValueError(
                f"{self.out_dir} has no clips; a bundle with an empty clip list "
                "is a page that loads and shows nothing"
            )
        return bundle.write(
            self.out_dir,
            title=self.title,
            source=str(self.source) if self.source is not None else self.out_dir.name,
            clips=self.clips,
            fps=self.fps,
            scenes=self.scenes,
            detail=self.detail,
        )
