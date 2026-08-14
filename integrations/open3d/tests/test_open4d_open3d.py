from types import SimpleNamespace

import numpy as np
import open3d as o3d
import pytest

pytestmark = pytest.mark.open3d

from integrations.open3d import frame_to_open3d


VERTICES = np.array(
    [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]],
    dtype=np.float32,
)
TRIANGLES = np.array([[0, 1, 2]], dtype=np.uint32)


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
