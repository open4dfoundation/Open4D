"""Unified whole-sequence entry points for Open4D."""

from __future__ import annotations

from collections.abc import Mapping
import os
from pathlib import Path

from .codec import Codec, available_codecs, decode_sequence, encode_sequence
from .core import Sequence
from .gaussians import NeuralGaussianFrame
from .io import open_sequence, write_sequence
from .native import NativeSequence, save_native

_USD_SUFFIXES = frozenset((".usd", ".usda", ".usdc", ".usdz"))
_VMESH_SUFFIX = ".vmesh"


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


def _set_raw_fps(options: dict[str, object], fps: float | None) -> None:
    if fps is None:
        return
    if "fps" in options:
        raise TypeError("fps was passed both by name and in options")
    options["fps"] = fps


def load(
    source: str | os.PathLike[str],
    *,
    format: str | None = None,
    codec: str | Codec | None = None,
    fps: float | None = None,
    options: Mapping[str, object] | None = None,
) -> Sequence | NativeSequence | tuple[NeuralGaussianFrame, ...]:
    """Open a sequence artifact, V3C .vmesh bitstream, or geometry source."""
    if format is not None and codec is not None:
        raise TypeError("format and codec are mutually exclusive")
    values = _options(options)
    path = Path(source)
    if codec is not None:
        if fps is not None and path.suffix.lower() != _VMESH_SUFFIX:
            raise TypeError("fps applies to I/O sources, not codec artifacts")
        _set_raw_fps(values, fps)
        return decode_sequence(path, codec=codec, **values)
    if not path.is_dir() and path.suffix.lower() == _VMESH_SUFFIX:
        if format is not None:
            raise TypeError("format cannot select a .vmesh bitstream")
        _set_raw_fps(values, fps)
        return decode_sequence(path, **values)
    if path.suffix.lower() in _codec_suffixes():
        if format is not None:
            raise TypeError("format cannot select a codec artifact")
        if fps is not None:
            raise TypeError("fps applies to I/O sources, not codec artifacts")
        return decode_sequence(path, **values)
    if path.suffix.lower() in (".usd", ".usda", ".usdc"):
        from .io._native_usd import is_native_usd, read_native_usd
        if is_native_usd(path):
            if fps is not None or format is not None:
                raise TypeError("native USD has its own representation and timestamps")
            return read_native_usd(path, **values)
    return open_sequence(path, format=format, fps=fps, options=values)


def save(
    sequence: Sequence | NativeSequence,
    destination: str | os.PathLike[str],
    *,
    codec: str | Codec | None = None,
    overwrite: bool = False,
    fps: float | None = None,
    up_axis: str | None = None,
    options: Mapping[str, object] | None = None,
) -> Path:
    """Write a sequence to an OpenUSD file or a research codec artifact."""
    if isinstance(sequence, NativeSequence):
        if fps is not None or up_axis is not None or options:
            raise TypeError("native repacking preserves its configuration/timeline and accepts no geometry options")
        if codec is not None and (codec if isinstance(codec, str) else codec.id) != sequence.codec:
            raise ValueError("codec does not match the native representation")
        return save_native(sequence, destination, overwrite=overwrite)
    if not isinstance(sequence, Sequence):
        raise TypeError("sequence must be an open4d.Sequence or NativeSequence")
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
        if len(matches) == 1:
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
    if not isinstance(sequence, (Sequence, NativeSequence)):
        raise TypeError("sequence must be an open4d.Sequence or NativeSequence")
    sequence.close()


def reconstruct(source, output=None, *, method="rgbd", **options):
    """Build meshes from depth images, or splats from calibrated camera images.

    RGB-D: reconstruct(depth, color=rgb, intrinsics=(fx, fy, cx, cy)).
    Gaussian: reconstruct(scene_folder, output_folder, method="queen").
    """
    if method == "rgbd":
        if output is not None:
            raise TypeError("RGB-D reconstruction returns a Sequence; omit output")
        from .streaming import reconstruct as reconstruct_rgbd

        return reconstruct_rgbd(source, **options)
    if method in ("queen", "3dgstream"):
        if output is None:
            raise TypeError("Gaussian reconstruction requires an output folder")
        from .gaussians import reconstruct_gaussians

        return reconstruct_gaussians(source, output, method=method, **options)
    raise ValueError("method must be 'rgbd', 'queen' or '3dgstream'")


def stream(source, *address, **options):
    """Send mesh frames over TCP or export a sequence for browser playback.

    A path, or browser options such as out_dir/name/rungs, selects the optional
    browser streamer. A frame iterable without browser options retains the
    original TCP behavior, including positional or keyword host/port arguments.
    Use send() to select TCP explicitly.
    """
    browser_options = {"out_dir", "name", "title", "rungs", "fps", "open_browser", "block"}
    if address or (not isinstance(source, (str, os.PathLike))
                   and not browser_options.intersection(options)):
        from .streaming import send

        return send(source, *address, **options)
    from ._streamer import stream as stream_to_browser

    return stream_to_browser(source, **options)
