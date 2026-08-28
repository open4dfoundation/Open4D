"""Unified whole-sequence entry points for Open4D."""

from __future__ import annotations

from collections.abc import Mapping
import os
from pathlib import Path

from .codec import Codec, available_codecs, decode_sequence, encode_sequence
from .core import Sequence
from .io import open_sequence, write_sequence

_USD_SUFFIXES = frozenset((".usd", ".usda", ".usdc", ".usdz"))
_RAW_VMESH_SUFFIX = ".vmesh"


def _options(value: Mapping[str, object] | None) -> dict[str, object]:
    if value is None:
        return {}
    if not isinstance(value, Mapping):
        raise TypeError("options must be a mapping or None")
    return dict(value)


def _codec_suffixes() -> dict[str, list[str]]:
    suffixes: dict[str, list[str]] = {}
    for info in available_codecs():
        for suffix in info.suffixes:
            suffixes.setdefault(suffix.lower(), []).append(info.id)
    return suffixes


def load(
    source: str | os.PathLike[str],
    *,
    format: str | None = None,
    codec: str | Codec | None = None,
    fps: float | None = None,
    options: Mapping[str, object] | None = None,
) -> Sequence:
    """Open a sequence artifact, raw V-DMC bitstream, or geometry source."""
    if format is not None and codec is not None:
        raise TypeError("format and codec are mutually exclusive")
    values = _options(options)
    path = Path(source)
    if codec is not None:
        if fps is not None and path.suffix.lower() != _RAW_VMESH_SUFFIX:
            raise TypeError("fps applies to I/O sources, not codec artifacts")
        if fps is not None:
            if "fps" in values:
                raise TypeError("fps was passed both by name and in options")
            values["fps"] = fps
        return decode_sequence(path, codec=codec, **values)
    if not path.is_dir() and path.suffix.lower() == _RAW_VMESH_SUFFIX:
        if format is not None:
            raise TypeError("format cannot select a raw V-DMC bitstream")
        if fps is not None:
            if "fps" in values:
                raise TypeError("fps was passed both by name and in options")
            values["fps"] = fps
        return decode_sequence(path, codec="vdmc", **values)
    if not path.is_dir() and path.suffix.lower() in _codec_suffixes():
        if format is not None:
            raise TypeError("format cannot select a codec artifact")
        if fps is not None:
            raise TypeError("fps applies to I/O sources, not codec artifacts")
        return decode_sequence(path, **values)
    return open_sequence(path, format=format, fps=fps, options=values)


def save(
    sequence: Sequence,
    destination: str | os.PathLike[str],
    *,
    codec: str | Codec | None = None,
    overwrite: bool = False,
    fps: float | None = None,
    up_axis: str | None = None,
    options: Mapping[str, object] | None = None,
) -> Path:
    """Write a sequence to one OpenUSD or codec artifact file."""
    if not isinstance(sequence, Sequence):
        raise TypeError("sequence must be an open4d.Sequence")
    if not isinstance(overwrite, bool):
        raise TypeError("overwrite must be bool")
    path = Path(destination)
    suffix = path.suffix.lower()
    values = _options(options)
    if "overwrite" in values:
        raise TypeError("overwrite must be passed as the named argument")

    if suffix in _USD_SUFFIXES:
        if codec is not None:
            raise TypeError("codec cannot be used with an OpenUSD destination")
        values.update(
            item
            for item in (("fps", fps), ("up_axis", up_axis))
            if item[1] is not None
        )
        return write_sequence(
            sequence, path, overwrite=overwrite, options=values
        )

    suffixes = _codec_suffixes()
    if codec is None:
        matches = suffixes.get(suffix, [])
        if suffix == ".o4d":
            codec = "npz"
        elif len(matches) == 1:
            codec = matches[0]
        elif len(matches) > 1:
            raise ValueError(
                f"ambiguous sequence-file extension {suffix!r}; pass codec="
            )
        else:
            raise ValueError(
                f"destination needs a recognized sequence-file extension; got "
                f"{suffix or 'no extension'}"
            )
    implementation_suffixes = (
        tuple(codec.suffixes)
        if not isinstance(codec, str)
        else next(
            (info.suffixes for info in available_codecs() if info.id == codec),
            (),
        )
    )
    if implementation_suffixes and suffix not in implementation_suffixes:
        raise ValueError(
            f"destination extension {suffix!r} does not match codec {codec!r}"
        )
    if fps is not None or up_axis is not None:
        raise TypeError("fps and up_axis apply only to OpenUSD destinations")
    return encode_sequence(
        sequence, path, codec=codec, overwrite=overwrite, **values
    )


def unload(sequence: Sequence) -> None:
    """Release resources owned by a loaded sequence."""
    if not isinstance(sequence, Sequence):
        raise TypeError("sequence must be an open4d.Sequence")
    sequence.close()
