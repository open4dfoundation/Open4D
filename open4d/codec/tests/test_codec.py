from __future__ import annotations

import sys
import subprocess
from pathlib import Path
from types import SimpleNamespace
from zipfile import ZipFile

import numpy as np
import pytest

from open4d import Frame, MemoryFrameProvider, Sequence, TopologyMode, TriangleMesh
from open4d.codec import (
    CodecError,
    available_codecs,
    decode_sequence,
    encode_sequence,
    register_codec,
    VMeshCodec,
)
from open4d.codec._torch import torch_device
from open4d.codec._npz import NumPyZipCodec, REFERENCE_CODECS
from open4d.codec._draco import DRACO_CODEC
from open4d.codec._temporal import TEMPORAL_DELTA_CODEC, TEMPORAL_PCA_CODEC

REFERENCE = NumPyZipCodec()

pytestmark = pytest.mark.cpu


def fake_torch(*, cuda=False, mps=False):
    return SimpleNamespace(
        cuda=SimpleNamespace(is_available=lambda: cuda),
        backends=SimpleNamespace(mps=SimpleNamespace(is_available=lambda: mps)),
        device=lambda value: SimpleNamespace(type=value.split(":")[0]),
    )


def test_neural_device_auto_prefers_cuda_then_metal_then_cpu():
    assert torch_device(fake_torch(cuda=True, mps=True), "auto").type == "cuda"
    assert torch_device(fake_torch(mps=True), None).type == "mps"
    assert torch_device(fake_torch(), "auto").type == "cpu"


def test_neural_device_rejects_unavailable_accelerators():
    with pytest.raises(CodecError, match="Metal/MPS"):
        torch_device(fake_torch(), "mps")
    with pytest.raises(CodecError, match="CUDA"):
        torch_device(fake_torch(), "cuda:0")


def sequence() -> Sequence:
    frames = []
    for index in range(2):
        mesh = TriangleMesh(
            positions=[[float(index), 0, 0], [1.0 + index, 0, 0], [index, 1.0, 0]],
            triangles=[[0, 1, 2]],
            colors=[[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]],
            normals=[[0.0, 0, 1.0]] * 3,
            texture_coordinates=[[0.0, 0], [1.0, 0], [0, 1.0]],
            attributes={"labels": np.array([3, 4, 5], dtype=np.int16)},
        )
        frames.append(Frame(10 + index, index / 24, mesh, {"take": "A"}))
    provider = MemoryFrameProvider(
        frames,
        metadata={"subject": "test"},
        topology=TopologyMode.FIXED,
        has_constant_vertex_count=True,
        has_vertex_correspondence=True,
    )
    return Sequence(provider)


def test_numpy_zip_round_trip_is_lazy_and_preserves_geometry(tmp_path, monkeypatch):
    source = sequence()
    artifact = encode_sequence(source, tmp_path / "take.o4d", codec=REFERENCE)
    calls = []
    import open4d.codec._npz as implementation

    real_read = implementation._read_array

    def recording_read(*args):
        calls.append(args[1])
        return real_read(*args)

    monkeypatch.setattr(implementation, "_read_array", recording_read)
    decoded = decode_sequence(artifact, codec=REFERENCE)

    assert calls == []
    assert len(decoded) == 2
    assert decoded.timestamps == (0.0, 1 / 24)
    assert decoded.metadata["subject"] == "test"
    assert decoded.topology is TopologyMode.FIXED
    actual, expected = decoded[1], source[1]
    assert calls
    assert actual.frame_index == expected.frame_index
    assert actual.metadata == expected.metadata
    for name in ("positions", "triangles", "colors", "normals", "texture_coordinates"):
        np.testing.assert_array_equal(
            getattr(actual.geometry, name), getattr(expected.geometry, name)
        )
    np.testing.assert_array_equal(
        actual.geometry.attributes["labels"], expected.geometry.attributes["labels"]
    )
    decoded.close()


