from __future__ import annotations

import json
from io import BytesIO
from pathlib import Path
import sys
from types import SimpleNamespace
from zipfile import ZipFile

import numpy as np
import pytest

from open4d import Frame, MemoryFrameProvider, Sequence, TriangleMesh
from open4d.codec import CodecError
from open4d.codec._npz import NumPyZipCodec
from open4d.codec import _klt, _n4mc, _qndf, _temporal, _vmesh


def sequence():
    return Sequence(MemoryFrameProvider([
        Frame(i, i / 30, TriangleMesh([[0., 0, 0], [1, 0, 0], [0, 1, 0]], [[0, 1, 2]]))
        for i in range(2)
    ]))


def test_reference_encode_does_not_replace_a_file_created_during_encoding(tmp_path, monkeypatch):
    codec = NumPyZipCodec()
    destination = tmp_path / "take.o4d"
    original = codec.pack

    def pack(payload):
        destination.write_bytes(b"created by another writer")
        return original(payload)

    monkeypatch.setattr(codec, "pack", pack)
    with pytest.raises(FileExistsError):
        codec.encode(sequence(), destination)
    assert destination.read_bytes() == b"created by another writer"


def test_vmesh_preserves_neighbor_temporary_file(tmp_path, monkeypatch):
    destination = tmp_path / "take.v4d"
    neighbor = tmp_path / ".take.v4d.tmp"
    neighbor.write_bytes(b"unrelated user file")

    def run(command, label):
        path = next(arg.split("=", 1)[1] for arg in command if arg.startswith("--compressed="))
        Path(path).write_bytes(b"native payload")

    monkeypatch.setattr(_vmesh, "_run", run)
    _vmesh.VDMC_CODEC.encode(sequence(), destination, encoder=sys.executable)
    assert neighbor.read_bytes() == b"unrelated user file"


def test_klt_closes_decoded_provider_if_output_count_is_wrong(tmp_path, monkeypatch):
    artifact = tmp_path / "take.k4d"
    with ZipFile(artifact, "w") as archive:
        archive.writestr("manifest.json", json.dumps({
            "schema": "open4d.klt-sequence/v1", "codec": "klt",
            "normalization": {"center": [0, 0, 0], "scale": 1},
            "frames": [{"frame_index": 0, "timestamp": 0}],
        }))
    decoded = sequence()
    monkeypatch.setattr(_klt, "_backend", lambda: SimpleNamespace(decode_compressed=lambda *args: None))
    monkeypatch.setattr(_klt, "open_sequence", lambda *args: decoded)
    with pytest.raises(CodecError, match="expected 1"):
        _klt.KLT_CODEC.decode(artifact)
    assert decoded.closed


def test_temporal_delta_rejects_broadcasted_displacements(tmp_path):
    from open4d.codec._temporal import TEMPORAL_DELTA_CODEC

    artifact = tmp_path / "broken.td4d"
    manifest = {"schema": "open4d.temporal-delta-sequence/v1", "codec": "temporal-delta",
                "quantization_bits": 16,
                "frames": [{"frame_index": i, "timestamp": i / 30} for i in range(2)]}
    with artifact.open("wb") as stream:
        np.savez(stream, manifest=np.frombuffer(json.dumps(manifest).encode(), dtype=np.uint8),
                 reference=sequence()[0].geometry.positions, triangles=np.array([[0, 1, 2]], dtype=np.uint32),
                 displacement=np.zeros((2, 1, 3), dtype=np.int16), scale=1.0)
    with pytest.raises(CodecError, match="shape|displacement"):
        TEMPORAL_DELTA_CODEC.decode(artifact)


@pytest.mark.parametrize("codec", [NumPyZipCodec(), _klt.KLT_CODEC, _n4mc.N4MC_CODEC,
                                   _qndf.QNDF_CODEC, _vmesh.VDMC_CODEC])
@pytest.mark.parametrize("manifest", [b"[]", b"\xff"])
def test_codec_detection_returns_false_for_malformed_manifest(tmp_path, codec, manifest):
    artifact = tmp_path / "invalid.zip"
    with ZipFile(artifact, "w") as archive:
        archive.writestr("manifest.json", manifest)
    assert codec.can_decode(artifact) is False


@pytest.mark.parametrize("frame", [{"frame_index": -1, "timestamp": 0},
                                  {"frame_index": 0, "timestamp": float("nan")},
                                  {"frame_index": 0, "timestamp": 0, "metadata": []}])
