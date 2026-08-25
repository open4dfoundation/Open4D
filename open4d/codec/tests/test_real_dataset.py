import os
from pathlib import Path

import numpy as np
import pytest

from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh
from open4d.codec import available_codecs, decode_sequence, encode_sequence
from open4d.io import available_formats, open_sequence, write_sequence

pytestmark = [pytest.mark.cpu, pytest.mark.slow]


def rafa_dataset() -> Path:
    configured = os.environ.get("OPEN4D_RAFA_DATASET")
    if configured:
        return Path(configured)
    root = Path(__file__).resolve().parents[3] / "4d_files"
    named = root / "Rafa_Approves_hd_4k"
    return named if named.is_dir() else root


def test_rafa_obj_frames_encode_and_decode(tmp_path):
    dataset = rafa_dataset()
    if not dataset.is_dir() or not next(dataset.glob("*.obj"), None):
        pytest.skip("Rafa_Approves_hd_4k is not available")

    source = open_sequence(dataset, fps=30)
    sample = source[:2]
    artifact = encode_sequence(sample, tmp_path / "rafa.o4d")
    decoded = decode_sequence(artifact)

    assert len(source) >= 2
    assert len(decoded) == 2
    assert artifact.stat().st_size > 100_000
    for index in range(2):
        np.testing.assert_array_equal(
            decoded[index].geometry.positions, source[index].geometry.positions
        )
        np.testing.assert_array_equal(
            decoded[index].geometry.triangles, source[index].geometry.triangles
        )
    decoded.close()


def test_real_draco_round_trip_on_rafa(tmp_path):
    pytest.importorskip("DracoPy")
    dataset = rafa_dataset()
    if not dataset.is_dir() or not next(dataset.glob("*.obj"), None):
        pytest.skip("Rafa_Approves_hd_4k is not available")

    source = open_sequence(dataset, fps=30)[:2]
    artifact = encode_sequence(source, tmp_path / "rafa.d4d", codec="draco")
    decoded = decode_sequence(artifact)

    assert len(decoded) == len(source)
    for expected, actual in zip(source, decoded, strict=True):
        np.testing.assert_array_equal(
            actual.geometry.triangles, expected.geometry.triangles
        )
        error = actual.geometry.positions - expected.geometry.positions
        assert np.sqrt(np.mean(error * error)) < 5e-5
    decoded.close()


def test_every_input_codec_and_output_combination(tmp_path):
    pytest.importorskip("trimesh")
    pytest.importorskip("DracoPy")
    positions = np.array(
        [[0.0, 0, 0], [1.25, 0, 0], [0, 2.5, 0], [0, 0, 3.75]],
        dtype=np.float32,
    )
    triangles = np.array(
        [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]], dtype=np.uint32
    )
    canonical = Sequence(MemoryFrameProvider([
        Frame(0, 0, TriangleMesh(positions, triangles))
    ]))

    def signature(value):
        mesh = value[0].geometry
        vertices = mesh.positions.astype(np.float64)
        a, b, c = vertices[mesh.triangles].transpose(1, 0, 2)
        return (
            vertices.min(0), vertices.max(0),
            np.linalg.norm(np.cross(b - a, c - a), axis=1).sum() / 2,
        )

    expected = signature(canonical)
    for input_info in available_formats():
        source = write_sequence(
            canonical, tmp_path / f"in.{input_info.id}", allow_lossy=True
        )
        for codec_info in available_codecs():
            if codec_info.backend not in {"python", "python-binding"}:
                continue
            artifact = encode_sequence(
                source,
                tmp_path / f"{input_info.id}-{codec_info.id}{codec_info.suffixes[0]}",
                codec=codec_info.id,
            )
            decoded = decode_sequence(artifact)
            for output_info in available_formats():
                output = write_sequence(
                    decoded,
                    tmp_path / f"{input_info.id}-{codec_info.id}-{output_info.id}.{output_info.id}",
                    allow_lossy=True,
                )
                actual = signature(open_sequence(output))
                np.testing.assert_allclose(actual[0], expected[0], atol=1e-4)
                np.testing.assert_allclose(actual[1], expected[1], atol=1e-4)
                assert actual[2] == pytest.approx(expected[2], rel=1e-4)
            decoded.close()