def test_numpy_zip_preserves_reversed_view_timing_policy(tmp_path):
    source = sequence()[::-1]
    artifact = encode_sequence(source, tmp_path / "reversed.o4d", codec=REFERENCE)

    decoded = decode_sequence(artifact, codec=REFERENCE)

    assert decoded.allow_nonmonotonic_timestamps is True
    assert decoded.timestamps == source.timestamps
    decoded.close()


def test_n4mc_component_filter_is_disabled_by_default():
    from open4d.codec._n4mc import _filter_components

    small = SimpleNamespace(faces=np.zeros((4, 3), dtype=np.uint32))
    large = SimpleNamespace(faces=np.zeros((40, 3), dtype=np.uint32))
    mesh = SimpleNamespace(split=lambda **_: [large, small])

    assert _filter_components(mesh, None) is mesh
    assert _filter_components(mesh, 32) is large

    only_small = SimpleNamespace(split=lambda **_: [small])
    with pytest.raises(CodecError, match="removed all"):
        _filter_components(only_small, 32)


def test_encode_refuses_to_overwrite_and_decode_rejects_corruption(tmp_path):
    artifact = encode_sequence(sequence(), tmp_path / "take.o4d", codec=REFERENCE)
    with pytest.raises(FileExistsError):
        encode_sequence(sequence(), artifact, codec=REFERENCE)
    broken = tmp_path / "broken.o4d"
    broken.write_bytes(b"not a zip")
    with pytest.raises(CodecError, match="invalid Open4D artifact"):
        decode_sequence(broken, codec=REFERENCE)


@pytest.mark.parametrize(("suffix", "codec"), (
    (".o4d", REFERENCE), (".d4d", DRACO_CODEC), (".v4d", None),
))
def test_non_object_codec_manifests_are_codec_errors(tmp_path, suffix, codec):
    artifact = tmp_path / f"invalid{suffix}"
    with ZipFile(artifact, "w") as archive:
        archive.writestr("manifest.json", "[]")

    options = {} if codec is None else {"codec": codec}
    with pytest.raises(CodecError, match="manifest|no known codec"):
        decode_sequence(artifact, **options)


def test_encode_failure_removes_partial_artifact(tmp_path):
    bad = Sequence(
        MemoryFrameProvider(tuple(sequence()), metadata={"bad": object()})
    )
    destination = tmp_path / "bad.o4d"

    with pytest.raises(CodecError, match="not serializable"):
        encode_sequence(bad, destination, codec=REFERENCE)

    assert not destination.exists()
    assert list(tmp_path.iterdir()) == []


def test_codec_import_does_not_load_optional_dependencies():
    probe = subprocess.run(
        [sys.executable, "-c", "import open4d,sys; "
         "assert not {'PyQt6','torch','open3d','scipy','plyfile'} & sys.modules.keys()"],
        capture_output=True, text=True,
    )
    assert probe.returncode == 0, probe.stderr


@pytest.mark.parametrize("codec", REFERENCE_CODECS)
def test_reference_codecs_remain_usable_privately(tmp_path, codec):
    source = sequence()
    artifact = encode_sequence(source, tmp_path / f"{codec}.o4d", codec=codec)
    decoded = decode_sequence(artifact, codec=codec)

    assert len(decoded) == len(source)
    for expected, actual in zip(source, decoded, strict=True):
        assert actual.frame_index == expected.frame_index
        assert actual.timestamp == expected.timestamp
        assert actual.metadata == expected.metadata
        for name in ("positions", "triangles", "colors", "normals", "texture_coordinates"):
            np.testing.assert_array_equal(
                getattr(actual.geometry, name), getattr(expected.geometry, name)
            )
        np.testing.assert_array_equal(
            actual.geometry.attributes["labels"],
            expected.geometry.attributes["labels"],
        )
    decoded.close()


def test_public_registry_contains_research_codecs_only():
    identifiers = {info.id for info in available_codecs()}
    assert {"klt", "n4mc", "qndf", "qndf-int8", "vdmc", "faster_vdmc", "tvmc", "tsmc"} <= identifiers
    assert not {"npz", "raw", "deflate", "bzip2", "lzma", "rle", "draco",
                "temporal-delta", "temporal-pca"} & identifiers
    import open4d.codec as public
    assert not any(hasattr(public, name) for name in
                   ("DracoCodec", "NumPyZipCodec", "TemporalMeshCodec"))