def test_reference_decode_rejects_invalid_frame_metadata_without_reading_arrays(tmp_path, frame):
    artifact = tmp_path / "invalid.o4d"
    with ZipFile(artifact, "w") as archive:
        archive.writestr("manifest.json", json.dumps({
            "schema": "open4d.numpy-zip/v1", "codec": "npz", "frames": [frame],
        }))
    with pytest.raises(CodecError):
        NumPyZipCodec().decode(artifact)


def test_klt_rejects_broadcast_normalization_before_decoding(tmp_path, monkeypatch):
    artifact = tmp_path / "invalid.k4d"
    with ZipFile(artifact, "w") as archive:
        archive.writestr("manifest.json", json.dumps({
            "schema": "open4d.klt-sequence/v1", "codec": "klt",
            "frames": [{"frame_index": 0, "timestamp": 0}],
            "normalization": {"center": 0, "scale": 1},
        }))
    monkeypatch.setattr(_klt, "open_sequence", lambda *args: sequence()[:1])
    monkeypatch.setattr(_klt, "_backend", lambda: SimpleNamespace(decode_compressed=lambda *args: None))
    with pytest.raises(CodecError, match="normalization"):
        _klt.KLT_CODEC.decode(artifact)


@pytest.mark.parametrize("bounds, bits", [([0, 1], 12), ([[0, 0, 0], [1, 1, 1]], 0),
                                         ([[1, 0, 0], [0, 1, 1]], 12)])
def test_vmesh_rejects_invalid_position_normalization(tmp_path, monkeypatch, bounds, bits):
    artifact = tmp_path / "invalid.v4d"
    with ZipFile(artifact, "w") as archive:
        archive.writestr("manifest.json", json.dumps({
            "schema": "open4d.vmesh-sequence/v1", "codec": "vdmc",
            "frames": [{"frame_index": 0, "timestamp": 0}],
            "position_bounds": bounds, "position_bit_depth": bits,
        }))
        archive.writestr("sequence.vmesh", b"stream")
    monkeypatch.setattr(_vmesh, "_executable", lambda *args: Path("decoder"))
    monkeypatch.setattr(_vmesh, "_run", lambda *args: None)
    monkeypatch.setattr(_vmesh, "open_sequence", lambda *args, **kwargs: sequence()[:1])
    with pytest.raises(CodecError, match="position"):
        _vmesh.VDMC_CODEC.decode(artifact)


@pytest.mark.torch
@pytest.mark.parametrize("input_scale, output_scale", [(1, 1), (-1, -2), (0, 1)])
def test_qndf_int8_stores_weights_and_restores_the_same_predictions(
    tmp_path, monkeypatch, input_scale, output_scale,
):
    torch = pytest.importorskip("torch")
    from open4d.codec._research import research_module

    models = research_module("qndf.compress")
    preprocessing = SimpleNamespace(build_pair=lambda positions, faces, *args: (
        positions, faces, positions, {"scale": 1.0, "bbox_min": [0, 0, 0]},
    ))
    monkeypatch.setattr(_qndf, "_backend", lambda: (torch, models, preprocessing))
    quantized_models = []
    quantize = torch.ao.quantization.quantize_dynamic

    def capture(*args, **kwargs):
        model = quantize(*args, **kwargs)
        quantized_models.append(model)
        return model

    monkeypatch.setattr(torch.ao.quantization, "quantize_dynamic", capture)
    codec = _qndf.QNDF_INT8_CODEC
    path = codec.encode(sequence(), tmp_path / "take.qi4d", coarse_size=3,
                        num_subdiv=0, pe_dim=2, hidden_dim=4, num_layers=3,
                        epochs=1, batch_size=16, input_scale=input_scale,
                        output_scale=output_scale, device="cpu")
    with ZipFile(path) as archive:
        assert not any(name.endswith("/model.pt") for name in archive.namelist())
        context = torch.load(BytesIO(archive.read("frames/000000/context.pt")), weights_only=True)
    coarse = context["coarse_vertices"]
    encoded, normalized, _, _ = _qndf._inputs(torch, models, coarse, 2, input_scale)
    graph = models.MeshDataset(encoded, normalized, context["coarse_faces"], torch.zeros_like(coarse), progress=False)
    with torch.inference_mode():
        expected = (coarse + quantized_models[0](encoded, graph.neighbors, graph.edge_wts) / output_scale).numpy()
    with codec.decode(path) as decoded:
        np.testing.assert_allclose(decoded[0].geometry.positions, expected, rtol=1e-6, atol=1e-6)


