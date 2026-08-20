"""Codec selection and public encode/decode entry points."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path

from open4d.core import Sequence

from ._draco import DRACO_CODEC
from ._npz import REFERENCE_CODECS
from ._protocol import Codec, CodecError
from ._vmesh import FASTER_VDMC_CODEC, VDMC_CODEC


@dataclass(frozen=True)
class CodecInfo:
    id: str
    suffixes: tuple[str, ...]
    backend: str
    lossless: bool | None
    preserves: tuple[str, ...]


_CODECS: dict[str, Codec] = {
    codec.id: codec
    for codec in (*REFERENCE_CODECS, DRACO_CODEC, VDMC_CODEC, FASTER_VDMC_CODEC)
}


def register_codec(codec: Codec, *, replace: bool = False) -> None:
    """Register an in-process codec implementation by its stable identifier."""
    if not isinstance(codec, Codec):
        raise TypeError("codec must implement the Codec protocol")
    if not codec.id or not isinstance(codec.id, str):
        raise ValueError("codec.id must be a non-empty string")
    if codec.id in _CODECS and not replace:
        raise ValueError(f"codec {codec.id!r} is already registered")
    _CODECS[codec.id] = codec


def available_codecs() -> tuple[CodecInfo, ...]:
    return tuple(
        CodecInfo(
            codec.id,
            tuple(codec.suffixes),
            getattr(codec, "backend", "custom"),
            getattr(codec, "lossless", None),
            tuple(getattr(codec, "preserves", ("positions", "triangles"))),
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
    matches = [codec for codec in _CODECS.values() if path.suffix in codec.suffixes]
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
    sequence: Sequence | str | os.PathLike[str],
    destination: str | Path,
    *,
    codec: str | Codec = "npz",
    input_format: str | None = None,
    fps: float | None = None,
    **options,
) -> Path:
    """Encode a canonical sequence or any path supported by :mod:`open4d.io`."""
    path = Path(destination)
    implementation = _codec(codec, path)
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
) -> Sequence:
    """Decode a sequence using a named, inferred, or caller-supplied codec."""
    path = Path(source)
    return _codec(codec, path).decode(path, **options)
