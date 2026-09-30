"""Codec selection and public encode/decode entry points."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path

from open4d.core import Sequence
from open4d.gaussians import GaussianSplats, NeuralGaussianFrame, VEGA_CODEC
from collections.abc import Iterable

from ._klt import KLT_CODEC
from ._n4mc import N4MC_CODEC
from ._protocol import Codec, CodecError
from ._qndf import QNDF_CODEC, QNDF_INT8_CODEC
from ._tracked import TVMC_CODEC, TSMC_CODEC
from ._vmesh import FASTER_VDMC_CODEC, VDMC_CODEC


@dataclass(frozen=True)
class CodecInfo:
    id: str
    suffixes: tuple[str, ...]
    backend: str
    lossless: bool | None
    preserves: tuple[str, ...]
    representation: str = "triangle_mesh"


_CODECS: dict[str, Codec] = {
    codec.id: codec for codec in (
        KLT_CODEC, N4MC_CODEC, QNDF_CODEC, QNDF_INT8_CODEC,
        VDMC_CODEC, FASTER_VDMC_CODEC, TVMC_CODEC, TSMC_CODEC, VEGA_CODEC,
    )
}


def register_codec(codec: Codec, *, replace: bool = False) -> None:
    """Register a codec implementation by its stable identifier."""
    if not isinstance(codec, Codec):
        raise TypeError("codec must implement the Codec protocol")
    if not codec.id or not isinstance(codec.id, str):
        raise ValueError("codec.id must be a non-empty string")
    if codec.id in _CODECS and not replace:
        raise ValueError(f"codec {codec.id!r} is already registered")
    _CODECS[codec.id] = codec


def available_codecs() -> tuple[CodecInfo, ...]:
    """List research codec adapters; their optional backends are loaded on use."""
    return tuple(
        CodecInfo(
            codec.id,
            tuple(codec.suffixes),
            getattr(codec, "backend", "custom"),
            getattr(codec, "lossless", None),
            tuple(getattr(codec, "preserves", ("positions", "triangles"))),
            getattr(codec, "representation", "triangle_mesh"),
        )
        for codec in sorted(_CODECS.values(), key=lambda item: item.id)
    )


def _codec(value: str | Codec | None, path: Path) -> Codec:
    if value is not None and not isinstance(value, str):
        if not isinstance(value, Codec):
            raise TypeError("codec must be a codec id or Codec implementation")
        return value
    if isinstance(value, str):
        try:
            return _CODECS[value]
        except KeyError:
            raise ValueError(f"unknown codec {value!r}") from None
    matches = [codec for codec in _CODECS.values() if path.suffix.lower() in codec.suffixes]
    if len(matches) > 1 and path.is_file():
        detected = [
            codec for codec in matches
            if callable(getattr(codec, "can_decode", None)) and codec.can_decode(path)
        ]
        if not detected:
            raise CodecError(f"invalid Open4D artifact {path}: no known codec manifest")
        matches = detected
    if len(matches) != 1:
        raise ValueError(f"cannot infer a codec for {path}; pass codec=")
    return matches[0]


def encode_sequence(
    sequence: Sequence | Iterable[GaussianSplats] | str | os.PathLike[str],
    destination: str | Path,
    *,
    codec: str | Codec,
    input_format: str | None = None,
    fps: float | None = None,
    **options,
) -> Path:
    """Encode meshes (Sequence or path), or GaussianSplats frames with Vega."""
    if "overwrite" in options and not isinstance(options["overwrite"], bool):
        raise TypeError("overwrite must be bool")
    path = Path(destination)
    implementation = _codec(codec, path)
    if getattr(implementation, "representation", "triangle_mesh") == "gaussian_splats":
        if input_format is not None or fps is not None:
            raise TypeError("input_format and fps apply only to mesh path inputs")
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
) -> Sequence | tuple[NeuralGaussianFrame, ...]:
    """Decode a mesh Sequence or Vega frames using a named or inferred codec."""
    path = Path(source)
    return _codec(codec, path).decode(path, **options)
