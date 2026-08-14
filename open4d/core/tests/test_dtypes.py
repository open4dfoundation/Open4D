"""Tests for the canonical dtype contract `TriangleMesh` enforces.

The point of the canon is that a consumer can read a mesh without asking what
produced it, so these tests are written from that side: whatever dtype goes in,
assert what comes out. The interesting cases are the ones where a plain
`asarray` would have quietly succeeded — a float16 position array, a uint64
index that wraps, a byte color that means 255 where a float color means 1.0.

Value-preservation is checked against something other than the cast itself:
byte and float spellings of the same color must produce bit-identical storage,
and a coordinate too large for float32 must be reported rather than stored as
infinity.
"""

from __future__ import annotations

import numpy as np
import pytest

from open4d import (
    COLOR_DTYPE,
    INDEX_DTYPE,
    NORMAL_DTYPE,
    POSITION_DTYPE,
    UV_DTYPE,
    TriangleMesh,
)

POSITIONS = np.array(
    [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [1.0, 1.0, 0.0]]
)
TRIANGLES = np.array([[0, 1, 2], [1, 3, 2]])

FLOAT_DTYPES = [np.float16, np.float32, np.float64, np.longdouble]
INTEGER_DTYPES = [
    np.int8, np.uint8, np.int16, np.uint16,
    np.int32, np.uint32, np.int64, np.uint64,
]

pytestmark = pytest.mark.cpu


# ----------------------------
# Storage is canonical whatever the producer used
# ----------------------------
@pytest.mark.parametrize("dtype", FLOAT_DTYPES)
def test_positions_are_stored_float32(dtype):
    mesh = TriangleMesh(POSITIONS.astype(dtype), TRIANGLES)
    assert mesh.positions.dtype == POSITION_DTYPE


@pytest.mark.parametrize("dtype", INTEGER_DTYPES)
def test_triangles_are_stored_uint32(dtype):
    mesh = TriangleMesh(POSITIONS, TRIANGLES.astype(dtype))
    assert mesh.triangles.dtype == INDEX_DTYPE
    # The values survive the narrowing, not just the dtype.
    assert np.array_equal(mesh.triangles, TRIANGLES)


@pytest.mark.parametrize("dtype", FLOAT_DTYPES)
def test_normals_and_uvs_are_stored_float32(dtype):
    normals = np.tile([0.0, 0.0, 1.0], (len(POSITIONS), 1)).astype(dtype)
    uvs = np.zeros((len(POSITIONS), 2), dtype=dtype)
    mesh = TriangleMesh(POSITIONS, TRIANGLES, normals=normals,
                        texture_coordinates=uvs)
    assert mesh.normals.dtype == NORMAL_DTYPE
    assert mesh.texture_coordinates.dtype == UV_DTYPE


def test_attributes_are_narrowed_by_kind_and_masks_stay_boolean():
    mesh = TriangleMesh(
        POSITIONS, TRIANGLES,
        attributes={
            "curvature": np.zeros(len(POSITIONS), dtype=np.float64),
            "label": np.zeros(len(POSITIONS), dtype=np.int64),
            "selected": np.zeros(len(POSITIONS), dtype=bool),
        },
    )
    assert mesh.attributes["curvature"].dtype == np.float32
    assert mesh.attributes["label"].dtype == np.int32
    assert mesh.attributes["selected"].dtype == np.bool_


@pytest.mark.parametrize(
    "value",
    [np.iinfo(np.int32).min - 1, np.iinfo(np.int32).max + 1],
)
def test_integer_attributes_reject_values_that_would_overflow_int32(value):
    labels = np.full(len(POSITIONS), value, dtype=np.int64)
    with pytest.raises(ValueError, match="outside the range"):
        TriangleMesh(POSITIONS, TRIANGLES, attributes={"label": labels})


@pytest.mark.parametrize(
    "value",
    [np.iinfo(np.int32).min, np.iinfo(np.int32).max],
)
def test_integer_attributes_accept_int32_boundary_values(value):
    """int32 min and max must be accepted and stored canonically as int32."""
    labels = np.full(len(POSITIONS), value, dtype=np.int64)
    mesh = TriangleMesh(POSITIONS, TRIANGLES, attributes={"label": labels})
    assert mesh.attributes["label"].dtype == np.int32
    assert np.array_equal(mesh.attributes["label"], value)


def test_empty_integer_attributes_are_accepted():
    """Empty attribute arrays must be accepted without validation errors.

    An attribute's length must align with vertex/triangle/corner counts, so
    this is only well-defined on a mesh that itself has zero vertices and
    zero triangles.
    """
    empty_positions = np.zeros((0, 3))
    empty_triangles = np.zeros((0, 3), dtype=np.int64)
    empty_labels = np.array([], dtype=np.int64)
    mesh = TriangleMesh(
        empty_positions, empty_triangles, attributes={"label": empty_labels}
    )
    assert mesh.attributes["label"].dtype == np.int32
    assert len(mesh.attributes["label"]) == 0


