"""Owned texture images and per-face materials for triangle meshes.

A texture is part of the mesh value, not a path to a file next to it: a codec
or transport that carries a mesh must either carry its pixels or reject it, and
a file reference would let a texture silently go missing between the two. The
pixels are therefore held as arrays, with the few facts needed to sample them
the same way everywhere -- their color space and how lookups wrap and filter.

Orientation follows OBJ and OpenGL. Pixel row 0 is the *top* of the image, and
texture coordinate ``(0, 0)`` addresses its *bottom-left* corner, so ``v = 1``
is row 0. Readers and writers convert to this convention at the file boundary.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from numpy.typing import NDArray

from . import dtypes

COLOR_SPACES = ("srgb", "linear", "raw")
WRAP_MODES = ("repeat", "clamp", "mirror")
FILTERS = ("linear", "nearest")
PIXEL_DTYPES = (np.dtype(np.uint8), np.dtype(np.uint16), np.dtype(np.float32))


def _choice(value: str, allowed: tuple[str, ...], name: str) -> str:
    if not isinstance(value, str) or value not in allowed:
        raise ValueError(f"{name} must be one of {', '.join(allowed)}; got {value!r}")
    return value


@dataclass(frozen=True, eq=False)
class TextureImage:
    """One texture map with the metadata needed to sample it.

    ``pixels`` has shape ``(height, width, channels)`` with one to four
    channels; a 2-D array is read as one channel. Storage is uint8, uint16 or
    float32 (float64 is narrowed, other integers are rejected rather than
    rescaled). ``color_space`` is ``"srgb"`` for display-referred color,
    ``"linear"`` for scene-linear color and ``"raw"`` for data maps such as
    normals or labels. ``wrap`` and ``filter`` describe sampling.
    """

    pixels: NDArray
    color_space: str = "srgb"
    wrap: str = "repeat"
    filter: str = "linear"
    name: str | None = None

    def __post_init__(self) -> None:
        pixels = np.asarray(self.pixels)
        if pixels.ndim == 2:
            pixels = pixels[:, :, None]
        if pixels.ndim != 3 or not 1 <= pixels.shape[2] <= 4:
            raise ValueError(
                f"texture pixels must have shape (H, W, 1..4); got {pixels.shape}"
            )
        if pixels.shape[0] < 1 or pixels.shape[1] < 1:
            raise ValueError("texture pixels must have a nonzero width and height")
        if pixels.dtype == np.float64:
            if not np.isfinite(pixels).all():
                raise ValueError("texture pixels must contain only finite values")
            pixels = dtypes._cast(pixels, np.dtype(np.float32), "texture pixels")
        if pixels.dtype not in PIXEL_DTYPES:
            raise TypeError(
                "texture pixels must be uint8, uint16 or float32; "
                f"got {pixels.dtype}"
            )
        if pixels.dtype == np.float32 and not np.isfinite(pixels).all():
            raise ValueError("texture pixels must contain only finite values")
        if self.name is not None and (not isinstance(self.name, str) or not self.name):
            raise ValueError("texture name must be a non-empty string or None")
        object.__setattr__(self, "pixels", pixels)
        object.__setattr__(self, "color_space", _choice(self.color_space, COLOR_SPACES, "color_space"))
        object.__setattr__(self, "wrap", _choice(self.wrap, WRAP_MODES, "wrap"))
        object.__setattr__(self, "filter", _choice(self.filter, FILTERS, "filter"))

    @property
    def height(self) -> int:
        return int(self.pixels.shape[0])

    @property
    def width(self) -> int:
        return int(self.pixels.shape[1])

    @property
    def channels(self) -> int:
        return int(self.pixels.shape[2])


@dataclass(frozen=True, eq=False)
class MeshTexture:
    """Base-color texture images and the material each triangle samples.

    ``images[k]`` is material ``k``'s map. ``material_ids`` holds one int32
    material index per triangle (face domain); it may be omitted only when
    there is a single image. ``material_names`` default to ``material_<k>``.
    The mesh validates ``material_ids`` against its triangle count and requires
    texture coordinates alongside a texture.
    """

    images: tuple[TextureImage, ...]
    material_ids: NDArray | None = None
    material_names: tuple[str, ...] | None = None

    def __post_init__(self) -> None:
        images = self.images
        if isinstance(images, TextureImage):
            images = (images,)
        images = tuple(images)
        if not images or not all(isinstance(image, TextureImage) for image in images):
            raise TypeError("images must be a non-empty sequence of TextureImage")
        material_ids = self.material_ids
        if material_ids is not None:
            values = np.asarray(material_ids)
            if values.ndim != 1:
                raise ValueError("material_ids must be one index per triangle")
            if not np.issubdtype(values.dtype, np.integer):
                raise TypeError("material_ids must have an integer dtype")
            if values.size and (int(values.min()) < 0 or int(values.max()) >= len(images)):
                raise ValueError(f"material_ids must index the {len(images)} texture images")
            material_ids = values.astype(dtypes.ATTRIBUTE_INT_DTYPE, copy=False)
        elif len(images) != 1:
            raise ValueError("material_ids are required when a texture has several images")
        names = self.material_names
        if names is None:
            names = tuple(f"material_{index}" for index in range(len(images)))
        names = tuple(names)
        if (len(names) != len(images) or len(set(names)) != len(names)
                or not all(isinstance(name, str) and name and name not in (".", "..")
                           and not any(c.isspace() or c in "/\\:#" for c in name)
                           for name in names)):
            raise ValueError("material_names must be unique non-empty names without whitespace, one per image")
        object.__setattr__(self, "images", images)
        object.__setattr__(self, "material_ids", material_ids)
        object.__setattr__(self, "material_names", names)

    def face_materials(self, triangle_count: int) -> NDArray:
        """Return the per-triangle material index, expanding a single material."""
        if self.material_ids is None:
            return np.zeros(triangle_count, dtype=dtypes.ATTRIBUTE_INT_DTYPE)
        return self.material_ids


def _texture(value: MeshTexture | TextureImage | None) -> MeshTexture | None:
    if value is None or isinstance(value, MeshTexture):
        return value
    if isinstance(value, TextureImage):
        return MeshTexture((value,))
    raise TypeError("texture must be a MeshTexture, TextureImage or None")
