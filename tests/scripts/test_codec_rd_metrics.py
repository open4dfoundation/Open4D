import numpy as np
import pytest

from open4d.core import TriangleMesh
from scripts.codec_rd.metrics import mesh_errors


def test_exact_surface_metric_distinguishes_error_below_independent_sampling_floor():
    pytest.importorskip("point_cloud_utils")
    vertices = np.array([[0., 0, 0], [1., 0, 0], [1., 1, 0], [0., 1, 0]])
    reference = TriangleMesh(vertices, [[0, 1, 2], [0, 2, 3]])
    decoded = TriangleMesh(vertices + [0, 0, .001], reference.triangles)
    result = mesh_errors(reference, decoded, peak=2, samples=100, exact_hausdorff=False)
    assert result["d1_mse"] > 1e-4  # Independent point spacing masks this error.
    assert result["surface_d1_mse"] == pytest.approx(1e-6, rel=1e-5)
    assert result["surface_d2_mse"] == pytest.approx(1e-6, rel=1e-5)


def test_surface_metric_accepts_different_tessellations_of_the_same_square():
    pytest.importorskip("point_cloud_utils")
    vertices = np.array([[0., 0, 0], [1., 0, 0], [1., 1, 0], [0., 1, 0]])
    reference = TriangleMesh(vertices, [[0, 1, 2], [0, 2, 3]])
    decoded = TriangleMesh(vertices, [[0, 1, 3], [1, 2, 3]])
    result = mesh_errors(reference, decoded, peak=2, samples=100, exact_hausdorff=False)
    assert result["surface_d1_mse"] < 1e-25
    assert result["surface_d2_mse"] == 0


def test_collapsed_native_triangles_do_not_create_nan_surface_metrics():
    pytest.importorskip("point_cloud_utils")
    vertices = np.array([[0., 0, 0], [1., 0, 0], [1., 1, 0], [0., 1, 0]])
    reference = TriangleMesh(vertices, [[0, 1, 2], [0, 2, 3]])
    decoded = TriangleMesh(vertices + [0, 0, .001], [[0, 1, 2], [0, 2, 3], [0, 0, 0]])
    result = mesh_errors(reference, decoded, peak=2, samples=100, exact_hausdorff=False)
    assert result["surface_d1_mse"] == pytest.approx(1e-6, rel=1e-5)
    assert result["surface_d2_mse"] == pytest.approx(1e-6, rel=1e-5)