@pytest.mark.parametrize("codec,suffix", (
    (TEMPORAL_DELTA_CODEC, ".td4d"), (TEMPORAL_PCA_CODEC, ".tp4d"),
))
def test_temporal_codecs_fresh_decode_without_processes(tmp_path, codec, suffix):
    frames = [Frame(
        20 + index, index / 30,
        TriangleMesh(
            [[index * .25, 0, 0], [1 + index * .1, 0, 0], [0, 1, index * .05]],
            [[0, 1, 2]],
        ), {"take": "moving"},
    ) for index in range(2)]
    source = Sequence(MemoryFrameProvider(
        frames, metadata={"fps": 30}, topology=TopologyMode.FIXED,
        has_constant_vertex_count=True, has_vertex_correspondence=True,
    ))
    artifact = encode_sequence(
        source, tmp_path / f"take{suffix}", codec=codec,
        quantization_bits=16, components=3,
    )
    first = decode_sequence(artifact, codec=codec, device="cpu")
    second = decode_sequence(artifact, codec=codec, device="cpu")

    assert first.metadata == source.metadata
    assert first.topology is TopologyMode.FIXED
    for expected, left, right in zip(source, first, second, strict=True):
        assert left.frame_index == expected.frame_index
        assert left.metadata == expected.metadata
        np.testing.assert_array_equal(left.geometry.positions, right.geometry.positions)
        np.testing.assert_array_equal(left.geometry.triangles, right.geometry.triangles)
        np.testing.assert_allclose(left.geometry.positions, expected.geometry.positions, atol=2e-5)


def test_encode_accepts_a_supported_path_without_codec_specific_io(tmp_path):
    source = tmp_path / "frame.obj"
    source.write_text(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="ascii"
    )

    artifact = encode_sequence(source, tmp_path / "frame.o4d", fps=24, codec=REFERENCE)
    decoded = decode_sequence(artifact, codec=REFERENCE)

    assert len(decoded) == 1
    np.testing.assert_array_equal(decoded[0].geometry.triangles, [[0, 1, 2]])
    decoded.close()


def test_path_reader_options_are_rejected_for_an_open_sequence(tmp_path):
    with pytest.raises(TypeError, match="apply only to path inputs"):
        encode_sequence(sequence(), tmp_path / "frame.o4d", fps=24, codec=REFERENCE)


def test_caller_supplied_codec_is_used_without_a_registry(tmp_path):
    class Codec:
        id = "memory-test"
        suffixes = (".test",)

        def encode(self, value, destination, **options):
            self.value = value
            self.options = options
            return destination

        def decode(self, source, **options):
            self.source = source
            self.options = options
            return sequence()

    codec = Codec()
    destination = tmp_path / "sequence.test"
    assert encode_sequence(sequence(), destination, codec=codec, level=2) == destination
    assert codec.options == {"level": 2}
    assert len(decode_sequence(destination, codec=codec, verify=True)) == 2
    assert codec.options == {"verify": True}


def test_registered_codec_can_be_selected_by_name(tmp_path, monkeypatch):
    import open4d.codec._api as implementation

    monkeypatch.setattr(implementation, "_CODECS", implementation._CODECS.copy())

    class RegisteredCodec:
        id = "registered-test"
        suffixes = (".registered",)

        def encode(self, value, destination, **options):
            return destination

        def decode(self, source, **options):
            return sequence()

    codec = RegisteredCodec()
    register_codec(codec)

    destination = tmp_path / "sequence.registered"
    assert encode_sequence(sequence(), destination, codec=codec.id) == destination
    assert len(decode_sequence(destination)) == 2
    with pytest.raises(ValueError, match="already registered"):
        register_codec(codec)


