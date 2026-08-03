"""NumPy-backed geometry values shared by Open4D pipelines."""

from __future__ import annotations

from dataclasses import dataclass, field
from types import MappingProxyType
from typing import Mapping

import numpy as np
from numpy.typing import ArrayLike, NDArray

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
    """A validated triangle mesh that retains the supplied NumPy storage.

    Instances are structurally immutable: fields and the attribute mapping
    cannot be replaced. Array buffers are intentionally not made read-only and
    may be shared with the caller. Mutating such a buffer mutates the mesh.
    Callers that need value immutability should pass copies.
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
        if not np.issubdtype(positions.dtype, np.floating):
            raise TypeError("positions must have a floating-point dtype")
        _finite(positions, "positions")

        triangles = _array(self.triangles, "triangles")
        if triangles.ndim != 2 or triangles.shape[1:] != (3,):
            raise ValueError(
                f"triangles must have shape (M, 3); got {triangles.shape}"
            )
        if not np.issubdtype(triangles.dtype, np.integer) or (
            triangles.dtype == np.bool_
        ):
            raise TypeError("triangles must have an integer dtype")
        if triangles.size and (
            np.any(triangles < 0) or np.any(triangles >= len(positions))
        ):
            raise ValueError(
                f"triangle indices must be between 0 and {len(positions) - 1}"
            )

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
        if not (
            np.issubdtype(colors.dtype, np.integer)
            or np.issubdtype(colors.dtype, np.floating)
        ) or colors.dtype == np.bool_:
            raise TypeError("colors must have an integer or floating-point dtype")
        _finite(colors, "colors")
        maximum = 255 if np.issubdtype(colors.dtype, np.integer) else 1.0
        if np.any(colors < 0) or np.any(colors > maximum):
            raise ValueError(f"colors must be in the range [0, {maximum}]")
        return colors

    @staticmethod
    def _validate_normals(value: ArrayLike | None, count: int) -> NDArray | None:
        if value is None:
            return None
        normals = _array(value, "normals")
        if normals.shape != (count, 3):
            raise ValueError(
                f"normals must have shape ({count}, 3); got {normals.shape}"
            )
        if not np.issubdtype(normals.dtype, np.floating):
            raise TypeError("normals must have a floating-point dtype")
        _finite(normals, "normals")
        return normals

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
        if not np.issubdtype(coordinates.dtype, np.floating):
            raise TypeError(
                "texture_coordinates must have a floating-point dtype"
            )
        _finite(coordinates, "texture_coordinates")
        return coordinates

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
            result[name] = attribute
        return result
