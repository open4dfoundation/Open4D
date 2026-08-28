"""Renderer-neutral frames used by the public visualizer.

`open4d.TriangleMesh` is the right thing to load into, but a viewer wants arrays
it can hand straight to a GL buffer. `RenderFrame` is that: positions,
triangles, colors, nothing else.

Decoding here also means the load path needs no viewer library at all, so
`--info` and the Qt viewer both work without Open3D installed.

Up axis is the one thing that has to be settled before drawing. Viewers disagree
about which way is up — pyqtgraph is +Z, Open3D is +Y — so the reorderings are
kept separate and each viewer asks for the one it needs. Every entry is a cyclic
rotation, so geometry is reoriented without being mirrored.
"""

from __future__ import annotations

from collections import OrderedDict
import operator
from typing import NamedTuple

import numpy as np

# Rotations that bring the named source axis onto plot Z (pyqtgraph's up).
UP_TO_Z = {"x": [1, 2, 0], "y": [2, 0, 1], "z": [0, 1, 2]}

# Rotations that bring it onto plot Y (Open3D's up).
UP_TO_Y = {"x": [2, 0, 1], "y": [0, 1, 2], "z": [1, 2, 0]}

UP_AXES = tuple(sorted(UP_TO_Z))

# The fixed directional light both viewers shade with, in plot space. Shared so a
# reference mesh and its error map are lit identically.
LIGHT = (0.35, 0.5, 0.8)


class RenderFrame(NamedTuple):
    """One frame's geometry and identity, ready to draw."""

    positions: np.ndarray            # (N, 3) float32
    triangles: np.ndarray            # (M, 3) uint32, empty for a point cloud
    colors: np.ndarray | None        # (N, 3) float in [0, 1], or None
    frame_index: int                 # as the source numbered it
    timestamp: float                 # seconds, as the source recorded it

    @property
    def is_mesh(self) -> bool:
        return len(self.triangles) > 0


def rgb_colors(colors) -> np.ndarray | None:
    """Drop any alpha channel, leaving (N, 3) to shade with.

    Range and dtype need no work here: `TriangleMesh` stores colors as float in
    [0, 1] whether the source carried `.ply` bytes or USD floats, so the only
    thing left to settle is the channel count.
    """
    if colors is None:
        return None
    return colors[:, :3]


def to_render_frame(frame, order: list[int]) -> RenderFrame:
    """Convert one core `Frame`, reordering axes for the target viewer.

    Positions and triangles arrive in the canonical float32/uint32 storage the
    GL buffers want, so the axis rotation is the only conversion left.
    """
    mesh = frame.geometry
    positions = mesh.positions
    if order != [0, 1, 2]:
        positions = np.ascontiguousarray(positions[:, order])
    return RenderFrame(
        positions=positions,
        triangles=mesh.triangles,
        colors=rgb_colors(mesh.colors),
        # Carried through so the viewer can report the source's own numbering
        # and timing rather than its position in the strided list.
        frame_index=int(frame.frame_index),
        timestamp=float(frame.timestamp),
    )


class LazyRenderSequence:
    """Strided, bounded LRU view that converts core frames on demand."""

    def __init__(
        self,
        sequence,
        *,
        stride: int,
        order: list[int],
        cache_size: int = 3,
    ) -> None:
        if not isinstance(stride, int) or isinstance(stride, bool) or stride < 1:
            raise ValueError("stride must be a positive integer")
        if (
            not isinstance(cache_size, int)
            or isinstance(cache_size, bool)
            or cache_size < 1
        ):
            raise ValueError("cache_size must be a positive integer")
        self.sequence = sequence
        self.indices = range(0, len(sequence), stride)
        self.order = order
        self.cache_size = cache_size
        self._cache: OrderedDict[int, RenderFrame] = OrderedDict()

    def __len__(self) -> int:
        return len(self.indices)

    @property
    def cached_indices(self) -> tuple[int, ...]:
        """Displayed ordinals currently retained by the LRU cache."""
        return tuple(self._cache)

    def __getitem__(self, index: int) -> RenderFrame:
        if isinstance(index, bool):
            raise TypeError("render frame indices must be integers")
        try:
            ordinal = operator.index(index)
        except TypeError as error:
            raise TypeError("render frame indices must be integers") from error
        if ordinal < 0:
            ordinal += len(self)
        if ordinal < 0 or ordinal >= len(self):
            raise IndexError("render frame index out of range")
        cached = self._cache.pop(ordinal, None)
        if cached is None:
            cached = to_render_frame(
                self.sequence[self.indices[ordinal]], self.order
            )
        self._cache[ordinal] = cached
        while len(self._cache) > self.cache_size:
            self._cache.popitem(last=False)
        return cached

    def prefetch(self, index: int) -> None:
        """Decode one valid future frame while retaining the cache bound."""
        if 0 <= index < len(self) and index not in self._cache:
            self[index]