def test_missing_research_source_has_an_actionable_error(tmp_path, monkeypatch):
    from open4d.codec._research import research_module

    monkeypatch.setenv("OPEN4D_RESEARCH_ROOT", str(tmp_path))
    with pytest.raises(CodecError, match="OPEN4D_RESEARCH_ROOT"):
        research_module("klt.klt")


@pytest.mark.parametrize("identifier,suffix", [("native-test", ".v4d"), ("vdmc", ".vmesh"), ("faster_vdmc", ".vmesh")])
def test_vmesh_uses_one_native_call_per_sequence_direction(tmp_path, monkeypatch, identifier, suffix):
    import open4d.codec._vmesh as implementation

    mesh = TriangleMesh([[-2.0, 3, 4], [2, 3, 4], [-2, 7, 4]], [[0, 1, 2]])
    clean = Sequence(MemoryFrameProvider(
        [Frame(7, 0.25, mesh), Frame(8, 0.5, mesh)],
        topology=TopologyMode.FIXED,
        has_constant_vertex_count=True, has_vertex_correspondence=True,
    ))
    executable = tmp_path / "native"
    executable.write_text("native test double", encoding="ascii")
    executable.chmod(0o700)
    calls = []

    def native_call(command, label):
        calls.append((command, label))
        options = dict(item[2:].split("=", 1) for item in command[1:] if "=" in item)
        if "compressed" in options and "srcMesh" in options:
            encoded = Path(options["srcMesh"].replace("%06d", "000000")).read_text()
            assert "v 0 0 0" in encoded and "v 4095 0 0" in encoded
            Path(options["compressed"]).write_bytes(b"real-native-stream")
        elif "decMesh" in options:
            Path(options["decMesh"].replace("%06d", "000000")).write_text(
                "v 0 0 0\nv 4095 0 0\nv 0 4095 0\nf 1 2 3\n", encoding="ascii"
            )
            Path(options["decMesh"].replace("%06d", "000001")).write_text(
                "v 0 0 0\nv 4095 0 0\nv 0 4095 0\nv 4095 4095 0\nf 1 2 3\nf 2 4 3\n",
                encoding="ascii",
            )

    monkeypatch.setattr(implementation, "_run", native_call)
    codec = VMeshCodec(identifier)
    artifact = codec.encode(
        clean, tmp_path / ("sequence" + suffix), encoder=executable,
    )
    decoded = codec.decode(artifact, decoder=executable)

    assert len(decoded) == 2 and decoded[0].frame_index == 7
    np.testing.assert_allclose(decoded[0].geometry.positions, clean[0].geometry.positions)
    assert [len(frame.geometry.positions) for frame in decoded] == [3, 4]
    assert decoded.topology is TopologyMode.UNKNOWN
    assert decoded.has_constant_vertex_count is None
    assert decoded.has_vertex_correspondence is None
    assert decoded.timestamps == clean.timestamps
    assert [label for _, label in calls] == [
        f"{identifier} encoder", f"{identifier} decoder"
    ]
    assert all(isinstance(command, list) for command, _ in calls)
    assert "--encodeDisplacements=1" in calls[0][0]
    assert not any(item.startswith("--config=") for command, _ in calls for item in command)
    assert any(item.startswith("--decTex=") for item in calls[1][0])
    decoded.close()


