"""Equivalence check for QNDF's linear-time decoder preprocessing."""

from __future__ import annotations

import importlib.util
from pathlib import Path

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
