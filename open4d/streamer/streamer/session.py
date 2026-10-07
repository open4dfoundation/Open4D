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
sizes measured off disk. `quality` is left empty unless asked for: this knows
what a rung cost, and only knows what it was worth when ``score=True`` has
`score` measure each rung against the sequence it came from.

A rung is a frame format, optionally behind one of Open4D's codecs:

* ``ply``, ``draco``, ``draco@11`` -- a mesh or point cloud as written.
* ``klt``, ``tsmc/draco@11`` -- encoded with that `open4d.encode` codec and
  decoded again here, then written in the frame format after the slash (``ply``
  when there is none). No browser decodes a ``.vmesh``, so this is how a codec's
  output reaches the client at all: its *quality* is the codec's, and its
  ``bytes`` are what is served. The codec's own bitstream size is kept in the
  variant's ``detail["codec_bytes"]``, because a chooser spending wire bytes and
  a reader comparing codecs want different numbers.
* ``ply`` or ``splat`` for Gaussians -- see `gaussians`.
"""

from __future__ import annotations

import contextlib
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence as TypingSequence

from open4d.core import Representation, Sequence

from . import bundle, export, gaussians

#: Separates a frame format from its quantisation in a rung spec: ``draco@11``.
RUNG_SEPARATOR = "@"
#: Separates an Open4D codec from the frame format it is delivered in:
#: ``tsmc/draco``.
CODEC_SEPARATOR = "/"


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
    #: The `open4d.encode` codec the frames pass through first, if any.
    codec: str | None = None
    #: For Gaussians, the fraction of each frame's Gaussians written.
    keep: float = 1.0


def _mesh_codecs() -> tuple[str, ...]:
    """Open4D codecs whose decoded output is a mesh `Sequence`."""
    import open4d

    return tuple(
        info.id for info in open4d.available_codecs()
        if info.representation == "triangle_mesh"
    )


def parse_rung(
    spec: str | Rung, representation: Representation | str = Representation.MESH
) -> Rung:
    """``"ply"``, ``"draco@11"``, ``"klt"`` or ``"tsmc/draco"`` as a `Rung`.

    Quantisation on a format that does not quantise is an error rather than an
    ignored argument: ``ply@11`` is a caller believing they asked for something
    smaller, and silently writing the same bytes at the same size would hide
    that until someone compared the rungs and found them identical.
    """
    if isinstance(spec, Rung):
        return spec
    representation = Representation(representation)
    text = str(spec).strip()
    if representation is Representation.GAUSSIANS:
        frame_format, _, share = text.partition(RUNG_SEPARATOR)
        if frame_format not in gaussians.FORMATS:
            raise ValueError(
                f"unknown Gaussian frame format {frame_format!r} in rung {spec!r}; "
                f"expected one of {', '.join(gaussians.FORMATS)}"
            )
        if not share:
            return Rung(id=text, frame_format=frame_format)
        try:
            if not share.endswith("%"):
                raise ValueError
            keep = float(share[:-1]) / 100.0
        except ValueError:
            raise ValueError(
                f"rung {spec!r} keeps {share!r}; a Gaussian rung keeps a "
                "percentage of its Gaussians, as in 'splat@25%'"
            ) from None
        if not 0.0 < keep <= 1.0:
            raise ValueError(f"rung {spec!r} must keep between 0% and 100%")
        return Rung(id=text, frame_format=frame_format, keep=keep)
    codec = None
    delivery = text
    head, slash, tail = text.partition(CODEC_SEPARATOR)
    name = head.partition(RUNG_SEPARATOR)[0].strip()
    if slash or name not in export.FORMATS:
        codec, delivery = name, (tail if slash else export.FRAME_FORMAT)
        if RUNG_SEPARATOR in head:
            raise ValueError(f"rung {spec!r} quantises the codec, not the frame format")
        known = _mesh_codecs()
        if codec not in known:
            raise ValueError(
                f"unknown frame format or codec {codec!r} in rung {spec!r}; "
                f"expected one of {', '.join(export.FORMATS)}, or an Open4D mesh "
                f"codec ({', '.join(known)}) optionally followed by "
                f"'{CODEC_SEPARATOR}<frame format>'"
            )
    frame_format, _, bits = delivery.partition(RUNG_SEPARATOR)
    frame_format = frame_format.strip()
    if frame_format not in export.FORMATS:
        raise ValueError(
            f"unknown frame format {frame_format!r} in rung {spec!r}; "
            f"expected one of {', '.join(export.FORMATS)}"
        )
    if not bits:
        return Rung(id=text, frame_format=frame_format, codec=codec)
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
        id=text, frame_format=frame_format, quantization_bits=quantization_bits,
        codec=codec,
    )


def _rendition_name(clip: str, rung: Rung) -> str:
    """Frame directory for a variant, with the spec's separators flattened."""
    flat = (rung.id.replace(RUNG_SEPARATOR, "").replace(CODEC_SEPARATOR, "-")
            .replace("%", "pct"))
    return f"{clip}-{flat}"


