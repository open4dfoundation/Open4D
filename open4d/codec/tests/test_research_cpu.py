from __future__ import annotations

import numpy as np
import pytest
from scipy.spatial import cKDTree

from open4d import Frame, MemoryFrameProvider, Sequence, TopologyMode, TriangleMesh
from open4d.codec import decode_sequence, encode_sequence

pytestmark = [pytest.mark.open3d, pytest.mark.torch, pytest.mark.slow]


def moving_cube() -> Sequence:
    positions = np.array([
        [-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1],
        [-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1],
    ], dtype=np.float32)
    triangles = np.array([
        [0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7],
        [0, 1, 5], [0, 5, 4], [1, 2, 6], [1, 6, 5],
        [2, 3, 7], [2, 7, 6], [3, 0, 4], [3, 4, 7],
    ], dtype=np.uint32)
    identities = ((41, 1.25), (73, 2.75))
    frames = [
        Frame(frame_index, timestamp, TriangleMesh(
            positions + [ordinal * .1, 0, ordinal * .05], triangles
        ), metadata={"camera": f"left-{ordinal}"})
        for ordinal, (frame_index, timestamp) in enumerate(identities)
    ]
    return Sequence(MemoryFrameProvider(
        frames, metadata={"capture": "synthetic-moving-cube"},
        topology=TopologyMode.FIXED,
        has_constant_vertex_count=True, has_vertex_correspondence=True,
    ))


def surface_rms_fraction(expected, actual, seed):
    pcu = pytest.importorskip("point_cloud_utils")
    clouds = []
    for mesh, value in ((expected, seed), (actual, seed + 1)):
        vertices = np.asarray(mesh.positions, dtype=np.float64)
        faces = np.asarray(mesh.triangles, dtype=np.int32)
        indices, barycentric = pcu.sample_mesh_random(
            vertices, faces, 2000, random_seed=value
        )
        clouds.append(np.einsum(
            "nij,ni->nj", vertices[faces[indices]], barycentric
        ))
    left, right = clouds
    distances = np.concatenate((
        cKDTree(right).query(left, workers=-1)[0],
        cKDTree(left).query(right, workers=-1)[0],
    ))
    return np.sqrt(np.mean(distances ** 2)) / np.linalg.norm(np.ptp(left, axis=0))


@pytest.mark.parametrize("codec,suffix,options", (
    ("klt", ".k4d", {
        "resolution": 7, "num_components": 4, "block_size": 2,
        "k_total": 32, "training_frames": (0,),
    }),
    ("n4mc", ".n4d", {
        "resolution": 7, "epochs": 30, "hidden_channels": (4, 8),
        "latent_channels": 4, "learning_rate": 3e-3, "device": "cpu",
    }),
    ("qndf", ".q4d", {
        "coarse_size": 12, "num_subdiv": 0, "epochs": 2,
        "hidden_dim": 8, "num_layers": 2, "batch_size": 32, "device": "cpu",
    }),
    ("qndf-int8", ".qi4d", {
        "coarse_size": 12, "num_subdiv": 0, "epochs": 2,
        "hidden_dim": 8, "num_layers": 2, "batch_size": 32, "device": "cpu",
    }),
))
def test_research_codec_cpu_encode_and_fresh_decode(
    tmp_path, codec, suffix, options
):
    pytest.importorskip("torch")
    pytest.importorskip("trimesh")
    pytest.importorskip("open3d")
    source = moving_cube()
    artifact = encode_sequence(
        source, tmp_path / f"cube{suffix}", codec=codec, **options
    )
    first = decode_sequence(artifact, device="cpu")
    second = decode_sequence(artifact, device="cpu")

    assert artifact.stat().st_size > 0
    assert len(first) == len(second) == len(source)
    assert first.metadata == second.metadata == source.metadata
    quality_limits = {"klt": .2, "n4mc": .45, "qndf": .15, "qndf-int8": .15}
    for ordinal, (expected, left, right) in enumerate(
        zip(source, first, second, strict=True)
    ):
        assert len(left.geometry.positions) and len(left.geometry.triangles)
        assert left.frame_index == right.frame_index == expected.frame_index
        assert left.timestamp == right.timestamp == expected.timestamp
        assert left.metadata == right.metadata == expected.metadata
        np.testing.assert_array_equal(left.geometry.positions, right.geometry.positions)
        np.testing.assert_array_equal(left.geometry.triangles, right.geometry.triangles)
        assert surface_rms_fraction(
            expected.geometry, left.geometry, 200 + ordinal * 2
        ) < quality_limits[codec]
    first.close()
    second.close()
