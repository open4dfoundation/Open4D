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
from open4d.codec import _klt, _n4mc, _qndf, _temporal, _o4d
from open4d.codec._o4d_format import pack_o4d, unpack_o4d


def sequence():
    return Sequence(MemoryFrameProvider([
        Frame(i, i / 30, TriangleMesh([[0., 0, 0], [1, 0, 0], [0, 1, 0]], [[0, 1, 2]]))
        for i in range(2)
    ]))


def research_artifact(tmp_path, codec, payload, *, normalization=None):
    native = tmp_path / "native"
    native.mkdir()
    metadata = {"version": 1, "codec": codec, "native": {"profile": f"{codec}/1"},
                "frames": [{"frame_index": 0, "timestamp": 0}]}
    if codec == "klt":
        metadata["normalization"] = normalization or {"center": [0, 0, 0], "scale": 1}
        (native / "decoder_context.pt").write_bytes(payload)
        (native / "000000_quantized_indices.zst").write_bytes(b"indices")
        (native / "000000_quantized_metadata.npz").write_bytes(b"quantizer")
    else:
        (native / "frame_000000.pt").write_bytes(payload)
    (native / "metadata.json").write_text(json.dumps(metadata))
    return pack_o4d(native, tmp_path / "take.o4d")


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


def test_o4d_preserves_neighbor_temporary_file(tmp_path, monkeypatch):
    destination = tmp_path / "take.o4d"
    neighbor = tmp_path / ".take.o4d.tmp"
    neighbor.write_bytes(b"unrelated user file")

    def run(command, label):
        path = next(arg.split("=", 1)[1] for arg in command if arg.startswith("--compressed="))
        Path(path).write_bytes(b"native payload")

    monkeypatch.setattr(_o4d, "_run", run)
    _o4d.VDMC_CODEC.encode(sequence(), destination, encoder=sys.executable)
    assert neighbor.read_bytes() == b"unrelated user file"


def test_klt_closes_decoded_provider_if_output_count_is_wrong(tmp_path, monkeypatch):
    artifact = research_artifact(tmp_path, "klt", b"context")
    decoded = sequence()
    monkeypatch.setattr(_klt, "_backend", lambda: SimpleNamespace(decode_compressed=lambda *args: None))
    monkeypatch.setattr(_klt, "open_sequence", lambda *args: decoded)
    with pytest.raises(CodecError, match="expected 1"):
        _klt.KLT_CODEC.decode(artifact)
    assert decoded.closed


def test_temporal_delta_rejects_broadcasted_displacements(tmp_path):
    from open4d.codec._temporal import TEMPORAL_DELTA_CODEC

    native = tmp_path / "native"
    native.mkdir()
    manifest = {"version": 1, "codec": "temporal-delta", "native": {"profile": "temporal-delta/1"},
                "quantization_bits": 16,
                "frames": [{"frame_index": i, "timestamp": i / 30} for i in range(2)]}
    np.savez(native / "sequence.npz", reference=sequence()[0].geometry.positions,
             triangles=np.array([[0, 1, 2]], dtype=np.uint32),
             displacement=np.zeros((2, 1, 3), dtype=np.int16), scale=1.0)
    (native / "metadata.json").write_text(json.dumps(manifest))
    artifact = pack_o4d(native, tmp_path / "broken.o4d")
    with pytest.raises(CodecError, match="shape|displacement"):
        TEMPORAL_DELTA_CODEC.decode(artifact)


@pytest.mark.parametrize("codec", [NumPyZipCodec(), _klt.KLT_CODEC, _n4mc.N4MC_CODEC,
                                   _qndf.QNDF_CODEC, _o4d.VDMC_CODEC])
@pytest.mark.parametrize("manifest", [b"[]", b"\xff"])
def test_codec_detection_returns_false_for_malformed_manifest(tmp_path, codec, manifest):
    artifact = tmp_path / "invalid.zip"
    with ZipFile(artifact, "w") as archive:
        archive.writestr("manifest.json", manifest)
    assert codec.can_decode(artifact) is False


