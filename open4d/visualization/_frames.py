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
    texture_coordinates: np.ndarray | None = None
    opacities: np.ndarray | None = None
    representation: str = "mesh"
    # Gaussians only. Covariances are plot-space (N, 6) upper triangles
    # (xx, xy, xz, yy, yz, zz), built once here so the renderer only projects.
    covariances: np.ndarray | None = None
    # (N, K, 3) Graphdeco SH, kept only when K > 1 (degree 0 is `colors`).
    spherical_harmonics: np.ndarray | None = None
    # A view-dependent color model with `colors(directions)`, e.g. Vega's.
    appearance: object | None = None
    # (3, 3) rotation taking source axes to plot axes. SH and neural colors are
    # defined in source axes, so view directions are rotated back before use.
    source_axes: np.ndarray | None = None
    texture: object | None = None

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


# Graphdeco's degree-0 SH constant: RGB = 0.5 + SH_C0 * DC.
SH_C0 = 0.28209479177387814


class GaussianFrame(NamedTuple):
    """Frame identity for `GaussianSplats` or `NeuralGaussianFrame` values.

    Those types are not core `Geometry`, so a core `Frame` cannot hold them;
    this carries the same three fields `to_render_frame` reads.
    """

    frame_index: int
    timestamp: float
    geometry: object


def gaussian_geometry(value) -> bool:
    """Whether ``value`` is a `GaussianSplats` or `NeuralGaussianFrame`."""
    from open4d.gaussians import GaussianSplats, NeuralGaussianFrame

    return isinstance(value, (GaussianSplats, NeuralGaussianFrame))


class GaussianFrames:
    """Sequence-like view of Gaussian frames for playback and comparison.

    Wraps a nonempty list or tuple of `GaussianSplats` or `NeuralGaussianFrame`,
    or a `GaussianRun`, whose dense PLY frames are read one at a time. These
    carry no timing of their own, so frame ``i`` is stamped ``i / fps``; two
    sources played as a comparison at the same rate therefore line up.
    """

    def __init__(self, source, fps: float) -> None:
        from open4d.gaussians import GaussianRun

        if isinstance(source, GaussianRun):
            self._count = len(source.frame_paths)
            self._load = source.load_frame
        else:
            self._count = len(source)
            self._load = source.__getitem__
        self.metadata: dict = {}
        self.fps = float(fps)

    @staticmethod
    def accepts(source) -> bool:
        from open4d.gaussians import GaussianRun

        if isinstance(source, GaussianRun):
            return True
        return (isinstance(source, (list, tuple)) and bool(source)
                and all(gaussian_geometry(frame) for frame in source))

    def __len__(self) -> int:
        return self._count

    def __getitem__(self, index: int) -> GaussianFrame:
        index = range(self._count)[index]
        return GaussianFrame(index, index / self.fps, self._load(index))


def axis_matrix(order: list[int]) -> np.ndarray:
    """The (3, 3) rotation A with ``positions[:, order] == positions @ A.T``."""
    matrix = np.zeros((3, 3), dtype=np.float32)
    matrix[np.arange(3), order] = 1.0
    return matrix


def gaussian_covariances(scales, rotations, axes=None) -> np.ndarray:
    """Σ = R diag(s²) Rᵀ per Gaussian, optionally rotated to A Σ Aᵀ.

    ``scales`` are activated standard deviations and ``rotations`` unit
    ``(w, x, y, z)`` quaternions, as both Open4D Gaussian types store them.
    Returns the (N, 6) upper triangle (xx, xy, xz, yy, yz, zz) as float32.
    Rotating M = R diag(s) rather than Σ itself is the same A Σ Aᵀ, since
    (A M)(A M)ᵀ = A M Mᵀ Aᵀ, and needs one product instead of two.
    """
    s = np.asarray(scales, dtype=np.float32)
    w, x, y, z = np.asarray(rotations, dtype=np.float32).T
    rotation = np.empty((len(s), 3, 3), dtype=np.float32)
    rotation[:, 0, 0] = 1 - 2 * (y * y + z * z)
    rotation[:, 0, 1] = 2 * (x * y - w * z)
    rotation[:, 0, 2] = 2 * (x * z + w * y)
    rotation[:, 1, 0] = 2 * (x * y + w * z)
    rotation[:, 1, 1] = 1 - 2 * (x * x + z * z)
    rotation[:, 1, 2] = 2 * (y * z - w * x)
    rotation[:, 2, 0] = 2 * (x * z - w * y)
    rotation[:, 2, 1] = 2 * (y * z + w * x)
    rotation[:, 2, 2] = 1 - 2 * (x * x + y * y)
    factor = rotation * s[:, None, :]
    if axes is not None:
        factor = np.einsum("ij,njk->nik", np.asarray(axes, dtype=np.float32), factor)
    rows, columns = np.triu_indices(3)
    return np.ascontiguousarray(
        np.einsum("nik,njk->nij", factor, factor)[:, rows, columns], dtype=np.float32
    )


