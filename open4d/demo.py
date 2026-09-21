"""Generate a looping mesh wave using NumPy.

Grid adapted from ``scripts/benchmark_codec.py``. Code and output are MIT licensed.
"""

from __future__ import annotations

import math
from numbers import Real
import operator
from pathlib import Path
import tempfile

import numpy as np

from ._files import publish_directory
from .core import Frame, Sequence, TopologyMode, TriangleMesh


_LICENSE = """MIT License

Copyright (c) 2026 SINRG Lab

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""


def _integer(value: int, name: str, minimum: int) -> int:
    if isinstance(value, bool):
        raise TypeError(f"{name} must be an integer")
    try:
        result = operator.index(value)
    except TypeError as error:
        raise TypeError(f"{name} must be an integer") from error
    if result < minimum:
        raise ValueError(f"{name} must be at least {minimum}")
    return result


class _WaveProvider:
    topology = TopologyMode.FIXED
    has_constant_vertex_count = True
    has_vertex_correspondence = True

    def __init__(self, side: int, frames: int, fps: float) -> None:
        self.frame_count = frames
        self.fps = fps
        axis = np.linspace(-1, 1, side, dtype=np.float32)
        x, y = np.meshgrid(axis, axis)
        self.x, self.y = x.ravel(), y.ravel()
        cells = np.arange((side - 1) ** 2, dtype=np.uint32)
        row, column = np.divmod(cells, side - 1)
        corner = row * side + column
        self.triangles = np.column_stack((
            corner, corner + 1, corner + side,
            corner + 1, corner + side + 1, corner + side,
        )).reshape(-1, 3)
        self.triangles.setflags(write=False)
        self.metadata = {
            "name": "Open4D wave",
            "source": "Procedurally generated; no captured data or external assets",
            "generator": "open4d.demo.mesh_sequence/v1",
            "generator_parameters": {"side": side, "frames": frames, "fps": fps},
            "copyright": "Copyright (c) 2026 SINRG Lab",
            "license": "MIT",
            "up_axis": "z",
            "fps": fps,
        }

    @property
    def timestamps(self) -> tuple[float, ...]:
        return tuple(index / self.fps for index in range(self.frame_count))

    def get_frame(self, index: int) -> Frame:
        phase = 2 * math.pi * index / self.frame_count
        z = 0.15 * np.sin(3 * self.x + phase) * np.cos(1.5 * self.y)
        positions = np.column_stack((self.x, self.y, z)).astype(np.float32)
        return Frame(index, index / self.fps, TriangleMesh(positions, self.triangles))


def mesh_sequence(*, side: int = 24, frames: int = 60, fps: float = 30.0) -> Sequence:
    """Return one wave cycle with fixed vertex order and triangle connectivity.

    ``side`` sets the number of vertices per grid edge (at least 2).
    Frames contain positions and triangles, generated on access.
    Timestamps are spaced ``1 / fps`` seconds apart.
    """
    side = _integer(side, "side", 2)
    frames = _integer(frames, "frames", 1)
    if not isinstance(fps, Real) or isinstance(fps, bool):
        raise TypeError("fps must be a real number")
    fps = float(fps)
    if not math.isfinite(fps) or fps <= 0:
        raise ValueError("fps must be finite and greater than zero")
    return Sequence(_WaveProvider(side, frames, fps))


def write_demo(
    destination: str | Path, *, side: int = 24, frames: int = 60, fps: float = 30.0,
) -> Path:
    """Write PLY frames, timing, source notes, and the MIT license to a new folder.

    Refuses existing destinations and removes partial output if writing fails.
    """
    from .io import write_sequence

    destination = Path(destination).expanduser().absolute()
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(f"destination already exists: {destination}; choose a new folder")
    with mesh_sequence(side=side, frames=frames, fps=fps) as sequence:
        parameters = sequence.metadata["generator_parameters"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".open4d-demo-", dir=destination.parent) as work:
            generated = write_sequence(sequence, Path(work) / "frames", format="ply")
            (generated / "LICENSE").write_text(_LICENSE, encoding="utf-8")
            (generated / "README.md").write_text(
                "# Open4D wave sample\n\n"
                f"{frames} frames at {float(fps):g} fps, {side * side} vertices and "
                f"{2 * (side - 1) ** 2} triangles per frame. Up axis: z.\n\n"
                "Generated from a grid and sine wave, with no captured data or external assets.\n"
                "Copyright (c) 2026 SINRG Lab. MIT license; see LICENSE.\n"
                "Keep LICENSE and open4d.sequence.json with the frames when sharing.\n\n"
                "Generate another copy:\n\n```bash\n"
                f"open4d demo another-wave --side {parameters['side']} "
                f"--frames {parameters['frames']} --fps {parameters['fps']}\n```\n\n"
                "From this folder, run `open4d inspect .` or `open4d view .`.\n"
                "The viewer requires `open4d[player]` and a display with OpenGL.\n",
                encoding="utf-8",
            )
            if destination.exists() or destination.is_symlink():
                raise FileExistsError(f"destination already exists: {destination}")
            publish_directory(generated, destination)
    return destination
