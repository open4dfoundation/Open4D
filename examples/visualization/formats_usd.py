"""Compatibility wrappers around Open4D's public OpenUSD sequence backend."""

from __future__ import annotations

from pathlib import Path
from typing import Iterable

from open4d import Frame, MemoryFrameProvider, Sequence
from open4d.io import _usd

SUFFIXES = _usd.USD_SUFFIXES
CONTAINER_VERSION = 1
PRIM_PATH = _usd.PRIM_PATH
UsdSequenceProvider = _usd.UsdSequenceProvider


def open_usd_sequence(path: Path, fps: float | None = None) -> Sequence:
    """Open one time-sampled USD file through the public backend."""
    return _usd.open_usd_sequence(path, fps=fps)


def read_usd_frame(path: Path):
    """Read the first frame of a USD sequence for legacy example callers."""
    with open_usd_sequence(path) as sequence:
        mesh = sequence[0].geometry
        return mesh.positions, mesh.triangles, mesh.colors


def read_container_metadata(path: Path) -> dict:
    """Return sequence declarations without decoding geometry."""
    provider = UsdSequenceProvider(path)
    try:
        return {
            **provider._manifest,
            "frame_count": provider.frame_count,
            "fps": provider.metadata.get("fps"),
        }
    finally:
        provider.close()


def write_usd_container(
    path: Path,
    frames: Iterable[Frame],
    fps: float = 30.0,
    up_axis: str = "z",
    source: str | None = None,
    source_format: str | None = None,
    generator: str | None = None,
) -> Path:
    """Write frames through the public OpenUSD sequence writer."""
    if isinstance(frames, Sequence):
        sequence = frames
    else:
        sequence = Sequence(MemoryFrameProvider(
            tuple(frames),
            metadata={
                "source": source or "",
                "source_format": source_format or "",
                "generator": generator or "examples/visualization/formats_usd.py",
            },
        ))
    return _usd.write_usd_sequence(
        sequence,
        path,
        overwrite=True,
        fps=fps,
        up_axis=up_axis,
    )
