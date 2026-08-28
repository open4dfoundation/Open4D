"""Thin example helpers around Open4D's public sequence I/O API."""

from __future__ import annotations

from pathlib import Path

from open4d import load as _open_sequence
from open4d.codec import available_codecs
from open4d.io import available_formats, inspect_sequence

DEFAULT_FPS = 30.0
_USD_SUFFIXES = {".usd", ".usda", ".usdc", ".usdz"}
_CODEC_SUFFIXES = {
    suffix for info in available_codecs() for suffix in info.suffixes
}


def supported_formats() -> str:
    """Return concise CLI help for every public input format."""
    frame_lines = ["per-frame files and import directories:"]
    sequence_lines = ["whole-sequence files:"]
    for info in available_formats():
        extra = (
            f"  needs the [{info.dependency_extra}] extra"
            if info.dependency_extra
            else ""
        )
        target = sequence_lines if info.id == "usd" else frame_lines
        target.append(f"  {'/'.join(info.suffixes):<24}{extra}".rstrip())
    sequence_lines.append("  codec artifacts such as .o4d, .d4d, and .v4d")
    return "\n".join((*sequence_lines, *frame_lines))


def source_kind(path: Path | str) -> str:
    """Classify a source without decoding its geometry."""
    path = Path(path)
    if path.is_dir():
        return "folder"
    if not path.exists():
        raise SystemExit(f"{path} does not exist")
    if path.suffix.lower() in _USD_SUFFIXES | _CODEC_SUFFIXES:
        return "sequence-file"
    try:
        inspect_sequence(path)
    except Exception as error:
        raise SystemExit(str(error)) from None
    return "single-frame"


def open_sequence(path: Path | str, fps: float | None = None):
    """Load a source, using ``fps`` only to timestamp frame directories."""
    path = Path(path)
    return _open_sequence(path, fps=fps if path.is_dir() else None)


def describe_source(path: Path | str) -> str:
    """Describe a source through public inspection without geometry decoding."""
    path = Path(path)
    if path.suffix.lower() in _CODEC_SUFFIXES and path.is_file():
        with _open_sequence(path) as sequence:
            return (
                f"sequence file: {path.suffix.lower()} ({len(sequence)} frames), "
                f"{path.stat().st_size / 1e6:.2f} MB on disk"
            )
    info = inspect_sequence(path)
    if path.is_dir():
        megabytes = sum(
            entry.stat().st_size for entry in path.iterdir() if entry.is_file()
        ) / 1e6
    else:
        megabytes = path.stat().st_size / 1e6
    if info.storage == "container":
        return (
            f"sequence file: {path.suffix.lower()} ({info.frame_count} frames), "
            f"{megabytes:.2f} MB on disk"
        )
    if info.storage == "directory":
        return (
            f"frame directory: {info.frame_count} {info.format} frames, "
            f"{megabytes:.2f} MB on disk"
        )
    return f"single {info.format} import frame, {megabytes:.2f} MB on disk"
