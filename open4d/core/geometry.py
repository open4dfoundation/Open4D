"""NumPy-backed geometry values shared by Open4D pipelines."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from types import MappingProxyType
from typing import Mapping, Protocol, runtime_checkable

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


class Representation(str, Enum):
    """What a decoded frame *is*, independent of how it was transported.

    This is the axis that decides which renderer can draw a frame and whether a
    free camera is meaningful at all, and it is deliberately separate from the
    codec that produced the frame: a triangle mesh is a mesh whether it arrived
    as OBJ, as a Draco payload, or out of a V-DMC bitstream. Conflating the two
    is what makes a viewer need a new branch per format instead of per
    representation.
    """

    MESH = "mesh"
    POINTS = "points"
    GAUSSIANS = "gaussians"
    #: Already-rendered pixels. Some representations cannot be decoded in the
    #: consumer's process at all -- ReRF's entropy coder ships only as a
    #: CPython 3.8 binary with no sources -- so server-rendered images are the
    #: honest form for them rather than a fallback. The concrete type lands with
    #: the camera model it needs in order to be comparable at a known pose; only
    #: the taxonomy entry and :attr:`has_geometry` exist here, which is what a
    #: consumer needs to gate on it today.
    PIXELS = "pixels"

    @property
    def has_geometry(self) -> bool:
        """Whether a free camera can be aimed at this representation.

        False only for :attr:`PIXELS`, which is fixed to whichever camera
        rendered it. Gate free-camera views on this rather than on a concrete
        type, so that a mesh and a Gaussian cloud are treated alike without
        either being named.
        """
        return self is not Representation.PIXELS


@runtime_checkable
class Geometry(Protocol):
    """What :class:`open4d.core.Frame` accepts as a frame's payload.

    One property, so a new representation can be added without editing `Frame`.
    The concrete types in this module implement it; so may a third party's.
    """

    @property
    def representation(self) -> Representation:
        """Which :class:`Representation` this value is."""


def _validate_point_attributes(
    values: Mapping[str, ArrayLike], count: int
) -> dict[str, NDArray]:
    """Validate attributes for a representation whose only alignment is per-point."""
    if not isinstance(values, Mapping):
        raise TypeError("attributes must be a mapping")
    result: dict[str, NDArray] = {}
    for name, value in values.items():
        if not isinstance(name, str) or not name:
            raise ValueError("attribute names must be non-empty strings")
        if name in _RESERVED_ATTRIBUTES:
            raise ValueError(f"{name!r} is a reserved attribute name")
        attribute = _array(value, f"attribute {name!r}")
        if attribute.ndim == 0 or attribute.shape[0] != count:
            raise ValueError(f"attribute {name!r} must be point-aligned")
        _finite(attribute, f"attribute {name!r}")
        result[name] = dtypes.as_attribute(attribute, f"attribute {name!r}")
    return result


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

    @property
    def representation(self) -> Representation:
        return Representation.MESH

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


@dataclass(frozen=True, eq=False)
class PointCloud:
    """Positions without connectivity, in the same canonical dtypes as a mesh.

    A distinct representation rather than a mesh with zero triangles: the RGB-D
    fusion path and the point-cloud codecs in this repository produce exactly
    this, and a consumer that wants to render points should not have to discover
    the absence of connectivity by inspecting an empty array.
    """

    positions: NDArray
    colors: NDArray | None = None
    normals: NDArray | None = None
    attributes: Mapping[str, NDArray] = field(default_factory=dict)

    def __post_init__(self) -> None:
        positions = _array(self.positions, "positions")
        if positions.ndim != 2 or positions.shape[1:] != (3,):
            raise ValueError(
                f"positions must have shape (N, 3); got {positions.shape}"
            )
        _finite(positions, "positions")
        positions = dtypes.as_positions(positions)
        count = len(positions)

        object.__setattr__(self, "positions", positions)
        object.__setattr__(
            self, "colors", TriangleMesh._validate_colors(self.colors, count)
        )
        object.__setattr__(
            self, "normals", TriangleMesh._validate_normals(self.normals, count)
        )
        object.__setattr__(
            self,
            "attributes",
            MappingProxyType(_validate_point_attributes(self.attributes, count)),
        )

    @property
    def representation(self) -> Representation:
        return Representation.POINTS


@dataclass(frozen=True, eq=False)
class GaussianCloud:
    """A frame of 3D Gaussians, stored activated and renderer-ready.

    Activated rather than raw -- world-space standard deviations, unit
    quaternions, opacity already in [0, 1] -- because the raw parameterisation is
    a training detail that differs between implementations. 3DGS stores
    log-scale and pre-sigmoid opacity; a consumer should not have to know which
    activation to apply, nor risk applying it twice. This is the same argument
    `open4d.core.dtypes` makes for dtypes, one level up.

    ``rotations`` are ``(w, x, y, z)``, matching the order 3DGS writes into a PLY,
    and are normalised on construction.

    ``colors`` is the view-*independent* term only. View-dependent appearance is
    not representable here and is deliberately not faked: a method whose colour
    is a hash grid or a higher-order SH expansion must either bake it for one
    direction and declare that it did, or keep its own renderer. Storing a
    degree-0 term and calling it the appearance is how a comparison quietly
    stops being one.
    """

    positions: NDArray
    scales: NDArray
    rotations: NDArray
    opacities: NDArray
    colors: NDArray | None = None
    attributes: Mapping[str, NDArray] = field(default_factory=dict)

    def __post_init__(self) -> None:
        positions = _array(self.positions, "positions")
        if positions.ndim != 2 or positions.shape[1:] != (3,):
            raise ValueError(
                f"positions must have shape (N, 3); got {positions.shape}"
            )
        _finite(positions, "positions")
        positions = dtypes.as_positions(positions)
        count = len(positions)

        scales = _array(self.scales, "scales")
        if scales.shape != (count, 3):
            raise ValueError(f"scales must have shape ({count}, 3); got {scales.shape}")
        _finite(scales, "scales")
        if scales.size and scales.min() < 0:
            raise ValueError(
                "scales must be nonnegative: Open4D stores activated "
                "world-space standard deviations, not log-scales"
            )
        scales = dtypes.as_positions(scales, "scales")

        rotations = _array(self.rotations, "rotations")
        if rotations.shape != (count, 4):
            raise ValueError(
                f"rotations must have shape ({count}, 4) as (w, x, y, z); "
                f"got {rotations.shape}"
            )
        _finite(rotations, "rotations")
        rotations = dtypes.as_positions(rotations, "rotations")
        norms = np.linalg.norm(rotations, axis=1, keepdims=True)
        if rotations.size and float(norms.min()) == 0.0:
            raise ValueError("rotations must not contain a zero quaternion")
        # Normalised here so every consumer can skip it. A renderer that builds a
        # covariance from a non-unit quaternion silently scales the Gaussian.
        rotations = (rotations / norms).astype(rotations.dtype, copy=False)

        opacities = _array(self.opacities, "opacities")
        if opacities.shape == (count, 1):
            opacities = opacities.reshape(count)
        if opacities.shape != (count,):
            raise ValueError(
                f"opacities must have shape ({count},) or ({count}, 1); "
                f"got {opacities.shape}"
            )
        _finite(opacities, "opacities")
        if opacities.size and (opacities.min() < 0.0 or opacities.max() > 1.0):
            raise ValueError(
                "opacities must lie in [0, 1]: Open4D stores activated opacity, "
                "not the pre-sigmoid parameter"
            )
        opacities = dtypes.as_positions(opacities, "opacities")

        colors = self.colors
        if colors is not None:
            colors = _array(colors, "colors")
            if colors.shape != (count, 3):
                raise ValueError(
                    f"colors must have shape ({count}, 3); got {colors.shape}"
                )
            _finite(colors, "colors")
            colors = dtypes.as_colors(colors)

        object.__setattr__(self, "positions", positions)
        object.__setattr__(self, "scales", scales)
        object.__setattr__(self, "rotations", rotations)
        object.__setattr__(self, "opacities", opacities)
        object.__setattr__(self, "colors", colors)
        object.__setattr__(
            self,
            "attributes",
            MappingProxyType(_validate_point_attributes(self.attributes, count)),
        )

    @property
    def representation(self) -> Representation:
        return Representation.GAUSSIANS