@pytest.mark.parametrize("payload", [b"PK\x03\x04legacy", b"VMESH\x00\x01\x00", b"VMESH\x00\x02\x00"])
def test_codec_detection_returns_false_for_foreign_o4d(tmp_path, payload):
    from open4d.codec._api import _CODECS, _codec
    artifact = tmp_path / "foreign.o4d"
    artifact.write_bytes(payload)
    assert all(codec.can_decode(artifact) is False for codec in _CODECS.values())
    with pytest.raises(CodecError, match="invalid O4D"):
        _codec(None, artifact)


@pytest.mark.parametrize("frame", [{"frame_index": -1, "timestamp": 0},
                                  {"frame_index": 0, "timestamp": float("nan")},
                                  {"frame_index": 0, "timestamp": 0, "metadata": []}])
def test_reference_decode_rejects_invalid_frame_metadata_without_reading_arrays(tmp_path, frame, monkeypatch):
    from open4d.codec import _npz
    from open4d.codec.tests.test_o4d_format import records, rewrite
    artifact = NumPyZipCodec().encode(sequence(), tmp_path / "invalid.o4d")
    items = records(artifact)
    manifest = json.loads(items[0][3])
    manifest["sequence"]["frames"][0] = frame
    items[0] = 0, 0, 0, json.dumps(manifest).encode()
    rewrite(artifact, items)
    monkeypatch.setattr(_npz, "_read_array", lambda *args: pytest.fail("invalid metadata read arrays"))
    with pytest.raises(CodecError):
        NumPyZipCodec().decode(artifact)


def test_klt_rejects_broadcast_normalization_before_decoding(tmp_path, monkeypatch):
    artifact = research_artifact(tmp_path, "klt", b"context",
                                 normalization={"center": 0, "scale": 1})
    monkeypatch.setattr(_klt, "open_sequence", lambda *args: sequence()[:1])
    monkeypatch.setattr(_klt, "_backend", lambda: SimpleNamespace(decode_compressed=lambda *args: None))
    with pytest.raises(CodecError, match="normalization"):
        _klt.KLT_CODEC.decode(artifact)


@pytest.mark.parametrize("bounds, bits", [([0, 1], 12), ([[0, 0, 0], [1, 1, 1]], 0),
                                         ([[1, 0, 0], [0, 1, 1]], 12)])
def test_o4d_rejects_invalid_position_normalization(tmp_path, monkeypatch, bounds, bits):
    artifact = tmp_path / "invalid.v4d"
    with ZipFile(artifact, "w") as archive:
        archive.writestr("manifest.json", json.dumps({
            "schema": "open4d.vmesh-sequence/v1", "codec": "vdmc",
            "frames": [{"frame_index": 0, "timestamp": 0}],
            "position_bounds": bounds, "position_bit_depth": bits,
        }))
        archive.writestr("sequence.vmesh", b"stream")
    monkeypatch.setattr(_o4d, "_executable", lambda *args: Path("decoder"))
    monkeypatch.setattr(_o4d, "_run", lambda *args: None)
    monkeypatch.setattr(_o4d, "open_sequence", lambda *args, **kwargs: sequence()[:1])
    with pytest.raises(CodecError, match="position"):
        from open4d.codec import migrate_legacy
        migrate_legacy(artifact, tmp_path / "converted.o4d")


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
    path = codec.encode(sequence(), tmp_path / "take.o4d", coarse_size=3,
                        num_subdiv=0, pe_dim=2, hidden_dim=4, num_layers=3,
                        epochs=1, batch_size=16, input_scale=input_scale,
                        output_scale=output_scale, device="cpu")
    native = tmp_path / "unpacked"
    unpack_o4d(path, native)
    assert not any(name.name == "model.pt" for name in native.iterdir())
    context = torch.load(native / "frame_000000.pt", weights_only=True)
    assert "schema" not in context
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
        destination = tmp_path / "legacy"
        destination.mkdir()
        _qndf._extract_legacy_qndf(path, destination, int8=True)


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
        "version": 1, "coarse_vertices": coarse,
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
    path = research_artifact(tmp_path, "qndf", _qndf._torch_bytes(torch, context))
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
        _qndf.QNDF_CODEC.encode(sequence(), tmp_path / "invalid.o4d", **options)
