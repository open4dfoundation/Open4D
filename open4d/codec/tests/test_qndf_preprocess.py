"""Equivalence check for QNDF's linear-time decoder preprocessing."""

from __future__ import annotations

import importlib.util
from pathlib import Path

import numpy as np
import pytest

torch = pytest.importorskip("torch")


def test_adjacency_preprocessing_matches_original_face_scan():
    source = Path(__file__).resolve().parents[2] / "codecs/qndf/compress.py"
    spec = importlib.util.spec_from_file_location("qndf_compress_test", source)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)

    faces = torch.tensor([[0, 1, 2], [0, 2, 3], [2, 3, 4]])
    vertices = torch.tensor([
        [0.0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0], [0, 2, 0]
    ])
    encoded = torch.arange(30, dtype=torch.float32).reshape(5, 6)
    actual = module.MeshDataset(encoded, vertices, faces, torch.zeros_like(vertices))

    expected_neighbors = []
    expected_weights = []
    for index in range(len(vertices)):
        rows, _ = torch.nonzero(faces == index, as_tuple=True)
        indices = torch.unique(faces[rows])
        indices = indices[indices != index]
        weights = torch.softmax(torch.linalg.vector_norm(vertices[indices] - vertices[index], dim=1), dim=0)
        neighbor_pad = torch.zeros((14, encoded.shape[1]))
        weight_pad = torch.zeros(14)
        neighbor_pad[: len(indices)] = encoded[indices][:14]
        weight_pad[: len(indices)] = weights[:14]
        expected_neighbors.append(neighbor_pad)
        expected_weights.append(weight_pad)

    torch.testing.assert_close(actual.neighbors, torch.stack(expected_neighbors))
    torch.testing.assert_close(actual.edge_wts, torch.stack(expected_weights))


@pytest.mark.open3d
def test_simplification_retains_every_disconnected_component():
    o3d = pytest.importorskip("open3d")
    from open4d.codecs.qndf.build_dataset_open3d import build_pair

    vertices = np.array([
        [0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
        [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1],
    ], dtype=np.float64)
    faces = np.array([
        [0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7],
        [0, 1, 5], [0, 5, 4], [1, 2, 6], [1, 6, 5],
        [2, 3, 7], [2, 7, 6], [3, 0, 4], [3, 4, 7],
    ], dtype=np.int32)
    vertices = np.vstack((vertices, vertices + [4, 0, 0]))
    faces = np.vstack((faces, faces + 8))

    low, low_faces, target, metadata = build_pair(
        vertices, faces, coarse_size=8, num_subdiv=0
    )
    mesh = o3d.geometry.TriangleMesh(
        o3d.utility.Vector3dVector(low), o3d.utility.Vector3iVector(low_faces)
    )
    _, component_counts, _ = mesh.cluster_connected_triangles()

    assert metadata["component_count"] == 2
    assert len(component_counts) == 2
    assert sum(item["allocated_coarse_faces"] for item in metadata["components"]) == 8
    assert all(item["actual_coarse_faces"] > 0 for item in metadata["components"])
    assert all(item["simplification_target_faces"] == item["allocated_coarse_faces"]
               for item in metadata["components"])
    assert metadata["actual_coarse_faces"] == 8
    assert metadata["coarse_face_budget_overshoot"] == 0
    assert target.shape == low.shape


@pytest.mark.open3d
def test_simplification_retries_collapsing_real_component_and_reports_overshoot():
    o3d = pytest.importorskip("open3d")
    from open4d.codecs.qndf.build_dataset_open3d import build_pair

    # C5/mitch frame 100, component 62 after the builder's normalization.
    vertices = np.array([
        [0.3591478930135873, 0.08521303404473901, 0.04937343443180466],
        [0.3676691964180612, 0.060651630114196595, 0.04761904843676592],
        [0.36140354932215, 0.0571428581241191, 0.04987468757324431],
        [0.35639101790775357, 0.059899750402037134, 0.04135338416877041],
        [0, 0, 0], [1, 0, 0], [0, 1, 0],
    ], dtype=np.float64)
    faces = np.array([[0, 1, 2], [0, 2, 3], [0, 3, 1], [4, 5, 6]], dtype=np.int32)

    low, low_faces, target, metadata = build_pair(vertices, faces, coarse_size=2, num_subdiv=0)
    mesh = o3d.geometry.TriangleMesh(
        o3d.utility.Vector3dVector(low), o3d.utility.Vector3iVector(low_faces)
    )
    assert len(mesh.cluster_connected_triangles()[1]) == 2
    component = metadata["components"][0]
    assert component["original_faces"] == 3
    assert component["allocated_coarse_faces"] == 1
    assert component["simplification_target_faces"] == 2
    assert component["actual_coarse_faces"] == 2
    assert metadata["actual_coarse_faces"] == 3
    assert metadata["coarse_face_budget_overshoot"] == 1
    assert np.isfinite(low).all() and np.isfinite(target).all()
    assert np.min(target[:component["training_vertices"], 2]) > 0.04
