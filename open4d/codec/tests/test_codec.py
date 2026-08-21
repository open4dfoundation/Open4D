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

pytestmark = pytest.mark.cpu


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
    artifact = encode_sequence(source, tmp_path / "take.o4d")
    calls = []
    import open4d.codec._npz as implementation

    real_read = implementation._read_array

    def recording_read(*args):
        calls.append(args[1])
        return real_read(*args)

    monkeypatch.setattr(implementation, "_read_array", recording_read)
    decoded = decode_sequence(artifact)

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


def test_encode_refuses_to_overwrite_and_decode_rejects_corruption(tmp_path):
    artifact = encode_sequence(sequence(), tmp_path / "take.o4d")
    with pytest.raises(FileExistsError):
        encode_sequence(sequence(), artifact)
    broken = tmp_path / "broken.o4d"
    broken.write_bytes(b"not a zip")
    with pytest.raises(CodecError, match="invalid Open4D artifact"):
        decode_sequence(broken)


def test_encode_failure_removes_partial_artifact(tmp_path):
    bad = Sequence(
        MemoryFrameProvider(tuple(sequence()), metadata={"bad": object()})
    )
    destination = tmp_path / "bad.o4d"

    with pytest.raises(CodecError, match="not serializable"):
        encode_sequence(bad, destination)

    assert not destination.exists()
    assert list(tmp_path.iterdir()) == []


def test_codec_import_has_no_optional_or_process_dependencies():
    probe = subprocess.run(
        [
            sys.executable,
            "-c",
            "import open4d.codec,sys; assert 'PyQt6' not in sys.modules",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert probe.returncode == 0, probe.stderr
    assert "npz" in {info.id for info in available_codecs()}
    package_root = Path(__file__).resolve().parents[2]
    source = "\n".join(
        path.read_text()
        for package in (package_root / "codec", package_root / "visualization")
        for path in package.glob("*.py")
        if path.name != "_vmesh.py"
    )
    for forbidden in ("subprocess", "os.system", "os.popen", "shell=True"):
        assert forbidden not in source
    native = (package_root / "codec/_vmesh.py").read_text()
    assert "shell=True" not in native and ".sh" not in native
    assert native.count("subprocess.run(") == 1


@pytest.mark.parametrize("codec", ("raw", "deflate", "bzip2", "lzma", "rle"))
def test_reference_codecs_round_trip_exactly_and_infer_from_manifest(tmp_path, codec):
    source = sequence()
    artifact = encode_sequence(source, tmp_path / f"{codec}.o4d", codec=codec)
    decoded = decode_sequence(artifact)

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


def test_all_reference_codec_ids_are_public():
    infos = {info.id: info for info in available_codecs()}
    identifiers = set(infos)
    assert {"raw", "deflate", "bzip2", "lzma", "rle"} <= identifiers
    assert infos["npz"].backend == "python"
    assert infos["npz"].lossless is True
    assert "attributes" in infos["npz"].preserves
    assert infos["draco"].backend == "python-binding"
    research = {"klt", "n4mc", "qndf", "qndf-int8"}
    assert all(infos[codec].backend == "python-in-process" for codec in research)
    experimental = {"temporal-delta", "temporal-pca"}
    assert all(infos[codec].backend == "python-experimental" for codec in experimental)
    assert not {"tvmc", "tsmc"} & set(infos)


@pytest.mark.parametrize("codec,suffix", (
    ("temporal-delta", ".td4d"), ("temporal-pca", ".tp4d"),
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
    first = decode_sequence(artifact, device="cpu")
    second = decode_sequence(artifact, device="cpu")

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

    artifact = encode_sequence(source, tmp_path / "frame.o4d", fps=24)
    decoded = decode_sequence(artifact)

    assert len(decoded) == 1
    np.testing.assert_array_equal(decoded[0].geometry.triangles, [[0, 1, 2]])
    decoded.close()


def test_path_reader_options_are_rejected_for_an_open_sequence(tmp_path):
    with pytest.raises(TypeError, match="apply only to path inputs"):
        encode_sequence(sequence(), tmp_path / "frame.o4d", fps=24)


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


def test_source_only_codec_has_an_actionable_installed_package_error(
    tmp_path, monkeypatch
):
    import open4d.codec._api as implementation

    monkeypatch.setattr(
        implementation, "_CODECS",
        {key: value for key, value in implementation._CODECS.items() if key != "klt"},
    )
    monkeypatch.setattr(
        implementation, "_UNAVAILABLE_CODECS",
        {"klt": "research implementation is not included in this installation"},
    )
    with pytest.raises(CodecError, match="source|not included"):
        encode_sequence(sequence(), tmp_path / "take.k4d", codec="klt")


def test_vmesh_uses_one_native_call_per_sequence_direction(tmp_path, monkeypatch):
    import open4d.codec._vmesh as implementation

    clean = Sequence(MemoryFrameProvider([
        Frame(7, 0.25, TriangleMesh(
            [[0.0, 0, 0], [1, 0, 0], [0, 1, 0]], [[0, 1, 2]]
        ))
    ]))
    executable = tmp_path / "native"
    executable.write_text("native test double", encoding="ascii")
    executable.chmod(0o700)
    encoder_config = tmp_path / "encoder.cfg"
    decoder_config = tmp_path / "decoder.cfg"
    encoder_config.write_text("profile: test\n", encoding="ascii")
    decoder_config.write_text("profile: test\n", encoding="ascii")
    calls = []

    def native_call(command, label):
        calls.append((command, label))
        options = dict(item[2:].split("=", 1) for item in command[1:] if "=" in item)
        if "compressed" in options and "srcMesh" in options:
            Path(options["compressed"]).write_bytes(b"real-native-stream")
        elif "decMesh" in options:
            Path(options["decMesh"].replace("%06d", "000000")).write_text(
                "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="ascii"
            )

    monkeypatch.setattr(implementation, "_run", native_call)
    codec = VMeshCodec("native-test")
    artifact = codec.encode(
        clean, tmp_path / "sequence.v4d", encoder=executable,
        encoder_config=encoder_config, decoder_config=decoder_config,
    )
    decoded = codec.decode(artifact, decoder=executable)

    assert len(decoded) == 1 and decoded[0].frame_index == 7
    assert [label for _, label in calls] == [
        "native-test encoder", "native-test decoder"
    ]
    assert all(isinstance(command, list) for command, _ in calls)
    assert any(item.startswith("--decTex=") for item in calls[1][0])
    decoded.close()


def test_klt_artifact_fresh_decode_uses_saved_payload(tmp_path, monkeypatch):
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
    monkeypatch.setattr(implementation, "_backend", lambda: SimpleNamespace(
        run_compression=compress, decode_compressed=decompress,
    ))
    artifact = encode_sequence(source, tmp_path / "take.k4d", codec="klt")
    decoded = decode_sequence(artifact, device="cpu")

    assert calls == [
        ("prepare", 1, 63), ("encode", 1, False),
        ("decode", b"saved-klt-context", "cpu"),
    ]
    assert decoded[0].frame_index == 9 and decoded[0].metadata["take"] == "rafa"
    np.testing.assert_allclose(decoded[0].geometry.positions[1], [10.5, 20, 30])
    decoded.close()
