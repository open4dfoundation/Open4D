from __future__ import annotations

import os
from pathlib import Path

import numpy as np
import pytest

from open4d import MemoryFrameProvider, Sequence
from open4d.codec import decode_sequence, encode_sequence
from open4d.io import open_sequence, write_sequence

pytestmark = [pytest.mark.gpu, pytest.mark.slow]


@pytest.mark.parametrize("input_format", ("ply", "glb"))
@pytest.mark.parametrize("codec", ("klt", "n4mc"))
def test_tsdf_codecs_fresh_decode_and_export_real_rafa(
    tmp_path, codec, input_format
):
    if os.environ.get("OPEN4D_TEST_RESEARCH_CODECS") != "1":
        pytest.skip("set OPEN4D_TEST_RESEARCH_CODECS=1 on the codec host")
    torch = pytest.importorskip("torch")
    pytest.importorskip("point_cloud_utils")
    pytest.importorskip("trimesh")
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
    }
    artifact = encode_sequence(
        input_path, tmp_path / f"rafa-{codec}{'.k4d' if codec == 'klt' else '.n4d'}",
        codec=codec, fps=30,
        **options[codec],
    )
    first = decode_sequence(artifact, device="cuda:0")
    second = decode_sequence(artifact, device="cuda:0")
    assert len(first) == len(second) == 2
    for left, right in zip(first, second, strict=True):
        assert len(left.geometry.positions) and len(left.geometry.triangles)
        np.testing.assert_array_equal(left.geometry.positions, right.geometry.positions)
        np.testing.assert_array_equal(left.geometry.triangles, right.geometry.triangles)
    for output_format in ("obj", "ply", "off", "stl", "gltf", "glb"):
        exported = write_sequence(
            first, tmp_path / f"{codec}-{input_format}-{output_format}",
            format=output_format,
        )
        assert len(open_sequence(exported)) == 2
    first.close()
    second.close()
    opened.close()
