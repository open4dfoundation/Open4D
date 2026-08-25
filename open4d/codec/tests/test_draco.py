"""DracoPy integration contracts for canonical mesh attributes."""

from __future__ import annotations

import numpy as np
import pytest

pytest.importorskip("DracoPy")

from open4d import Frame, MemoryFrameProvider, Sequence, TopologyMode, TriangleMesh
from open4d.codec import decode_sequence, encode_sequence

pytestmark = pytest.mark.cpu


def _round_trip(mesh: TriangleMesh, tmp_path) -> TriangleMesh:
    source = Sequence(MemoryFrameProvider([Frame(7, 1.25, mesh)]))
    decoded = decode_sequence(
        encode_sequence(source, tmp_path / "attributes.d4d", codec="draco")
    )
    assert decoded[0].frame_index == 7
    assert decoded[0].timestamp == 1.25
    return decoded[0].geometry


def test_draco_round_trip_preserves_canonical_normals_and_vertex_uvs(tmp_path):
    positions = np.array(
        [[0, 0, 0], [1, 0, .1], [1, 1, .2], [0, 1, .1]], dtype=np.float32
    )
    triangles = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.uint32)
    normals = np.array([[0, 0, 1], [0, .1, .99], [0, .2, .98], [0, .1, .99]], dtype=np.float32)
    uvs = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], dtype=np.float32)

    actual = _round_trip(
        TriangleMesh(positions, triangles, normals=normals, texture_coordinates=uvs),
        tmp_path,
    )

    np.testing.assert_array_equal(actual.triangles, triangles)
    np.testing.assert_allclose(actual.normals, normals, atol=1e-4)
    np.testing.assert_allclose(actual.texture_coordinates, uvs, atol=1e-4)


def test_draco_round_trip_preserves_per_corner_uv_seams_by_splitting_vertices(tmp_path):
    positions = np.array(
        [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], dtype=np.float32
    )
    triangles = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.uint32)
    normals = np.array([[0, 0, 1]] * 4, dtype=np.float32)
    corner_uvs = np.array(
        [[[0, 0], [1, 0], [1, 1]], [[.25, .25], [.75, 1], [0, 1]]],
        dtype=np.float32,
    )

    actual = _round_trip(
        TriangleMesh(
            positions, triangles, normals=normals,
            texture_coordinates=corner_uvs,
        ),
        tmp_path,
    )

    expected_indices = triangles.reshape(-1)
    np.testing.assert_allclose(actual.positions, positions[expected_indices], atol=1e-4)
    np.testing.assert_array_equal(
        actual.triangles, np.arange(6, dtype=np.uint32).reshape(-1, 3)
    )
    np.testing.assert_allclose(actual.normals, normals[expected_indices], atol=1e-4)
    np.testing.assert_allclose(
        actual.texture_coordinates, corner_uvs.reshape(-1, 2), atol=1e-4
    )


def test_draco_recomputes_declarations_after_uv_corner_splitting(tmp_path):
    positions = np.array(
        [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], dtype=np.float32
    )
    triangles = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.uint32)
    vertex_uvs = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], dtype=np.float32)
    corner_uvs = vertex_uvs[triangles]
    source = Sequence(MemoryFrameProvider(
        [
            Frame(0, 0, TriangleMesh(
                positions, triangles, texture_coordinates=vertex_uvs
            )),
            Frame(1, 1, TriangleMesh(
                positions, triangles, texture_coordinates=corner_uvs
            )),
        ],
        topology=TopologyMode.FIXED,
        has_constant_vertex_count=True,
        has_vertex_correspondence=True,
    ))

    decoded = decode_sequence(encode_sequence(
        source, tmp_path / "mixed-layout.d4d", codec="draco"
    ))

    assert [len(frame.geometry.positions) for frame in decoded] == [4, 6]
    assert decoded.topology is TopologyMode.UNKNOWN
    assert decoded.has_constant_vertex_count is False
    assert decoded.has_vertex_correspondence is None