def _gaussian_render_frame(frame, order: list[int]) -> RenderFrame:
    gaussians = frame.geometry
    axes = axis_matrix(order)
    positions = np.asarray(gaussians.positions, dtype=np.float32)
    if order != [0, 1, 2]:
        positions = np.ascontiguousarray(positions[:, order])
    colors = getattr(gaussians, "colors", None)
    sh = getattr(gaussians, "spherical_harmonics", None)
    if sh is None:
        sh = getattr(gaussians, "attributes", {}).get("spherical_harmonics")
    if sh is not None:
        sh = np.asarray(sh, dtype=np.float32)
        if (sh.ndim != 3 or sh.shape[0] != len(positions) or sh.shape[2] != 3
                or sh.shape[1] not in (1, 4, 9, 16) or not np.isfinite(sh).all()):
            raise ValueError("Gaussian spherical_harmonics must have shape (N, 1|4|9|16, 3)")
    if sh is not None:
        # The DC term is the color a point fallback and degree-0 data show.
        colors = np.clip(0.5 + SH_C0 * sh[:, 0, :], 0.0, 1.0).astype(np.float32)
        sh = sh if sh.shape[1] > 1 else None
    return RenderFrame(
        positions=positions,
        triangles=np.empty((0, 3), dtype=np.uint32),
        colors=rgb_colors(colors),
        frame_index=int(frame.frame_index),
        timestamp=float(frame.timestamp),
        opacities=np.asarray(gaussians.opacities, dtype=np.float32),
        representation="gaussians",
        covariances=gaussian_covariances(gaussians.scales, gaussians.rotations, axes),
        spherical_harmonics=sh,
        appearance=getattr(gaussians, "appearance", None),
        source_axes=axes,
    )


def to_render_frame(frame, order: list[int]) -> RenderFrame:
    """Convert one core `Frame`, reordering axes for the target viewer.

    Positions and triangles arrive in the canonical float32/uint32 storage the
    GL buffers want, so the axis rotation is the only conversion left. Gaussian
    frames (a `GaussianCloud`, or a `GaussianFrame` holding `GaussianSplats` or
    a `NeuralGaussianFrame`) also get their covariances, rotated the same way.
    """
    mesh = frame.geometry
    if hasattr(mesh, "scales") and hasattr(mesh, "rotations"):
        return _gaussian_render_frame(frame, order)
    positions = mesh.positions
    if order != [0, 1, 2]:
        positions = np.ascontiguousarray(positions[:, order])
    return RenderFrame(
        positions=positions,
        triangles=getattr(mesh, "triangles", np.empty((0, 3), dtype=np.uint32)),
        colors=rgb_colors(getattr(mesh, "colors", None)),
        # Carried through so the viewer can report the source's own numbering
        # and timing rather than its position in the strided list.
        frame_index=int(frame.frame_index),
        timestamp=float(frame.timestamp),
        texture_coordinates=getattr(mesh, "texture_coordinates", None),
        opacities=getattr(mesh, "opacities", None),
        representation=mesh.representation.value,
        texture=getattr(mesh, "texture", None),
    )


def texture_pixels(frame):
    """Display an owned base-color map; refuse unimplemented material atlases."""
    texture = frame.texture
    if texture is None:
        return None
    if len(texture.images) != 1:
        raise ValueError("Qt texture rendering currently requires one material image")
    image = texture.images[0]
    pixels = image.pixels
    if pixels.dtype == np.uint8 and image.color_space != "linear":
        result = pixels
    else:
        scale = np.iinfo(pixels.dtype).max if pixels.dtype.kind == "u" else 1.
        values = np.clip(pixels.astype(np.float32) / scale, 0, 1)
        if image.color_space == "linear":
            color = values[:, :, :3]
            values = values.copy()
            values[:, :, :3] = np.where(color <= .0031308, 12.92 * color,
                                       1.055 * color ** (1 / 2.4) - .055)
        result = np.rint(values * 255).astype(np.uint8)
    if result.shape[2] <= 2:
        rgb = np.repeat(result[:, :, :1], 3, axis=2)
        result = np.concatenate((rgb, result[:, :, 1:2]), axis=2) if result.shape[2] == 2 else rgb
    return result


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
    frames = [frame for frame in frames if len(frame.positions)]
    if not frames:
        raise ValueError("sequence contains no vertices to display")
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
    normals = np.zeros(positions.shape, dtype=np.float64)
    if len(triangles):
        corners = np.asarray(positions, dtype=np.float64)[triangles]
        face_normals = np.cross(
            corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0]
        )
        # np.add.at accumulates duplicate indices; plain fancy indexing would
        # keep only the last write per vertex.
        for column in range(3):
            np.add.at(normals, triangles[:, column], face_normals)

    lengths = np.linalg.norm(normals, axis=1, keepdims=True)
    np.divide(normals, lengths, out=normals, where=lengths > 0)

    direction = np.asarray(light, dtype=np.float32)
    direction = direction / np.linalg.norm(direction)
    diffuse = np.abs((normals * direction).sum(axis=1))
    # An ambient floor keeps faces turned away from the light off black.
    intensity = ambient + (1.0 - ambient) * diffuse

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
        alpha = (frame.opacities if frame.opacities is not None
                 else np.ones(len(frame.colors), dtype=np.float32))
        return np.column_stack([frame.colors, alpha])
    colors = shade(frame.positions, frame.triangles, base=base, ambient=ambient)
    if frame.opacities is not None:
        colors[:, 3] = frame.opacities
    return colors