# ----------------------------
# The two color conventions land in the same place
# ----------------------------
def test_byte_and_float_colors_produce_identical_storage():
    """A .ply's 255 and USD's 1.0 are the same color, so they must store alike."""
    red_bytes = np.array([[255, 0, 0]] * len(POSITIONS), dtype=np.uint8)
    red_floats = np.array([[1.0, 0.0, 0.0]] * len(POSITIONS), dtype=np.float32)

    from_bytes = TriangleMesh(POSITIONS, TRIANGLES, colors=red_bytes).colors
    from_floats = TriangleMesh(POSITIONS, TRIANGLES, colors=red_floats).colors

    assert from_bytes.dtype == COLOR_DTYPE
    assert from_floats.dtype == COLOR_DTYPE
    assert np.array_equal(from_bytes, from_floats)


def test_mid_grey_bytes_scale_rather_than_truncate():
    grey = np.array([[128, 128, 128]] * len(POSITIONS), dtype=np.uint8)
    stored = TriangleMesh(POSITIONS, TRIANGLES, colors=grey).colors
    assert np.allclose(stored, 128 / 255)


def test_alpha_channel_is_preserved():
    rgba = np.array([[255, 0, 0, 128]] * len(POSITIONS), dtype=np.uint8)
    stored = TriangleMesh(POSITIONS, TRIANGLES, colors=rgba).colors
    assert stored.shape == (len(POSITIONS), 4)
    assert np.allclose(stored[:, 3], 128 / 255)


@pytest.mark.parametrize(
    "colors",
    [
        np.array([[256, 0, 0]] * len(POSITIONS), dtype=np.int16),
        np.array([[1.5, 0.0, 0.0]] * len(POSITIONS), dtype=np.float32),
        np.array([[-0.5, 0.0, 0.0]] * len(POSITIONS), dtype=np.float32),
    ],
    ids=["byte-above-255", "float-above-1", "negative-float"],
)
def test_out_of_range_colors_are_rejected(colors):
    """One policy, not clip-here-raise-there: construction refuses the value."""
    with pytest.raises(ValueError, match="range"):
        TriangleMesh(POSITIONS, TRIANGLES, colors=colors)


# ----------------------------
# Casts that would lose information are reported
# ----------------------------
def test_index_too_large_for_uint32_is_rejected_not_wrapped():
    """2**32 + 1 must not arrive as vertex 1.

    Checked against the same dtype used legitimately, so a pass cannot come from
    uint64 being rejected outright.
    """
    assert TriangleMesh(POSITIONS, TRIANGLES.astype(np.uint64)).triangles.dtype == (
        INDEX_DTYPE
    )
    wrapping = np.array([[0, 1, 2**32 + 1]], dtype=np.uint64)
    with pytest.raises(ValueError, match="between 0 and"):
        TriangleMesh(POSITIONS, wrapping)


def test_negative_index_is_rejected():
    with pytest.raises(ValueError, match="between 0 and"):
        TriangleMesh(POSITIONS, np.array([[0, 1, -1]], dtype=np.int32))


def test_out_of_bounds_index_is_rejected():
    with pytest.raises(ValueError, match="between 0 and"):
        TriangleMesh(POSITIONS, np.array([[0, 1, len(POSITIONS)]], dtype=np.int32))


def test_position_too_large_for_float32_is_reported_not_infinite():
    """A finite float64 coordinate must not become inf on the way to storage."""
    huge = np.array([[1e300, 0.0, 0.0]] * 4, dtype=np.float64)
    with pytest.raises(ValueError, match="too large"):
        TriangleMesh(huge, TRIANGLES)


def test_non_finite_positions_are_still_rejected():
    broken = POSITIONS.copy()
    broken[0, 0] = np.nan
    with pytest.raises(ValueError, match="finite"):
        TriangleMesh(broken, TRIANGLES)


# ----------------------------
# Coercion is free when the producer already conforms
# ----------------------------
def test_conforming_arrays_are_not_copied():
    """The readers already emit float32/uint32; that path must stay zero-copy."""
    positions = POSITIONS.astype(POSITION_DTYPE)
    triangles = TRIANGLES.astype(INDEX_DTYPE)
    mesh = TriangleMesh(positions, triangles)
    assert mesh.positions is positions
    assert mesh.triangles is triangles


def test_dtype_errors_still_reject_wrong_kinds():
    with pytest.raises(TypeError, match="floating-point"):
        TriangleMesh(POSITIONS.astype(np.int32), TRIANGLES)
    with pytest.raises(TypeError, match="integer"):
        TriangleMesh(POSITIONS, TRIANGLES.astype(np.float32))
