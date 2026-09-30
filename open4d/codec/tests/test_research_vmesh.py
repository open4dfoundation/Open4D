from io import BytesIO
import json
from pathlib import Path
from zipfile import ZipFile

import numpy as np
import pytest

from open4d.codec import CodecError, inspect_vmesh, pack_vmesh, unpack_vmesh
from open4d.codec import _klt, _qndf
from open4d.codec._research import research_module


def klt_backend():
    pytest.importorskip("torch")
    pytest.importorskip("zstd")
    pytest.importorskip("zstandard")
    pytest.importorskip("trimesh")
    return research_module("klt.klt")


def klt_context():
    torch = pytest.importorskip("torch")
    return {"version": 1, "basis": torch.eye(8)[:2], "mean": torch.zeros((1, 8)),
            "block_size": 2, "num_components": 2}


def test_klt_real_vmesh_round_trip_preserves_sequence_and_native_context(tmp_path):
    klt_backend()
    pytest.importorskip("point_cloud_utils")
    torch = pytest.importorskip("torch")
    from open4d.codec.tests.test_research_cpu import moving_cube
    source = moving_cube()
    artifact = _klt.KLT_CODEC.encode(source, tmp_path / "take.vmesh", resolution=7,
                                    num_components=4, block_size=2, k_total=32)
    manifest = inspect_vmesh(artifact)
    assert manifest["codec"] == "klt" and len(manifest["files"]) == 5
    native = tmp_path / "native"
    unpack_vmesh(artifact, native)
    assert "schema" not in torch.load(native / "decoder_context.pt", weights_only=True)
    with _klt.KLT_CODEC.decode(artifact, device="cpu") as first, _klt.KLT_CODEC.decode(artifact, device="cpu") as second:
        assert first.metadata == source.metadata
        assert first.timestamps == source.timestamps
        for expected, left, right in zip(source, first, second, strict=True):
            assert len(left.geometry.positions) and len(left.geometry.triangles)
            assert left.frame_index == expected.frame_index and left.metadata == expected.metadata
            np.testing.assert_array_equal(left.geometry.positions, right.geometry.positions)
            np.testing.assert_array_equal(left.geometry.triangles, right.geometry.triangles)


@pytest.mark.parametrize("field", ("version", "schema", "basis", "basis.nan", "mean", "block_size", "num_components"))
def test_klt_context_rejects_unsafe_shapes_and_values(field):
    backend = klt_backend()
    torch = pytest.importorskip("torch")
    context = klt_context()
    changes = {"version": True, "schema": "open4d.klt/v1", "basis": torch.eye(2),
               "basis.nan": torch.eye(8)[:2].fill_(float("nan")), "mean": torch.zeros(8),
               "block_size": 0, "num_components": 2 ** 32}
    context[field.split(".")[0]] = changes[field]
    with pytest.raises(ValueError, match="KLT"):
        backend.validate_decoder_context(context)


def quantizer(tmp_path):
    backend = klt_backend()
    torch = pytest.importorskip("torch")
    prefix = str(tmp_path / "000000_quantized")
    backend.save_quantized_coeffs(torch.zeros((8, 2), dtype=torch.long),
                                 [torch.tensor([-1., 1.]), torch.tensor([0.])],
                                 {1: 0.}, prefix, (4, 4, 4))
    return backend, prefix


@pytest.mark.parametrize("field,value", (
    ("indices_shape", np.array([2 ** 50, 2], dtype=np.int64)),
    ("indices_dtype", np.array("<f8")),
    ("volume_shape", np.array([4, 4, 5], dtype=np.int64)),
    ("volume_shape", np.array([257, 257, 257], dtype=np.int64)),
    ("bin_centers_0", np.array([float("nan")], dtype=np.float32)),
    ("fixed_indices", np.array([2], dtype=np.int64)),
    ("fixed_values", np.array([float("inf")], dtype=np.float32)),
))
def test_klt_quantizer_rejects_unsafe_array_metadata(tmp_path, field, value):
    backend, prefix = quantizer(tmp_path)
    with np.load(f"{prefix}_metadata.npz", allow_pickle=False) as archive:
        metadata = {name: archive[name] for name in archive.files}
    metadata[field] = value
    np.savez_compressed(f"{prefix}_metadata.npz", **metadata)
    with pytest.raises(ValueError, match="KLT"):
        backend.load_quantized_coeffs(prefix, "cpu", decoder=klt_context())


def test_klt_quantizer_checks_npy_size_before_numpy_allocates(tmp_path):
    backend, prefix = quantizer(tmp_path)
    stream = BytesIO()
    np.lib.format.write_array_header_1_0(stream, {
        "descr": "<f4", "fortran_order": False, "shape": (2 ** 50,),
    })
    with ZipFile(f"{prefix}_metadata.npz", "w") as archive:
        archive.writestr("bin_centers_0.npy", stream.getvalue())
    with pytest.raises(ValueError, match="NPY payload"):
        backend.load_quantized_coeffs(prefix, "cpu")


@pytest.mark.parametrize("mode", ("oversized", "unknown-size", "trailing", "bad-index"))
def test_klt_coefficients_reject_size_bombs_extra_frames_and_invalid_indices(tmp_path, mode):
    backend, prefix = quantizer(tmp_path)
    zstandard = pytest.importorskip("zstandard")
    data = bytes(16)
    if mode in ("oversized", "unknown-size"):
        data = bytes(65536)
    elif mode == "bad-index":
        data = bytes([255]) + data[1:]
    compressed = zstandard.ZstdCompressor(write_content_size=mode != "unknown-size").compress(data)
    if mode == "trailing":
        compressed += zstandard.ZstdCompressor().compress(bytes(16))
    Path(f"{prefix}_indices.zst").write_bytes(compressed)
    with pytest.raises(ValueError, match="KLT"):
        backend.load_quantized_coeffs(prefix, "cpu", decoder=klt_context())


