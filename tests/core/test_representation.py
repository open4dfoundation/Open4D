"""Contract tests for the representation axis: geometry kinds beyond triangles."""

from __future__ import annotations

import numpy as np
import pytest

from open4d import (
    Frame,
    GaussianCloud,
    Geometry,
    PointCloud,
    Representation,
    TriangleMesh,
)

pytestmark = pytest.mark.cpu


def points(count: int = 3) -> PointCloud:
    return PointCloud(np.arange(count * 3, dtype=np.float32).reshape(count, 3))


def gaussians(count: int = 2) -> GaussianCloud:
    return GaussianCloud(
        positions=np.zeros((count, 3), dtype=np.float32),
        scales=np.ones((count, 3), dtype=np.float32),
        rotations=np.tile(np.asarray([1, 0, 0, 0], dtype=np.float32), (count, 1)),
        opacities=np.full(count, 0.5, dtype=np.float32),
    )


def mesh() -> TriangleMesh:
    return TriangleMesh(
        np.asarray([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32),
        np.asarray([[0, 1, 2]], dtype=np.uint32),
    )


# ---------------------------------------------------------------- taxonomy ---


def test_each_type_declares_its_representation():
    assert mesh().representation is Representation.MESH
    assert points().representation is Representation.POINTS
    assert gaussians().representation is Representation.GAUSSIANS


def test_only_pixels_lacks_geometry():
    """The Explore/free-camera gate. Everything decodable to 3D admits a camera."""
    assert Representation.PIXELS.has_geometry is False
    for value in (
        Representation.MESH,
        Representation.POINTS,
        Representation.GAUSSIANS,
    ):
        assert value.has_geometry is True


def test_representation_values_are_stable_strings():
    """These land in `view.json` and in URLs, so the wire values are contract."""
    assert [member.value for member in Representation] == [
        "mesh",
        "points",
        "gaussians",
        "pixels",
    ]


# ------------------------------------------------------------------- Frame ---


@pytest.mark.parametrize("geometry", [mesh(), points(), gaussians()])
def test_frame_accepts_every_representation(geometry):
    frame = Frame(0, 0.0, geometry)
    assert frame.geometry is geometry


def test_frame_rejects_a_value_that_is_not_geometry():
    with pytest.raises(TypeError, match="open4d.core.Geometry"):
        Frame(0, 0.0, object())


def test_frame_rejects_a_bogus_representation():
    class Fake:
        representation = "gaussians"  # a string, not the enum

    assert isinstance(Fake(), Geometry)  # satisfies the protocol structurally
    with pytest.raises(TypeError, match="must be a Representation"):
        Frame(0, 0.0, Fake())


def test_a_third_party_type_needs_no_edit_to_core():
    class Voxels:
        @property
        def representation(self) -> Representation:
            return Representation.POINTS

    assert Frame(0, 0.0, Voxels()).geometry.representation is Representation.POINTS


# -------------------------------------------------------------- PointCloud ---


def test_point_cloud_is_not_a_mesh_with_no_triangles():
    assert not hasattr(points(), "triangles")


def test_point_cloud_coerces_to_the_canonical_dtypes():
    cloud = PointCloud(
        np.zeros((2, 3), dtype=np.float64),
        colors=np.asarray([[255, 0, 0], [0, 255, 0]], dtype=np.uint8),
    )
    assert cloud.positions.dtype == np.float32
    assert cloud.colors.dtype == np.float32
    assert cloud.colors.max() <= 1.0


def test_point_cloud_rejects_misaligned_attributes():
    with pytest.raises(ValueError, match="point-aligned"):
        PointCloud(np.zeros((3, 3), dtype=np.float32), attributes={"weight": [1.0, 2.0]})


# ------------------------------------------------------------ GaussianCloud ---


def test_gaussian_rotations_are_normalised_on_construction():
    """A renderer building a covariance from a non-unit quaternion silently scales."""
    cloud = GaussianCloud(
        positions=np.zeros((1, 3), dtype=np.float32),
        scales=np.ones((1, 3), dtype=np.float32),
        rotations=np.asarray([[0, 3, 4, 0]], dtype=np.float32),
        opacities=np.asarray([1.0], dtype=np.float32),
    )
    assert np.allclose(np.linalg.norm(cloud.rotations, axis=1), 1.0)
    assert np.allclose(cloud.rotations, [[0, 0.6, 0.8, 0]])


def test_gaussian_cloud_rejects_a_zero_quaternion():
    with pytest.raises(ValueError, match="zero quaternion"):
        GaussianCloud(
            positions=np.zeros((1, 3), dtype=np.float32),
            scales=np.ones((1, 3), dtype=np.float32),
            rotations=np.zeros((1, 4), dtype=np.float32),
            opacities=np.asarray([1.0], dtype=np.float32),
        )


def test_gaussian_cloud_rejects_raw_log_scales():
    """Negative scale means the caller passed 3DGS's raw parameter, not a stddev."""
    with pytest.raises(ValueError, match="nonnegative"):
        GaussianCloud(
            positions=np.zeros((1, 3), dtype=np.float32),
            scales=np.asarray([[-2.0, -2.0, -2.0]], dtype=np.float32),
            rotations=np.asarray([[1, 0, 0, 0]], dtype=np.float32),
            opacities=np.asarray([1.0], dtype=np.float32),
        )


@pytest.mark.parametrize("value", [-0.5, 1.5])
def test_gaussian_cloud_rejects_unactivated_opacity(value):
    with pytest.raises(ValueError, match=r"\[0, 1\]"):
        GaussianCloud(
            positions=np.zeros((1, 3), dtype=np.float32),
            scales=np.ones((1, 3), dtype=np.float32),
            rotations=np.asarray([[1, 0, 0, 0]], dtype=np.float32),
            opacities=np.asarray([value], dtype=np.float32),
        )


def test_gaussian_cloud_accepts_a_column_of_opacities():
    """3DGS stores opacity as (N, 1); accepting it avoids a reshape at every call."""
    cloud = GaussianCloud(
        positions=np.zeros((2, 3), dtype=np.float32),
        scales=np.ones((2, 3), dtype=np.float32),
        rotations=np.tile(np.asarray([1, 0, 0, 0], dtype=np.float32), (2, 1)),
        opacities=np.full((2, 1), 0.25, dtype=np.float32),
    )
    assert cloud.opacities.shape == (2,)


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("scales", np.ones((3, 3), dtype=np.float32)),
        ("rotations", np.tile(np.asarray([1, 0, 0, 0], dtype=np.float32), (3, 1))),
        ("opacities", np.ones(3, dtype=np.float32)),
    ],
)
def test_gaussian_cloud_requires_every_field_to_match_the_point_count(field, value):
    kwargs = {
        "positions": np.zeros((2, 3), dtype=np.float32),
        "scales": np.ones((2, 3), dtype=np.float32),
        "rotations": np.tile(np.asarray([1, 0, 0, 0], dtype=np.float32), (2, 1)),
        "opacities": np.full(2, 0.5, dtype=np.float32),
        field: value,
    }
    with pytest.raises(ValueError, match="must have shape"):
        GaussianCloud(**kwargs)


def test_gaussian_cloud_is_structurally_immutable():
    cloud = gaussians()
    with pytest.raises(Exception):
        cloud.positions = np.zeros((2, 3), dtype=np.float32)


def test_empty_gaussian_cloud_is_valid():
    cloud = GaussianCloud(
        positions=np.zeros((0, 3), dtype=np.float32),
        scales=np.zeros((0, 3), dtype=np.float32),
        rotations=np.zeros((0, 4), dtype=np.float32),
        opacities=np.zeros(0, dtype=np.float32),
    )
    assert cloud.representation is Representation.GAUSSIANS
    assert len(cloud.positions) == 0
