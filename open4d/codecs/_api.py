"""Codec selection and public encode/decode entry points."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
from typing import TYPE_CHECKING

from open4d.core import Sequence
from open4d.gaussians import GaussianSplats, NeuralGaussianFrame
from collections.abc import Iterable

from ._protocol import Codec, CodecError
from ._attributes import AttributeSupport, attribute_support
from ._metadata import require_o4d_destination
from ._npz import REFERENCE_CODECS
from ._o4d_format import probe_codec
from open4d._plugins import entry_points

if TYPE_CHECKING:
    from open4d.native import NativeSequence


@dataclass(frozen=True)
class CodecInfo:
    id: str
    suffixes: tuple[str, ...]
    backend: str
    lossless: bool | None
    preserves: tuple[str, ...]
    representation: str = "triangle_mesh"
    attribute_support: tuple[AttributeSupport, ...] = ()


_CODECS: dict[str, Codec] = {codec.id: codec for codec in REFERENCE_CODECS}
_PLUGIN_CODECS: dict[str, Codec] = {}
_RESEARCH_IDS = frozenset((
    "klt", "n4mc", "qndf", "qndf-int8", "vdmc", "faster_vdmc",
    "tvmc", "tsmc", "vega", "queen", "3dgstream", "rerf",
))


class _PluginCodec:
    """A codec descriptor whose provider is imported only when selected."""

    suffixes = (".o4d",)
    backend = "entry-point"
    representation = "plugin"
    preserves = ()  # Capabilities are unknown until this provider is selected.

    def __init__(self, entry):
        self.id = entry.name
        self.entry = entry
        self.implementation = None

    def resolve(self):
        if self.implementation is None:
            try:
                candidate = self.entry.load()
                if isinstance(candidate, type) or not isinstance(candidate, Codec):
                    candidate = candidate()
                if not isinstance(candidate, Codec) or candidate.id != self.id:
                    raise TypeError(f"provider must return a Codec with id={self.id!r}")
                self.implementation = candidate
            except Exception as error:
                raise CodecError(
                    f"Cannot load installed codec plugin {self.id!r}: {error}. "
                    "Check its package dependencies and configured backend root."
                ) from error
        return self.implementation

    def encode(self, sequence, destination, **options):
        return self.resolve().encode(sequence, destination, **options)

    def decode(self, source, **options):
        return self.resolve().decode(source, **options)

    def can_decode(self, source):
        return probe_codec(source) == self.id


def _entry_identity(entry):
    # Standard-library and vendored EntryPoint implementations can coexist
    # after loading optional dependencies. Their equality methods may rely on
    # incompatible private APIs; compare only public registration metadata.
    value = getattr(entry, "value", None)
    return (entry.name, entry.group, value, id(entry) if value is None else None)


def _all_codecs():
    installed = entry_points("open4d.codecs")
    plugins = {}
    for identifier, entry in installed.items():
        if identifier in _CODECS:
            continue
        previous = _PLUGIN_CODECS.get(identifier)
        plugins[identifier] = (previous if previous is not None
                               and _entry_identity(previous.entry) == _entry_identity(entry)
                               else _PluginCodec(entry))
    _PLUGIN_CODECS.clear()
    _PLUGIN_CODECS.update(plugins)
    return {**_CODECS, **plugins}


def register_codec(codec: Codec, *, replace: bool = False) -> None:
    """Register a codec implementation by its stable identifier."""
    if not isinstance(codec, Codec):
        raise TypeError("codec must implement the Codec protocol")
    if not codec.id or not isinstance(codec.id, str):
        raise ValueError("codec.id must be a non-empty string")
    if codec.id in _CODECS and not replace:
        raise ValueError(f"codec {codec.id!r} is already registered")
    _CODECS[codec.id] = codec


def _describe(codec: Codec) -> CodecInfo:
    return CodecInfo(
        codec.id,
        tuple(codec.suffixes),
        getattr(codec, "backend", "custom"),
        getattr(codec, "lossless", None),
        tuple(getattr(codec, "preserves", ("positions", "triangles"))),
        getattr(codec, "representation", "triangle_mesh"),
        attribute_support(codec),
    )


def available_codecs() -> tuple[CodecInfo, ...]:
    """List NumPy codecs and installed entry points without importing plugins.

    An installed adapter can still require an external licensed runtime.
    """
    return tuple(
        _describe(codec)
        for codec in sorted(_all_codecs().values(), key=lambda item: item.id)
    )


def codec_info(identifier: str, *, resolve: bool = False) -> CodecInfo:
    """Describe one installed codec, optionally loading its selected provider.

    Discovery stays metadata-only by default. A lazy entry point has unknown
    representation (``"plugin"``); ``resolve=True`` obtains truthful capabilities
    from only that provider. This can import that plugin's optional dependencies
    and raise CodecError if they are missing. Unrelated providers are not loaded.
    """
    if not isinstance(identifier, str) or not identifier:
        raise ValueError("identifier must be a nonempty codec id")
    if not isinstance(resolve, bool):
        raise TypeError("resolve must be bool")
    implementation = _all_codecs().get(identifier)
    if implementation is None:
        # Share the selection API's actionable missing-adapter error.
        implementation = _codec(identifier, Path("."))
    if resolve and isinstance(implementation, _PluginCodec):
        implementation = implementation.resolve()
    return _describe(implementation)


def _codec(value: str | Codec | None, path: Path) -> Codec:
    if value is not None and not isinstance(value, str):
        if not isinstance(value, Codec):
            raise TypeError("codec must be a codec id or Codec implementation")
        return value
    codecs = _all_codecs()
    if isinstance(value, str):
        try:
            implementation = codecs[value]
        except KeyError:
            if value in _RESEARCH_IDS:
                raise CodecError(
                    f"Codec {value!r} requires a separately installed adapter. "
                    "From an Open4D checkout run 'pip install ./plugins/research'; "
                    "then acquire its backend under its own license and configure "
                    "the documented OPEN4D_*_ROOT variable. Research source, "
                    "weights and datasets are not included in the adapter package."
                ) from None
            raise ValueError(f"unknown codec {value!r}; inspect available_codecs()") from None
        return implementation.resolve() if isinstance(implementation, _PluginCodec) else implementation
    matches = [codec for codec in codecs.values() if path.suffix.lower() in codec.suffixes]
    if len(matches) > 1 and path.is_file():
        detected = [
            codec for codec in matches
            if callable(getattr(codec, "can_decode", None)) and codec.can_decode(path)
        ]
        if not detected:
            # can_decode is a boolean probe; surface why a damaged, archived or
            # retired-wrapper file was rejected before the generic error.
            probe_codec(path)
            raise CodecError(f"invalid Open4D artifact {path}: no known codec manifest")
        matches = detected
    if len(matches) != 1:
        raise ValueError(f"cannot infer a codec for {path}; pass codec=")
    implementation = matches[0]
    return implementation.resolve() if isinstance(implementation, _PluginCodec) else implementation


def encode_sequence(
    sequence: Sequence | Iterable[GaussianSplats] | str | os.PathLike[str],
    destination: str | Path,
    *,
    codec: str | Codec,
    input_format: str | None = None,
    fps: float | None = None,
    **options,
) -> Path:
    """Encode mesh/Gaussian data, or carry native temporal neural outputs."""
    if "overwrite" in options and not isinstance(options["overwrite"], bool):
        raise TypeError("overwrite must be bool")
    path = require_o4d_destination(destination)
    implementation = _codec(codec, path)
    if getattr(implementation, "representation", "triangle_mesh") not in ("triangle_mesh", "geometry_sequence"):
        if input_format is not None:
            raise TypeError("input_format applies only to mesh path inputs")
        if fps is not None:
            options["fps"] = fps
        return implementation.encode(sequence, path, **options)
    if isinstance(sequence, Sequence):
        if input_format is not None or fps is not None:
            raise TypeError("input_format and fps apply only to path inputs")
        return implementation.encode(sequence, path, **options)
    if not isinstance(sequence, (str, os.PathLike)):
        raise TypeError("sequence must be an open4d.Sequence or path-like source")
    from open4d.io import open_sequence

    with open_sequence(sequence, format=input_format, fps=fps) as opened:
        return implementation.encode(opened, path, **options)


def decode_sequence(
    source: str | Path, *, codec: str | Codec | None = None, **options
) -> Sequence | NativeSequence | tuple[NeuralGaussianFrame, ...]:
    """Open mesh reconstructions, native temporal state."""
    path = Path(source)
    if path.suffix.lower() == ".o4d" and not path.is_dir():
        detected = probe_codec(path)
        if detected is not None:
            selected = codec if isinstance(codec, str) else getattr(codec, "id", None)
            if selected is not None and selected != detected:
                raise CodecError(f".o4d contains {detected}, but codec={selected!r} was requested")
            if "fps" in options:
                raise TypeError("O4D stores frame timestamps; fps cannot override them")
            if detected == "frames":
                if options:
                    raise TypeError("frame delivery O4D accepts no native decoder options")
                from open4d._streamer import _require
                _require()
                from streamer.sequence import open_frames
                return open_frames(path)
            if detected not in _all_codecs() and (codec is None or codec == detected):
                from ._npz import REFERENCE_CODECS
                from .draco import DRACO_CODEC
                from ._temporal import TEMPORAL_DELTA_CODEC, TEMPORAL_PCA_CODEC
                private = {item.id: item for item in (*REFERENCE_CODECS, DRACO_CODEC, TEMPORAL_DELTA_CODEC, TEMPORAL_PCA_CODEC)}
                codec = private.get(detected, detected)
            else:
                codec = detected if codec is None else codec
        elif codec is None:
            codec = "vdmc"
    return _codec(codec, path).decode(path, **options)
