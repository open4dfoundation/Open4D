from types import SimpleNamespace

import numpy as np
import open3d as o3d
import pytest

pytestmark = pytest.mark.open3d

from integrations.open3d import frame_to_open3d
from open4d.core import Frame, TriangleMesh


VERTICES = np.array(
    [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]],
    dtype=np.float32,
)
TRIANGLES = np.array([[0, 1, 2]], dtype=np.uint32)


def test_rgba_colors_are_accepted_with_explicit_alpha_warning():
    colors = np.tile([1., 0.5, 0., 0.4], (3, 1))
    frame = Frame(0, 0., TriangleMesh(VERTICES, TRIANGLES, colors=colors))
    with pytest.warns(UserWarning, match="alpha"):
        mesh = frame_to_open3d(frame)
    np.testing.assert_allclose(np.asarray(mesh.vertex_colors), colors[:, :3])


def test_core_frame_conversion() -> None:
    frame = Frame(7, 0.25, TriangleMesh(VERTICES, TRIANGLES))

    mesh = frame_to_open3d(frame)

    assert isinstance(mesh, o3d.geometry.TriangleMesh)
    np.testing.assert_array_equal(np.asarray(mesh.vertices), VERTICES)
    np.testing.assert_array_equal(np.asarray(mesh.triangles), TRIANGLES)


@pytest.mark.parametrize("field", ["positions", "colors", "normals"])
def test_complex_geometry_is_rejected_instead_of_truncated(field):
    values = {"positions": np.zeros((1, 3)), field: np.array([[1 + 4j, 0, 0]])}
    with pytest.raises(TypeError, match="real numbers"):
        frame_to_open3d(values)


def test_triangle_mesh_conversion_with_colors_and_normals() -> None:
    colors = np.array([[255, 0, 0], [0, 255, 0], [0, 0, 255]], dtype=np.uint8)
    normals = np.tile([0.0, 0.0, 1.0], (3, 1))
    frame = SimpleNamespace(
        vertices=VERTICES,
        faces=TRIANGLES,
        vertex_colors=colors,
        vertex_normals=normals,
    )

    mesh = frame_to_open3d(frame)

    assert isinstance(mesh, o3d.geometry.TriangleMesh)
    np.testing.assert_array_equal(np.asarray(mesh.triangles), TRIANGLES)
    np.testing.assert_allclose(np.asarray(mesh.vertex_colors), colors / 255.0)
    np.testing.assert_allclose(np.asarray(mesh.vertex_normals), normals)


def test_point_cloud_conversion_with_colors() -> None:
    colors = np.array([[255, 128, 0], [0, 64, 255], [10, 20, 30]], dtype=np.uint8)

    cloud = frame_to_open3d((VERTICES, colors, 0.0))

    assert isinstance(cloud, o3d.geometry.PointCloud)
    np.testing.assert_allclose(np.asarray(cloud.points), VERTICES)
    np.testing.assert_allclose(np.asarray(cloud.colors), colors / 255.0)


@pytest.mark.parametrize(
    ("frame", "message"),
    [
        ((np.ones((3, 2)), None, 0.0), r"shape \(N, 3\)"),
        (
            (VERTICES, np.ones((2, 2), dtype=np.int32), 0.0),
            r"shape \(M, 3\)",
        ),
    ],
)
def test_malformed_arrays_are_rejected(frame: object, message: str) -> None:
    with pytest.raises(ValueError, match=message):
        frame_to_open3d(frame)


def test_triangle_indices_must_reference_vertices() -> None:
    with pytest.raises(ValueError, match="between 0 and 2"):
        frame_to_open3d((VERTICES, np.array([[0, 1, 3]]), 0.0))