def _representation_of(source: Any) -> Representation:
    if isinstance(source, Sequence):
        return export.representation_of(source)
    return Representation.GAUSSIANS


def _measure(out_dir: Path, frames: Iterable[str]) -> int:
    """Total bytes of ``frames``, which are relative to the bundle root.

    Measured rather than predicted, because the files already exist -- see the
    note in `bundle.Variant`.
    """
    return sum((out_dir / frame).stat().st_size for frame in frames)


RungList = TypingSequence[str | Rung]


class _GeometryOnly:
    """Positions and triangles of another sequence's frames, read on demand."""

    def __init__(self, sequence: Sequence) -> None:
        self._sequence = sequence
        self.timestamps = tuple(sequence.timestamps)
        self.metadata = sequence.metadata

    @property
    def frame_count(self) -> int:
        return len(self._sequence)

    def get_frame(self, index: int):
        from open4d.core import Frame, PointCloud, TriangleMesh

        frame = self._sequence[index]
        geometry = frame.geometry
        if isinstance(geometry, TriangleMesh):
            stripped = TriangleMesh(geometry.positions, geometry.triangles)
        else:
            stripped = PointCloud(geometry.positions)
        return Frame(frame.frame_index, frame.timestamp, stripped, frame.metadata)


def _geometry_only(sequence: Sequence) -> Sequence:
    """``sequence`` without colours, normals, UVs or attributes.

    What a codec rung encodes. Mesh codecs here preserve positions and
    triangles and nothing else, and V-DMC's geometry-only profile refuses a
    mesh carrying UVs outright -- which made every textured capture
    unencodable, though the client never reads a UV. Per-vertex colour, which
    the client does draw, is lost too; the rung's note says so.
    """
    return Sequence(_GeometryOnly(sequence))


