from __future__ import annotations

import os
from pathlib import Path

import numpy as np
import pytest
from scipy.spatial import cKDTree

from open4d import MemoryFrameProvider, Sequence
from open4d.codec import decode_sequence, encode_sequence
from open4d.io import open_sequence, write_sequence

pytestmark = [pytest.mark.gpu, pytest.mark.slow]


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


@pytest.mark.parametrize("input_format", ("ply", "glb"))
@pytest.mark.parametrize("codec", (
    "klt", "n4mc", "qndf", "qndf-int8", "tvmc", "tsmc",
))
def test_research_codecs_fresh_decode_quality_and_export_real_rafa(
    tmp_path, codec, input_format
):
    if os.environ.get("OPEN4D_TEST_RESEARCH_CODECS") != "1":
        pytest.skip("set OPEN4D_TEST_RESEARCH_CODECS=1 on the codec host")
    torch = pytest.importorskip("torch")
    pytest.importorskip("point_cloud_utils")
    pytest.importorskip("trimesh")
    pytest.importorskip("open3d")
    if not torch.cuda.is_available():
        pytest.skip("CUDA is required for the real research-codec matrix")
    dataset = Path(os.environ["OPEN4D_RAFA_DATASET"])
    opened = open_sequence(dataset, fps=30)
    source = Sequence(MemoryFrameProvider(tuple(opened[:2])))
    input_path = write_sequence(
        source, tmp_path / f"input-{input_format}", format=input_format
    )
    options = {
        "klt": dict(
            resolution=15, num_components=8, block_size=4,
            k_total=256, training_frames=(0,),
        ),
        "n4mc": dict(
            resolution=15, epochs=20, hidden_channels=(8, 16),
            latent_channels=8, learning_rate=1e-3, device="cuda:0",
        ),
        "qndf": dict(
            coarse_size=100, num_subdiv=0, epochs=20, hidden_dim=8,
            num_layers=3, batch_size=256, device="cuda:0",
        ),
        "qndf-int8": dict(
            coarse_size=100, num_subdiv=0, epochs=20, hidden_dim=8,
            num_layers=3, batch_size=256, device="cuda:0",
        ),
        "tvmc": dict(face_budget=100, quantization_bits=12),
        "tsmc": dict(face_budget=100, quantization_bits=12, components=3),
    }
    suffix = {"klt": ".k4d", "n4mc": ".n4d", "qndf": ".q4d",
              "qndf-int8": ".qi4d", "tvmc": ".tv4d", "tsmc": ".ts4d"}[codec]
    artifact = encode_sequence(
        input_path, tmp_path / f"rafa-{codec}{suffix}",
        codec=codec, fps=30,
        **options[codec],
    )
    decode_device = "cpu" if codec in {"tvmc", "tsmc"} else "cuda:0"
    first = decode_sequence(artifact, device=decode_device)
    second = decode_sequence(artifact, device=decode_device)
    assert len(first) == len(second) == 2
    for left, right in zip(first, second, strict=True):
        assert len(left.geometry.positions) and len(left.geometry.triangles)
        np.testing.assert_array_equal(left.geometry.positions, right.geometry.positions)
        np.testing.assert_array_equal(left.geometry.triangles, right.geometry.triangles)
    for ordinal, (expected, actual) in enumerate(zip(source, first, strict=True)):
        assert surface_rms_fraction(
            expected.geometry, actual.geometry, 100 + ordinal * 2
        ) < .1
    for output_format in ("obj", "ply", "off", "stl", "gltf", "glb"):
        exported = write_sequence(
            first, tmp_path / f"{codec}-{input_format}-{output_format}",
            format=output_format,
        )
        assert len(open_sequence(exported)) == 2
    first.close()
    second.close()
    opened.close()