def decode_all(sequence, stride: int, order: list[int]) -> list[RenderFrame]:
    """Decode the frames we intend to show, up front.

    Playback has to keep up with the frame clock, so frames are converted once
    here rather than parsed off disk inside the draw loop. This is also where a
    long sequence stops being lazy — every frame drawn is held in memory.
    """
    return [to_render_frame(frame, order) for frame in sequence[::stride]]


def bounds(frames: list[RenderFrame]) -> tuple[np.ndarray, np.ndarray]:
    """Overall lower and upper corner across every frame."""
    lower = np.min([frame.positions.min(axis=0) for frame in frames], axis=0)
    upper = np.max([frame.positions.max(axis=0) for frame in frames], axis=0)
    return lower, upper


def shade(
    positions: np.ndarray,
    triangles: np.ndarray,
    base: tuple[float, float, float] = (0.95, 0.95, 0.97),
    ambient: float = 0.32,
    light: tuple[float, float, float] = LIGHT,
) -> np.ndarray:
    """Per-vertex diffuse shading, returned as RGBA in [0, 1].

    pyqtgraph's built-in `shaded` shader lights from the camera, which flattens
    the surface exactly the way a headlight does. Baking a fixed directional
    light into vertex colors instead keeps the form readable while the camera
    moves, and costs one pass over the vertices per frame.

    Face normals are accumulated onto their vertices, which smooths the result
    without needing adjacency. The light term uses `abs`, so faces stay lit
    whichever way they are wound — reconstructed meshes are not consistent.
    """
    normals = np.zeros_like(positions)
    if len(triangles):
        corners = positions[triangles]
        face_normals = np.cross(
            corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0]
        )
        # np.add.at accumulates duplicate indices; plain fancy indexing would
        # keep only the last write per vertex.
        for column in range(3):
            np.add.at(normals, triangles[:, column], face_normals)

    lengths = np.linalg.norm(normals, axis=1, keepdims=True)
    normals = normals / np.maximum(lengths, 1e-12)

    direction = np.asarray(light, dtype=np.float32)
    direction = direction / np.linalg.norm(direction)
    diffuse = np.abs((normals * direction).sum(axis=1))
    # An ambient floor keeps faces turned away from the light off black.
    intensity = ambient + (1.0 - ambient) * diffuse

    # float32 throughout: a Python tuple of floats would promote this to float64
    # and double the largest per-vertex buffer for no visible gain.
    shaded = np.clip(intensity[:, None] * np.asarray(base, dtype=np.float32),
                     0.0, 1.0)
    return np.column_stack(
        [shaded, np.ones(len(positions), dtype=np.float32)]
    ).astype(np.float32)


def vertex_colors(
    frame: RenderFrame,
    base: tuple[float, float, float] = (0.95, 0.95, 0.97),
    ambient: float = 0.32,
) -> np.ndarray:
    """RGBA per vertex: the frame's own colors if it has them, else shading."""
    if frame.colors is not None:
        return np.column_stack(
            [frame.colors, np.ones(len(frame.colors), dtype=np.float32)]
        )
    return shade(frame.positions, frame.triangles, base=base, ambient=ambient)