def test_vmesh_decodes_a_raw_bitstream_without_an_open4d_manifest(
    tmp_path, monkeypatch
):
    import open4d.codec._vmesh as implementation

    executable = tmp_path / "decoder"
    executable.write_text("native test double", encoding="ascii")
    executable.chmod(0o700)
    config = tmp_path / "decoder.cfg"
    config.write_text("test config", encoding="ascii")
    bitstream = tmp_path / "capture.vmesh"
    bitstream.write_bytes(b"raw-vdmc-bitstream")
    calls = []
    monkeypatch.setenv("OPEN4D_VDMC_DECODER_CONFIG", str(config))

    def native_call(command, label):
        calls.append((command, label))
        options = dict(item[2:].split("=", 1) for item in command[1:] if "=" in item)
        assert Path(options["compressed"]) == bitstream.absolute()
        output = Path(options["decMesh"]).parent
        (output / "frame_000000.obj").write_text(
            "v 10 20 30\nv 11 20 30\nv 10 21 30\nf 1 2 3\n", encoding="ascii"
        )
        (output / "frame_000001.obj").write_text(
            "v 12 20 30\nv 13 20 30\nv 12 21 30\nf 1 2 3\n", encoding="ascii"
        )

    monkeypatch.setattr(implementation, "_run", native_call)
    decoded = VMeshCodec("vdmc").decode(
        bitstream, decoder=executable, fps=24
    )
    decoded_directory = Path(
        next(item for item in calls[0][0] if item.startswith("--decMesh="))
        .split("=", 1)[1]
    ).parent

    assert len(decoded) == 2
    assert decoded.timestamps == (0.0, 1 / 24)
    assert decoded.metadata["format"] == ".vmesh"
    assert decoded.metadata["codec"] == "vdmc"
    assert decoded.metadata["raw_bitstream"] is True
    assert decoded[1].frame_index == 1
    np.testing.assert_array_equal(decoded[0].geometry.positions[0], [10, 20, 30])
    assert calls[0][1] == "vdmc decoder"
    assert f"--config={config.absolute()}" in calls[0][0]
    assert decoded_directory.is_dir()

    decoded.close()
    assert not decoded_directory.exists()


@pytest.mark.parametrize("requested", ["cpu", "auto"])
def test_klt_artifact_fresh_decode_uses_saved_payload(tmp_path, monkeypatch, requested):
    import open4d.codec._klt as implementation

    source = Sequence(MemoryFrameProvider([
        Frame(9, 0.5, TriangleMesh(
            [[10.0, 20, 30], [10.5, 20, 30], [10, 20.5, 30]], [[0, 1, 2]]
        ), {"take": "rafa"})
    ], metadata={"fps": 30}))
    calls = []

    def prepare(sequence, destination, *, resolution):
        calls.append(("prepare", len(sequence), resolution))
        destination.mkdir(parents=True)
        return {"center": [10, 20, 30], "scale": 2.0, "resolution": resolution}

    def compress(args, *, verify_decode):
        calls.append(("encode", args.num_frames, verify_decode))
        Path(args.output_path).mkdir()
        with ZipFile(Path(args.output_path) / "compressed_archive.zip", "w") as archive:
            archive.writestr("decoder_context.pt", b"saved-klt-context")

    def decompress(source, destination, device):
        calls.append(("decode", (source / "decoder_context.pt").read_bytes(), device))
        destination.mkdir()
        (destination / "mesh_000000.obj").write_text(
            "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="ascii"
        )

    monkeypatch.setattr(implementation, "write_tsdf_sequence", prepare)
    if requested == "auto":
        torch = pytest.importorskip("torch")
        monkeypatch.setattr(torch.cuda, "is_available", lambda: False)
        monkeypatch.setattr(torch.backends.mps, "is_available", lambda: False)
    monkeypatch.setattr(implementation, "_backend", lambda: SimpleNamespace(
        run_compression=compress, decode_compressed=decompress,
    ))
    artifact = encode_sequence(source, tmp_path / "take.k4d", codec="klt")
    decoded = decode_sequence(artifact, device=requested)

    assert calls == [
        ("prepare", 1, 63), ("encode", 1, False),
        ("decode", b"saved-klt-context", "cpu"),
    ]
    assert decoded[0].frame_index == 9 and decoded[0].metadata["take"] == "rafa"
    np.testing.assert_allclose(decoded[0].geometry.positions[1], [10.5, 20, 30])
    decoded.close()


def test_encode_rejects_text_overwrite_flag_without_changing_output(tmp_path):
    from open4d.codec._npz import NumPyZipCodec

    output = tmp_path / "existing.o4d"
    output.write_bytes(b"original")
    with pytest.raises(TypeError, match="overwrite"):
        encode_sequence(sequence(), output, codec=NumPyZipCodec(), overwrite="false")
    assert output.read_bytes() == b"original"
