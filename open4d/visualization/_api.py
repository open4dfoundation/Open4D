"""Public sequence visualization API."""

from __future__ import annotations

from dataclasses import dataclass
import math
from pathlib import Path

from open4d.core import Sequence

from ._frames import UP_AXES, UP_TO_Z, decode_all


@dataclass(frozen=True)
class ViewerOptions:
    """Backend-neutral controls shared by interactive and GIF rendering."""

    fps: float
    title: str = "Open4D"
    width: int = 960
    height: int = 960
    x: int | None = None
    y: int | None = None
    point_size: float = 3.0
    color: tuple[float, float, float] = (0.95, 0.95, 0.97)
    ambient: float = 0.32
    background: tuple[float, float, float] = (1.0, 1.0, 1.0)
    wireframe: bool = False
    no_metrics: bool = False
    distance: float = 1.15
    elevation: float = 14.0
    azimuth: float = -62.0


def _prepare(
    sequence: Sequence, *, stride: int, fps: float | None, up: str
):
    if not isinstance(sequence, Sequence):
        raise TypeError("sequence must be an open4d.Sequence")
    if not len(sequence):
        raise ValueError("cannot visualize an empty sequence")
    if not isinstance(stride, int) or isinstance(stride, bool) or stride < 1:
        raise ValueError("stride must be a positive integer")
    if up not in UP_AXES:
        raise ValueError(f"up must be one of {UP_AXES}")
    playback_fps = float(fps if fps is not None else sequence.fps or 30.0)
    if not math.isfinite(playback_fps) or playback_fps <= 0:
        raise ValueError("fps must be finite and greater than zero")
    return decode_all(sequence, stride, UP_TO_Z[up]), playback_fps


def _options(fps: float, values: dict) -> ViewerOptions:
    options = ViewerOptions(fps=fps, **values)
    if not isinstance(options.title, str) or not options.title:
        raise ValueError("title must be a non-empty string")
    if any(value is not None and not isinstance(value, int)
           for value in (options.x, options.y)):
        raise ValueError("x and y must be integers or None")
    if (options.x is None) != (options.y is None):
        raise ValueError("x and y must be provided together")
    if options.width < 1 or options.height < 1:
        raise ValueError("width and height must be positive")
    if options.point_size <= 0:
        raise ValueError("point_size must be greater than zero")
    if not 0 <= options.ambient <= 1:
        raise ValueError("ambient must be in [0, 1]")
    return options


def visualize(
    sequence: Sequence,
    *,
    stride: int = 1,
    fps: float | None = None,
    up: str = "z",
    **viewer_options,
) -> None:
    """Open an interactive Qt viewer for a sequence.

    PyQt6, pyqtgraph, and PyOpenGL are imported only when this function runs.
    """
    from . import _qt

    _qt.check_available()
    frames, playback_fps = _prepare(sequence, stride=stride, fps=fps, up=up)
    _qt.play(frames, _options(playback_fps, viewer_options))


def render_gif(
    sequence: Sequence,
    output: str | Path,
    *,
    stride: int = 1,
    fps: float | None = None,
    up: str = "z",
    **viewer_options,
) -> Path:
    """Render a sequence to an animated GIF and return its path."""
    path = Path(output)
    if path.suffix.lower() != ".gif":
        raise ValueError(f"output must end in .gif, got {path.suffix or 'no suffix'}")
    from . import _qt

    _qt.check_available(gif=True)
    frames, playback_fps = _prepare(sequence, stride=stride, fps=fps, up=up)
    _qt.record(frames, _options(playback_fps, viewer_options), path)
    return path