@pytest.mark.torch
def test_qndf_int8_rejects_executable_artifact_version(tmp_path, monkeypatch):
    pytest.importorskip("torch")
    path = tmp_path / "old.qi4d"
    with ZipFile(path, "w") as archive:
        archive.writestr("manifest.json", json.dumps({
            "schema": "open4d.qndf-int8-sequence/v1", "codec": "qndf-int8",
            "frames": [{"frame_index": 0, "timestamp": 0}],
        }))
    with pytest.raises(CodecError, match="executable|re-encode"):
        _qndf.QNDF_INT8_CODEC.decode(path)


@pytest.mark.torch
@pytest.mark.parametrize("field", [
    "context", "normalization", "normalization.bbox_min", "normalization.scale",
    "input_mean", "input_std", "input_std.zero", "output_scale", "input_scale",
    "pe_dim", "hidden_dim", "num_layers", "coarse_vertices", "coarse_vertices.nan",
    "coarse_faces", "coarse_faces.index",
])
def test_qndf_rejects_malformed_frame_context(tmp_path, monkeypatch, field):
    torch = pytest.importorskip("torch")
    from open4d.codec._research import research_module

    models = research_module("qndf.compress")
    monkeypatch.setattr(_qndf, "_backend", lambda: (torch, models, None))
    coarse = torch.tensor([[0., 0, 0], [1., 0, 0], [0., 1, 0]])
    context = {
        "schema": "open4d.qndf/v1", "coarse_vertices": coarse,
        "coarse_faces": torch.tensor([[0, 1, 2]]),
        "input_mean": torch.zeros((1, 3)), "input_std": torch.ones((1, 3)),
        "input_scale": 1., "output_scale": 1., "pe_dim": 2, "hidden_dim": 4, "num_layers": 1,
        "normalization": {"scale": 1., "bbox_min": [0., 0., 0.]},
        "model_state_dict": models.MLP(6, 4, 3, 1).state_dict(),
    }
    bad_values = {
        "context": [], "normalization": [], "normalization.bbox_min": 0.,
        "normalization.scale": 0., "input_mean": torch.tensor(0.),
        "input_std": torch.tensor(1.), "input_std.zero": torch.zeros((1, 3)),
        "output_scale": 0., "input_scale": float("inf"), "pe_dim": 3,
        "hidden_dim": 4.5, "num_layers": True, "coarse_vertices": coarse.to(torch.int64),
        "coarse_vertices.nan": coarse.clone().fill_(float("nan")),
        "coarse_faces": torch.tensor([[0., 1, 2]]), "coarse_faces.index": torch.tensor([[0, 1, 3]]),
    }
    if field == "context":
        context = bad_values[field]
    elif field.startswith("normalization."):
        context["normalization"][field.split(".")[1]] = bad_values[field]
    else:
        context[field.split(".")[0]] = bad_values[field]
    path = tmp_path / "invalid.q4d"
    with ZipFile(path, "w") as archive:
        archive.writestr("manifest.json", json.dumps({
            "schema": "open4d.qndf-sequence/v1", "codec": "qndf",
            "frames": [{"frame_index": 0, "timestamp": 0}],
        }))
        archive.writestr("frames/000000/context.pt", _qndf._torch_bytes(torch, context))
    with pytest.raises(CodecError, match="context"):
        _qndf.QNDF_CODEC.decode(path, device="cpu")


@pytest.mark.parametrize("options", [
    {"output_scale": 0}, {"input_scale": float("nan")}, {"output_scale": float("inf")},
    {"hidden_dim": 4.5}, {"num_layers": True}, {"num_subdiv": -1},
])
def test_qndf_invalid_options_fail_before_loading_backend(tmp_path, monkeypatch, options):
    def unexpected_backend():
        pytest.fail("invalid codec options reached the research backend")

    monkeypatch.setattr(_qndf, "_backend", unexpected_backend)
    with pytest.raises(ValueError):
        _qndf.QNDF_CODEC.encode(sequence(), tmp_path / "invalid.q4d", **options)
