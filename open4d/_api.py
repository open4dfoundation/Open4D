"""Unified whole-sequence entry points for Open4D."""

from __future__ import annotations

from collections.abc import Mapping
import os
from pathlib import Path
import warnings

from .codec import Codec, available_codecs, decode_sequence, encode_sequence
from .core import Sequence
from .gaussians import NeuralGaussianFrame
from .io import open_sequence, write_sequence
from .native import NativeSequence, save_native

_USD_SUFFIXES = frozenset((".usd", ".usda", ".usdc", ".usdz"))
_O4D_SUFFIX = ".o4d"


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
    """Open O4D, native V-DMC input, or a geometry source."""
    if format is not None and codec is not None:
        raise TypeError("format and codec are mutually exclusive")
    values = _options(options)
    path = Path(source)
    if codec is not None:
        if fps is not None and path.suffix.lower() != _O4D_SUFFIX:
            raise TypeError("fps applies to I/O sources, not codec artifacts")
        _set_raw_fps(values, fps)
        return decode_sequence(path, codec=codec, **values)
    if not path.is_dir() and path.suffix.lower() == _O4D_SUFFIX:
        if format is not None:
            raise TypeError("format cannot select a .o4d bitstream")
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


def reconstruct(source, output=None, *, method="rgbd", codec=None,
                overwrite=False, **options):
    """Build meshes from depth images, or splats from calibrated camera images.

    RGB-D: reconstruct(depth, rgb, intrinsics=(fx, fy, cx, cy)); rgb is optional.
    A load_rgbd_capture() capture supplies images, cameras and timestamps.
    Gaussian: reconstruct(scene_folder, output_folder, method="queen"); the
    scene can also be an ORBIT folder or load_orbit() scene, with frames=,
    max_width=, test_views=, initial_points= and depth_priors=.

    RGB-D without a destination returns a Sequence. A destination ending in
    .o4d encodes the reconstruction (NumPy by default); a .usdc destination
    exports meshes with the optional OpenUSD dependency. ``tsdf`` aliases rgbd.
    Plugin reconstruction with ``codec=`` trains into a temporary folder and
    encodes its result into .o4d. Neural training remains backend-specific.
    """
    if not isinstance(overwrite, bool):
        raise TypeError("overwrite must be bool")
    if method in ("rgbd", "tsdf"):
        destination = Path(output) if isinstance(output, (str, os.PathLike)) else None
        if destination is not None:
            if destination.suffix.lower() not in {".o4d", *_USD_SUFFIXES}:
                raise ValueError("RGB-D output must end in .o4d or an OpenUSD extension")
            if destination.exists() and not overwrite:
                raise FileExistsError(destination)
            if destination.suffix.lower() != ".o4d" and codec is not None:
                raise TypeError("codec applies only to a .o4d destination")
            if destination.suffix.lower() == ".o4d":
                from .codec._api import _codec
                _codec(codec or "npz", destination)
            else:
                from .io._usd import _pxr
                _pxr()
            output = None
        elif codec is not None or overwrite:
            raise TypeError("codec and overwrite require an output path")
        from .reconstruction.rgbd import RGBDCapture, reconstruct as reconstruct_rgbd

        if isinstance(source, RGBDCapture):
            given = [name for name in ("color", "intrinsics", "camera_poses", "timestamps",
                                       "sync_error_us")
                     if name in options] + (["rgb"] if output is not None else [])
            if given:
                raise TypeError(f"{', '.join(given)} come from the RGBDCapture; omit them")
            if source.timestamps is not None and "fps" in options:
                raise TypeError("the RGBDCapture has timestamps; omit fps")
            if "depth_scale" in options and options["depth_scale"] != source.depth_scale:
                raise ValueError(f"depth_scale={options['depth_scale']!r} conflicts with the "
                                 f"RGBDCapture's {source.depth_scale:g} depth units per metre")
            if (source.sync_error_us is None and source.depth.shape[1] > 1
                    and options.get("sync_policy", "error") != "off"):
                from .reconstruction.rgbd import SyncWarning

                warnings.warn("the RGBDCapture records no camera synchronization, so frame "
                              "timing across cameras is unknown; metadata['sync'] marks it "
                              "(pass sync_policy='off' to accept unsynchronized input)",
                              SyncWarning, stacklevel=2)
            extra = options.get("metadata") or {}
            if not isinstance(extra, Mapping):
                raise TypeError("metadata must be a mapping or None")
            capture = {**source.provenance, "frame_numbers": list(source.pair_numbers),
                       "serials": list(source.serials)}
            if source.camera_timestamps_us is not None:
                capture["camera_timestamps_us"] = [list(stamps)
                                                   for stamps in source.camera_timestamps_us]
            options.update(color=source.color, intrinsics=source.intrinsics,
                           camera_poses=source.camera_poses, timestamps=source.timestamps,
                           sync_error_us=source.sync_error_us, depth_scale=source.depth_scale,
                           metadata={**extra, "capture": capture})
            source = source.depth
        elif output is not None:
            if "color" in options:
                raise TypeError("color was given both positionally and by keyword")
            options["color"] = output
        sequence = reconstruct_rgbd(source, **options)
        if destination is None:
            return sequence
        try:
            selected = (codec or "npz") if destination.suffix.lower() == ".o4d" else None
            return save(sequence, destination, codec=selected, overwrite=overwrite)
        finally:
            sequence.close()
    from .reconstruction._registry import get_method

    implementation = get_method(method)
    if codec is None:
        if overwrite:
            raise TypeError("overwrite requires codec= and a .o4d destination for plugin reconstruction")
        return implementation(source, output, **options)
    if not isinstance(output, (str, os.PathLike)) or Path(output).suffix.lower() != ".o4d":
        raise TypeError("codec= requires a .o4d output path")
    destination = Path(output)
    if destination.exists() and not overwrite:
        raise FileExistsError(destination)
    from tempfile import TemporaryDirectory

    with TemporaryDirectory(prefix="open4d-reconstruct-") as temporary:
        reconstructed = implementation(source, Path(temporary) / "run", **options)
        # Preserve a GaussianRun's configuration and initial-model context;
        # the native adapter accepts the run itself, not just its directory.
        try:
            return encode_sequence(reconstructed, destination, codec=codec, overwrite=overwrite)
        finally:
            if isinstance(reconstructed, (Sequence, NativeSequence)):
                reconstructed.close()


def stream(source, *address, **options):
    """Send mesh frames over TCP or export a sequence for browser playback.

    A path, a Gaussian sequence, or browser options such as out_dir/name/rungs
    selects the optional browser streamer. A frame iterable without browser
    options retains the original TCP behavior, including positional or keyword
    host/port arguments. Use send() to select TCP explicitly.
    """
    from ._streamer import browser_only

    browser_options = {"out_dir", "name", "title", "rungs", "fps", "score", "link",
                       "monitor", "open_browser", "block"}
    if address or (not browser_only(source)
                   and not browser_options.intersection(options)):
        from .transport import send

        return send(source, *address, **options)
    from ._streamer import stream as stream_to_browser

    return stream_to_browser(source, **options)
