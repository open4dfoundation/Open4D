"""NumPy-backed geometry values shared by Open4D pipelines."""

from __future__ import annotations

from dataclasses import dataclass, field
from types import MappingProxyType
from typing import Mapping

import numpy as np
from numpy.typing import ArrayLike, NDArray

from . import dtypes

_RESERVED_ATTRIBUTES = {
    "positions",
    "triangles",
    "colors",
    "normals",
    "texture_coordinates",
}


def _array(value: ArrayLike, name: str) -> NDArray:
    result = np.asarray(value)
    if not (np.issubdtype(result.dtype, np.number) or result.dtype == np.bool_):
        raise TypeError(f"{name} must have a numeric or boolean dtype")
    return result


def _finite(array: NDArray, name: str) -> None:
    if not np.isfinite(array).all():
        raise ValueError(f"{name} must contain only finite values")


@dataclass(frozen=True, eq=False)
class TriangleMesh:
    """A validated triangle mesh held in Open4D's canonical dtypes.

    Arrays are coerced on construction — float32 positions, normals and texture
    coordinates, uint32 triangle indices, and colors as float in [0, 1] whether
    they arrived as bytes or floats. See `open4d.core.dtypes` for the full canon
    and why it exists. A consumer can therefore read any field without asking
    what produced it.

    Instances are structurally immutable: fields and the attribute mapping cannot
    be replaced. Array buffers are not made read-only. An array that already
    matches the canon is stored as-is and stays shared with the caller, so
    mutating that buffer mutates the mesh; one that had to be converted is a
    fresh array and does not. Callers needing a value snapshot should pass
    copies rather than depend on which case they are in.
    """

    positions: NDArray
    triangles: NDArray
    colors: NDArray | None = None
    normals: NDArray | None = None
    texture_coordinates: NDArray | None = None
    attributes: Mapping[str, NDArray] = field(default_factory=dict)

    def __post_init__(self) -> None:
        positions = _array(self.positions, "positions")
        if positions.ndim != 2 or positions.shape[1:] != (3,):
            raise ValueError(
                f"positions must have shape (N, 3); got {positions.shape}"
            )
        # Finiteness is checked on the values as supplied; the cast that follows
        # reports separately if they will not fit the canonical dtype.
        _finite(positions, "positions")
        positions = dtypes.as_positions(positions)

        triangles = _array(self.triangles, "triangles")
        if triangles.ndim != 2 or triangles.shape[1:] != (3,):
            raise ValueError(
                f"triangles must have shape (M, 3); got {triangles.shape}"
            )
        triangles = dtypes.as_indices(triangles, len(positions))

        colors = self._validate_colors(self.colors, len(positions))
        normals = self._validate_normals(self.normals, len(positions))
        texture_coordinates = self._validate_texture_coordinates(
            self.texture_coordinates, len(positions), len(triangles)
        )
        attributes = self._validate_attributes(
            self.attributes, len(positions), len(triangles)
        )

        object.__setattr__(self, "positions", positions)
        object.__setattr__(self, "triangles", triangles)
        object.__setattr__(self, "colors", colors)
        object.__setattr__(self, "normals", normals)
        object.__setattr__(self, "texture_coordinates", texture_coordinates)
        object.__setattr__(self, "attributes", MappingProxyType(attributes))

    @staticmethod
    def _validate_colors(value: ArrayLike | None, count: int) -> NDArray | None:
        if value is None:
            return None
        colors = _array(value, "colors")
        if colors.ndim != 2 or colors.shape[0] != count or colors.shape[1] not in (3, 4):
            raise ValueError(
                f"colors must have shape ({count}, 3) or ({count}, 4); "
                f"got {colors.shape}"
            )
        _finite(colors, "colors")
        return dtypes.as_colors(colors)

    @staticmethod
    def _validate_normals(value: ArrayLike | None, count: int) -> NDArray | None:
        if value is None:
            return None
        normals = _array(value, "normals")
        if normals.shape != (count, 3):
            raise ValueError(
                f"normals must have shape ({count}, 3); got {normals.shape}"
            )
        _finite(normals, "normals")
        return dtypes.as_normals(normals)

    @staticmethod
    def _validate_texture_coordinates(
        value: ArrayLike | None, vertex_count: int, triangle_count: int
    ) -> NDArray | None:
        if value is None:
            return None
        coordinates = _array(value, "texture_coordinates")
        valid_shapes = ((vertex_count, 2), (triangle_count, 3, 2))
        if coordinates.shape not in valid_shapes:
            raise ValueError(
                "texture_coordinates must be per-vertex with shape "
                f"({vertex_count}, 2) or per-corner with shape "
                f"({triangle_count}, 3, 2); got {coordinates.shape}"
            )
        _finite(coordinates, "texture_coordinates")
        return dtypes.as_texture_coordinates(coordinates)

    @staticmethod
    def _validate_attributes(
        values: Mapping[str, ArrayLike], vertex_count: int, triangle_count: int
    ) -> dict[str, NDArray]:
        if not isinstance(values, Mapping):
            raise TypeError("attributes must be a mapping")
        result: dict[str, NDArray] = {}
        allowed_counts = {vertex_count, triangle_count, triangle_count * 3}
        for name, value in values.items():
            if not isinstance(name, str) or not name:
                raise ValueError("attribute names must be non-empty strings")
            if name in _RESERVED_ATTRIBUTES:
                raise ValueError(f"{name!r} is a reserved attribute name")
            attribute = _array(value, f"attribute {name!r}")
            if attribute.ndim == 0 or attribute.shape[0] not in allowed_counts:
                raise ValueError(
                    f"attribute {name!r} must be vertex-, triangle-, or "
                    "triangle-corner-aligned"
                )
            _finite(attribute, f"attribute {name!r}")
            result[name] = dtypes.as_attribute(attribute, f"attribute {name!r}")
        return result