def _through_codec(
    sequence: Sequence, codec: str, stack: contextlib.ExitStack
) -> tuple[Sequence, int]:
    """``sequence`` encoded with an Open4D codec and decoded again, and its size.

    The artifact lives in a temporary directory held by ``stack``, because a
    decoded `Sequence` may read its frames lazily from it.
    """
    import open4d

    work = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix="streamer-")))
    artifact = open4d.encode(_geometry_only(sequence), work / f"{codec}.vmesh",
                             codec=codec)
    decoded = open4d.decode(artifact)
    if not isinstance(decoded, Sequence):
        raise TypeError(f"{codec} decoded to {type(decoded).__name__}, not a Sequence")
    stack.enter_context(decoded)
    return decoded, Path(artifact).stat().st_size


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
        source: Any,
        *,
        name: str,
        rungs: RungList | Mapping[str, RungList] = (export.FRAME_FORMAT,),
        scene: str | None = None,
        method: str | None = None,
        notes: list[str] | None = None,
        detail: dict[str, Any] | None = None,
        score: bool = False,
    ) -> bundle.Clip:
        """Write ``source`` at every rung, as one clip with variants.

        ``source`` is an `open4d.Sequence` of meshes or points, or a Gaussian
        sequence in any form `gaussians.frames_of` takes. ``rungs`` may also map
        a representation value (``"mesh"``, ``"points"``, ``"gaussians"``) to a
        list, for a caller adding a source before it knows which it is.

        The first rung is the clip's default rendition -- the one a reader that
        knows nothing about variants plays -- and the rest become `Variant`
        entries beside it. Order is the caller's: this does not sort by size,
        because which rendition should be the default is a delivery decision
        (interchange? cheapest? middle?) and not one a byte count settles.

        ``score=True`` measures every rung against ``source`` with `score`, so a
        `policy` ladder read off this bundle has something to maximise.
        """
        representation = _representation_of(source)
        if isinstance(rungs, Mapping):
            try:
                rungs = rungs[representation.value]
            except KeyError:
                raise ValueError(
                    f"{name}: no rungs given for {representation.value}"
                ) from None
        parsed = [parse_rung(rung, representation) for rung in rungs]
        if not parsed:
            raise ValueError(f"{name}: needs at least one rung")
        seen: set[str] = set()
        for rung in parsed:
            if rung.id in seen:
                raise ValueError(f"{name}: rung {rung.id!r} is listed twice")
            seen.add(rung.id)
        if score and representation is Representation.GAUSSIANS:
            raise ValueError(
                f"{name}: geometric scoring applies to meshes and point clouds; "
                "score Gaussian clips against rendered references with "
                "streamer.metrics"
            )

        if representation is Representation.GAUSSIANS:
            # Resolved once: decoding a NativeSequence runs its method's
            # runtime, and every rung would otherwise pay for it again.
            source = gaussians.frames_of(source)
        if self._infer_fps and not self.clips:
            fps = source.fps
            self.fps = fps or 30.0

        default, *alternates = parsed
        with contextlib.ExitStack() as stack:
            decoded: dict[str, tuple[Sequence, int]] = {}

            def write_rung(rung: Rung, clip_name: str, **labels: Any) -> bundle.Clip:
                if representation is Representation.GAUSSIANS:
                    return gaussians.from_frames(
                        source, self.out_dir, name=clip_name,
                        frame_format=rung.frame_format, keep=rung.keep,
                        method=method, **labels,
                    )
                frames, codec_bytes = source, None
                if rung.codec is not None:
                    if rung.codec not in decoded:
                        decoded[rung.codec] = _through_codec(source, rung.codec, stack)
                    frames, codec_bytes = decoded[rung.codec]
                clip = export.from_sequence(
                    frames, self.out_dir, name=clip_name,
                    frame_format=rung.frame_format,
                    quantization_bits=rung.quantization_bits,
                    method=method, **labels,
                )
                if rung.codec is not None:
                    clip.detail.update(codec=rung.codec, codec_bytes=codec_bytes)
                    clip.notes.append(
                        f"encoded with Open4D's {rung.codec} codec and decoded on the "
                        f"server; the browser receives {rung.frame_format} frames of "
                        f"the decoded result ({codec_bytes} bytes as a bitstream); "
                        "geometry only, so any per-vertex colour is dropped"
                    )
                return clip

            clip = write_rung(
                default, name, scene=scene, notes=notes,
                detail={**(detail or {}), "rung": default.id},
            )
            for rung in alternates:
                # A separate clip export per rung, whose frame list is then
                # folded in as a variant and whose Clip is discarded. The
                # exporters are the only things that know how to write each
                # format, and the alternative -- teaching them to write several
                # at once -- would put the rung loop inside the exporter, where a
                # caller exporting one rendition would pay for it.
                rendition = write_rung(rung, _rendition_name(name, rung), scene=scene or name)
                clip.variants.append(
                    bundle.Variant(
                        name=rung.id,
                        frames=rendition.frames,
                        bytes=_measure(self.out_dir, rendition.frames),
                        detail={
                            key: rendition.detail[key]
                            for key in ("frame_format", "quantization_bits", "keep",
                                        "codec", "codec_bytes")
                            if key in rendition.detail
                        },
                    ).as_dict()
                )
            # The default's size, beside its variants' -- where the browser's
            # ladder reads it. A variant states its bytes; the default's are
            # only on disk, which a browser cannot stat, so without this the
            # client could switch away from the default and never back.
            clip.detail["bytes_per_frame"] = (
                _measure(self.out_dir, clip.frames) / len(clip.frames)
            )
            if score:
                from . import score as scoring

                scoring.score_clip(source, clip, self.out_dir)
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

        A codec artifact such as a ``.vmesh`` always carries its own timing, and
        may load as a Gaussian sequence rather than a mesh one; both are added.
        """
        import open4d
        from open4d.io import Open4DError, inspect_sequence

        source = Path(source).expanduser().resolve()
        try:
            declared = inspect_sequence(source).timing_source
        except Open4DError:
            # Not a mesh file or frame directory: a codec artifact, whose
            # timestamps are its own. If it is not that either, `load` below
            # raises the error worth reading.
            declared = None
        loaded = open4d.load(source, fps=fps if declared == "default" else None)
        with contextlib.ExitStack() as stack:
            if hasattr(loaded, "__exit__"):
                stack.enter_context(loaded)
            return self.add(
                loaded, name=name or source.stem or source.name, **kwargs
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