@pytest.mark.parametrize("codec,suffix", ((_klt.KLT_CODEC, ".k4d"), (_qndf.QNDF_CODEC, ".q4d"),
                                          (_qndf.QNDF_INT8_CODEC, ".qi4d")))
def test_research_codec_normal_decode_rejects_retired_extensions_before_loading_backend(tmp_path, monkeypatch, codec, suffix):
    path = tmp_path / f"old{suffix}"
    path.write_bytes(b"retired archive")
    module = _klt if codec.id == "klt" else _qndf
    monkeypatch.setattr(module, "_backend", lambda: pytest.fail("legacy input loaded a backend"))
    assert codec.suffixes == (".vmesh",)
    assert not codec.can_decode(path)
    with pytest.raises(CodecError, match="migrate"):
        codec.decode(path)


def test_klt_explicit_migration_removes_old_archive_and_context_schema(tmp_path):
    backend = klt_backend()
    torch = pytest.importorskip("torch")
    _, prefix = quantizer(tmp_path)
    context = klt_context()
    context.pop("version")
    context["schema"] = "open4d.klt/v1"
    stream = BytesIO()
    torch.save(context, stream)
    source = tmp_path / "old.k4d"
    with ZipFile(source, "w") as archive:
        archive.writestr("manifest.json", json.dumps({
            "schema": "open4d.klt-sequence/v1", "codec": "klt",
            "normalization": {"center": [10, 20, 30], "scale": 2},
            "frames": [{"frame_index": 73, "timestamp": 2.75, "metadata": {"camera": "left"}}],
        }))
        archive.writestr("decoder_context.pt", stream.getvalue())
        for suffix in ("indices.zst", "metadata.npz"):
            archive.write(f"{prefix}_{suffix}", f"000000_quantized_{suffix}")
    native = tmp_path / "migrated"
    native.mkdir()
    _klt._extract_legacy_klt(source, native)
    artifact = pack_vmesh(native, tmp_path / "take.vmesh")
    manifest = inspect_vmesh(artifact)
    assert artifact.read_bytes().startswith(b"VMESH\x00\x01\x00")
    assert "open4d." not in json.dumps(manifest)
    assert all(record["name"] != "manifest.json" for record in manifest["files"])
    result = torch.load(native / "decoder_context.pt", weights_only=True)
    backend.validate_decoder_context(result)
    torch.testing.assert_close(result["basis"], context["basis"])
    assert manifest["sequence"]["frames"][0]["frame_index"] == 73


@pytest.mark.parametrize("int8", (False, True))
def test_qndf_explicit_migration_keeps_native_predictions_without_open4d_schema(tmp_path, int8):
    torch = pytest.importorskip("torch")
    models = research_module("qndf.compress")
    codec, version = ("qndf-int8", 2) if int8 else ("qndf", 1)
    model = models.MLP(6, 4, 3, 3).eval()
    context = {
        "schema": f"open4d.{codec}/v{version}",
        "coarse_vertices": torch.tensor([[0., 0, 0], [1., 0, 0], [0., 1, 0]]),
        "coarse_faces": torch.tensor([[0, 1, 2]]), "input_mean": torch.zeros((1, 3)),
        "input_std": torch.ones((1, 3)), "pe_dim": 2, "hidden_dim": 4, "num_layers": 3,
        "input_scale": 1., "output_scale": 1., "normalization": {"scale": 1., "bbox_min": [0, 0, 0]},
    }
    if int8:
        context["quantized_engine"] = _qndf._quantized_engine(torch)
        model = torch.ao.quantization.quantize_dynamic(model, {torch.nn.Linear}, dtype=torch.qint8)
    context["model_state_dict"] = model.state_dict()
    source = tmp_path / "old.zip"
    with ZipFile(source, "w") as archive:
        archive.writestr("manifest.json", json.dumps({"schema": f"open4d.{codec}-sequence/v{version}",
                          "codec": codec, "frames": [{"frame_index": 41, "timestamp": 1.25}]}))
        archive.writestr("frames/000000/context.pt", _qndf._torch_bytes(torch, context))
    native = tmp_path / "migrated"
    native.mkdir()
    _qndf._extract_legacy_qndf(source, native, int8=int8)
    artifact = pack_vmesh(native, tmp_path / "take.vmesh")
    assert "open4d." not in json.dumps(inspect_vmesh(artifact))
    inputs = context["coarse_vertices"]
    encoded = models.PE(2)(inputs)
    graph = models.MeshDataset(encoded, inputs, context["coarse_faces"], torch.zeros_like(inputs), progress=False)
    with torch.inference_mode():
        expected = inputs + model(encoded, graph.neighbors, graph.edge_wts)
    implementation = _qndf.QNDF_INT8_CODEC if int8 else _qndf.QNDF_CODEC
    with implementation.decode(artifact, device="cpu") as decoded:
        np.testing.assert_array_equal(decoded[0].geometry.positions, expected.numpy())
        assert decoded[0].frame_index == 41 and decoded[0].timestamp == 1.25
    recovered = tmp_path / "recovered"
    unpack_vmesh(artifact, recovered)
    assert "schema" not in torch.load(recovered / "frame_000000.pt", weights_only=True)
