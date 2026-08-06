"""The canonical dtypes an Open4D mesh is stored in.

Geometry reaches Open4D from many producers — mesh readers, neural decoders,
live capture, other libraries — and each has its own idea of what a vertex
buffer looks like. Left alone, that variety propagates: every consumer grows its
own normalization step, those steps disagree, and the shared data model stops
being shared. So arrays are coerced once, at construction, and anything reading a
`TriangleMesh` can rely on the dtype without branching on it.

Coercion is nearly free. `astype(..., copy=False)` returns the input untouched
when it already matches, and the readers in `examples/visualization` already
produce float32 positions and uint32 indices, so the common path copies nothing.
Only a producer that disagrees with the canon pays for it.

Positions are stored float32 and *computed* in float64 — that is already what the
repository does in practice, and writing it down makes it a rule rather than a
coincidence. float32 halves the footprint of a decoded sequence, which matters
because the viewers decode every frame they intend to show up front, and it is
what a GL buffer wants anyway. The cost is roughly 0.5 m of resolution at UTM
magnitudes, so positions are understood to be scene-local: a georeferenced
capture carries its offset in a transform, not in the vertex buffer.

Colors are the one field with two live conventions — bytes in [0, 255] from a
`.ply`, floats in [0, 1] from USD. Both are accepted and both are stored as
float in [0, 1], so a consumer never has to ask which one it was handed.

Values are range-checked *before* the cast, so a `uint64` index cannot wrap into
a plausible-looking `uint32`, and checked again after it, so a float64 coordinate
too large for float32 is reported rather than silently becoming infinity.
"""

from __future__ import annotations

import numpy as np
from numpy.typing import NDArray

# Storage for vertex positions, normals, texture coordinates, and colors.
POSITION_DTYPE = np.dtype(np.float32)
NORMAL_DTYPE = np.dtype(np.float32)
UV_DTYPE = np.dtype(np.float32)
COLOR_DTYPE = np.dtype(np.float32)

# Triangle indices. uint32 matches GL index buffers and Draco, and addresses
# more vertices than a frame will hold. It has no room for a negative sentinel:
# a codec that needs "no index" carries a separate mask.
INDEX_DTYPE = np.dtype(np.uint32)

# Named per-vertex/-triangle/-corner streams keep their kind but not their width,
# so two codecs writing the same attribute name produce comparable arrays.
ATTRIBUTE_FLOAT_DTYPE = np.dtype(np.float32)
ATTRIBUTE_INT_DTYPE = np.dtype(np.int32)

# The value a color of each kind may not exceed, keyed by whether it is integral.
_INTEGER_COLOR_MAXIMUM = 255
_FLOAT_COLOR_MAXIMUM = 1.0


def _cast(array: NDArray, target: np.dtype, name: str) -> NDArray:
    """Cast *array* to *target*, refusing to lose the values it holds.

    The caller has already established that the incoming values are finite, so
    an infinity on the far side of the cast is overflow rather than input.
    """
    # Overflow is the condition being tested for, so NumPy's warning about it is
    # noise: the infinity it produces is caught and reported on the next line.
    with np.errstate(over="ignore"):
        result = array.astype(target, copy=False)
    if np.issubdtype(target, np.floating) and not np.isfinite(result).all():
        raise ValueError(
            f"{name} holds values too large for {target.name}; Open4D stores "
            f"{name} as {target.name} and treats coordinates as scene-local, so "
            "carry a large offset in a transform rather than in the array"
        )
    return result


def as_positions(array: NDArray, name: str = "positions") -> NDArray:
    """Return *array* as canonical position storage."""
    if not np.issubdtype(array.dtype, np.floating):
        raise TypeError(f"{name} must have a floating-point dtype")
    return _cast(array, POSITION_DTYPE, name)


def as_normals(array: NDArray, name: str = "normals") -> NDArray:
    """Return *array* as canonical normal storage."""
    if not np.issubdtype(array.dtype, np.floating):
        raise TypeError(f"{name} must have a floating-point dtype")
    return _cast(array, NORMAL_DTYPE, name)


def as_texture_coordinates(
    array: NDArray, name: str = "texture_coordinates"
) -> NDArray:
    """Return *array* as canonical texture-coordinate storage."""
    if not np.issubdtype(array.dtype, np.floating):
        raise TypeError(f"{name} must have a floating-point dtype")
    return _cast(array, UV_DTYPE, name)


def as_indices(
    array: NDArray, vertex_count: int, name: str = "triangles"
) -> NDArray:
    """Return *array* as canonical index storage, in range for *vertex_count*.

    The bounds are checked in Python integers against the array as supplied, so
    a value that would wrap on the way to uint32 is rejected for being out of
    range instead of arriving as a different, valid-looking index.
    """
    if not np.issubdtype(array.dtype, np.integer) or array.dtype == np.bool_:
        raise TypeError(f"{name} must have an integer dtype")
    if array.size and (
        int(array.min()) < 0 or int(array.max()) >= vertex_count
    ):
        raise ValueError(
            f"triangle indices must be between 0 and {vertex_count - 1}"
        )
    return array.astype(INDEX_DTYPE, copy=False)


def as_colors(array: NDArray, name: str = "colors") -> NDArray:
    """Return *array* as canonical color storage: float in [0, 1].

    Integral input is read as bytes in [0, 255] and scaled; floating-point input
    is expected to be in [0, 1] already. Both land in the same place, so the
    stored range is a property of the mesh rather than of its source.
    """
    is_integer = np.issubdtype(array.dtype, np.integer)
    if not (is_integer or np.issubdtype(array.dtype, np.floating)):
        raise TypeError(f"{name} must have an integer or floating-point dtype")

    # Compared as arrays rather than through int(), which would truncate a
    # fractional -0.5 to 0 and admit a negative color.
    maximum = _INTEGER_COLOR_MAXIMUM if is_integer else _FLOAT_COLOR_MAXIMUM
    if array.size and (array.min() < 0 or array.max() > maximum):
        raise ValueError(f"{name} must be in the range [0, {maximum}]")

    if is_integer:
        return (array / _INTEGER_COLOR_MAXIMUM).astype(COLOR_DTYPE, copy=False)
    return _cast(array, COLOR_DTYPE, name)


def as_attribute(array: NDArray, name: str) -> NDArray:
    """Return a named attribute in canonical storage for its kind.

    Booleans are left alone — a mask is not a narrow integer — while integers and
    floats are widened or narrowed to one width each, so an attribute name means
    the same thing whichever codec wrote it.
    """
    if array.dtype == np.bool_:
        return array
    if np.issubdtype(array.dtype, np.integer):
        return array.astype(ATTRIBUTE_INT_DTYPE, copy=False)
    if np.issubdtype(array.dtype, np.floating):
        return _cast(array, ATTRIBUTE_FLOAT_DTYPE, name)
    raise TypeError(f"{name} must have a numeric or boolean dtype")
