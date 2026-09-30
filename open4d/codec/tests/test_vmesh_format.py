from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import sys

import numpy as np
import pytest

import open4d
from open4d.codec import CodecError, decode_sequence, inspect_vmesh, pack_vmesh, unpack_vmesh
from open4d.codec import _api, _tracked, _vmesh_format
from open4d.io._mesh import write_obj

pytestmark = pytest.mark.cpu


def native_directory(root, codec, *, large=False):
    source = root / f"native.{codec}"
    source.mkdir()
    metadata = {
        "codec": codec, "version": 1, "metadata": {"subject": "motion", "units": "m"},
        "frames": [{"frame_index": 10, "timestamp": 0.125, "metadata": {"key": True}},
                   {"frame_index": 42, "timestamp": 0.375, "metadata": {"key": False}}],
    }
    (source / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    # Opaque bytes test carriage; native Draco/ANS validity is tested separately.
    (source / "reference.drc").write_bytes(bytes(range(256)) * (5000 if large else 1))
    if codec == "tvmc":
        for index in range(2):
            (source / f"displacement_{index:06d}.drc").write_bytes(b"displacement" + bytes([index]))
            np.save(source / f"displacement_{index:06d}.npy", np.arange(3, dtype=np.uint32))
    else:
        np.savetxt(source / "B_matrix.txt", np.arange(12).reshape(2, 6))
        np.savetxt(source / "T_matrix.txt", np.zeros((1, 6)))
        np.save(source / "delta_trajectories_encoded.npy", np.array([1234, 5678], dtype=np.uint32))
        np.savez(source / "entropy_model.npz", shape=[3, 2], means=[0., 0.], stds=[1., 1.])
    return source, metadata


def records(path):
    with path.open("rb") as stream:
        stream.seek(len(_vmesh_format._MAGIC))
        result = []
        while stream.peek(1):
            result.append(_vmesh_format._read_record(stream))
        return result


def rewrite(path, items):
    with path.open("wb") as stream:
        stream.write(_vmesh_format._MAGIC)
        for item in items:
            _vmesh_format._write_record(stream, *item)


@pytest.mark.parametrize("codec", ["tvmc", "tsmc"])
def test_native_bytes_and_irregular_timing_survive_single_file_carriage(tmp_path, codec):
    source, metadata = native_directory(tmp_path, codec, large=True)
    expected = {p.name: p.read_bytes() for p in source.iterdir()}
    (source / "frame_000.obj").write_text("scratch OBJ must not be packaged")
    path = pack_vmesh(source, tmp_path / "take.vmesh")
    shutil.rmtree(source)
    assert path.is_file()
    assert _vmesh_format.probe_codec(path) == codec
    manifest = inspect_vmesh(path)
    assert manifest["sequence"] == metadata
    assert manifest["frame_count"] == 2
    assert manifest["dependency_mode"] == ("shared-reference" if codec == "tvmc" else "whole-group")
    for record in manifest["files"]:
        assert hashlib.sha256(expected[record["name"]]).hexdigest() == record["sha256"]
    recovered = unpack_vmesh(path, tmp_path / "recovered")
    assert json.loads((recovered / "metadata.json").read_text()) == metadata
    assert {p.name: p.read_bytes() for p in recovered.iterdir() if p.name != "metadata.json"} == {
        k: v for k, v in expected.items() if k != "metadata.json"}
    with pytest.raises(FileExistsError):
        unpack_vmesh(path, recovered)


def test_wire_is_standalone_vmesh_with_direct_native_payloads(tmp_path):
    source, metadata = native_directory(tmp_path, "tvmc")
    data = pack_vmesh(source, tmp_path / "take.vmesh").read_bytes()
    assert data[:8] == b"VMESH\x00\x01\x00"
    pos, messages = 8, []
    while pos < len(data):
        size = int.from_bytes(data[pos:pos + 4], "big")
        unit = data[pos + 4:pos + 4 + size]
        assert len(unit) == size
        assert unit[0] == 1  # record version
        messages.append(unit[1])
        pos += 4 + size
    assert pos == len(data) and messages == [0] + [1] * 5 + [2]
    manifest = json.loads(records(tmp_path / "take.vmesh")[0][3])
    assert manifest["schema"] == "vmesh/1"
    assert manifest["sequence"]["frames"] == metadata["frames"]
    assert "schema" not in manifest["sequence"]
    assert "metadata.json" not in [f["name"] for f in manifest["files"]]
    assert b"open4d." not in data
    assert b"O4D\x01" not in data
    assert bytes.fromhex("e23b8c4791354e3490ad421c6c57b63a") not in data


@pytest.mark.parametrize("length", [224, 225, 226, 480, 481, 1024 * 1024, 1024 * 1024 + 1])
def test_record_lengths_and_chunk_boundaries(tmp_path, length):
    source, _ = native_directory(tmp_path, "tvmc")
    payload = bytes(range(256)) * (length // 256) + bytes(range(length % 256))
    (source / "reference.drc").write_bytes(payload)
    path = pack_vmesh(source, tmp_path / "take.vmesh")
    chunks = [item for item in records(path) if item[0] == 1 and item[1] == 0]
    assert b"".join(item[3] for item in chunks) == payload
    assert all(len(item[3]) <= 1024 * 1024 for item in chunks)
    recovered = unpack_vmesh(path, tmp_path / "native")
    assert (recovered / "reference.drc").read_bytes() == payload


@pytest.mark.parametrize("damage", ["truncated", "no-end", "trailing", "hash", "order", "offset",
                                    "bootstrap", "huge-unit", "wrong-record", "wrong-end"])
def test_malformed_stream_never_publishes_or_launches_native_decode(tmp_path, monkeypatch, damage):
    source, _ = native_directory(tmp_path, "tvmc")
    path = pack_vmesh(source, tmp_path / "take.vmesh")
    items = records(path)
    if damage == "no-end":
        rewrite(path, items[:-1])
    elif damage in ("hash", "order", "offset", "wrong-end"):
        if damage == "hash":
            kind, file_id, offset, blob = items[2]
            items[2] = kind, file_id, offset, bytes([blob[0] ^ 1]) + blob[1:]
        elif damage == "order":
            items[1], items[2] = items[2], items[1]
        elif damage == "offset":
            kind, file_id, offset, blob = items[2]
            items[2] = kind, file_id, offset + 1, blob
        else:
            items[-1] = (2, 0, 0, bytes(32))
        rewrite(path, items)
    else:
        data = bytearray(path.read_bytes())
        if damage == "truncated":
            del data[-1]
        elif damage == "trailing":
            data.extend(b"junk")
        elif damage == "bootstrap":
            data[0] ^= 1
        elif damage == "huge-unit":
            data[len(_vmesh_format._MAGIC):len(_vmesh_format._MAGIC) + 4] = b"\xff" * 4
        else:
            data[len(_vmesh_format._MAGIC) + 4] ^= 2
        path.write_bytes(data)
    monkeypatch.setattr(_tracked, "_run", lambda *args: pytest.fail("native worker called on corrupt input"))
    with pytest.raises(CodecError):
        inspect_vmesh(path)
    with pytest.raises(CodecError):
        unpack_vmesh(path, tmp_path / "recovered")
    with pytest.raises(CodecError):
        decode_sequence(path, codec="tvmc")
    assert not (tmp_path / "recovered").exists()
    assert not list(tmp_path.glob(".recovered-*"))


@pytest.mark.parametrize("change", ["path", "duplicate", "size", "version", "codec", "count", "mode", "metadata"])
def test_manifest_rejects_unsafe_or_inconsistent_native_descriptions(tmp_path, change):
    source, _ = native_directory(tmp_path, "tvmc")
    path = pack_vmesh(source, tmp_path / "take.vmesh")
    items = records(path)
    manifest = json.loads(items[0][3])
    if change == "path":
        manifest["files"][1]["name"] = "../escaped.drc"
    elif change == "duplicate":
        manifest["files"][1] = manifest["files"][0]
    elif change == "size":
        manifest["files"][1]["size"] = -1
    elif change == "version":
        manifest["schema"] = "open4d.vmesh.native/2"
    elif change == "codec":
        manifest["codec"] = "obj-sequence"
    elif change == "count":
        manifest["frame_count"] = True
    elif change == "mode":
        manifest["dependency_mode"] = "independent-frames"
    else:
        manifest["sequence"]["frames"][1]["timestamp"] = float("nan")
    blob = json.dumps(manifest).encode()
    items[0], items[-1] = (0, 0, 0, blob), (2, 0, 0, hashlib.sha256(blob).digest())
    rewrite(path, items)
    with pytest.raises(CodecError):
        unpack_vmesh(path, tmp_path / "recovered")
    assert not (tmp_path / "escaped.drc").exists()


def test_pack_preserves_existing_file_on_failure_and_requires_explicit_overwrite(tmp_path, monkeypatch):
    source, _ = native_directory(tmp_path, "tsmc")
    path = tmp_path / "take.vmesh"
    path.write_bytes(b"original")
    with pytest.raises(FileExistsError):
        pack_vmesh(source, path)
    original_writer = _vmesh_format._write_record

    def failing_writer(stream, kind, file_id, offset, data):
        if kind == 1:
            raise OSError("disk full")
        original_writer(stream, kind, file_id, offset, data)

    monkeypatch.setattr(_vmesh_format, "_write_record", failing_writer)
    with pytest.raises(OSError, match="disk full"):
        pack_vmesh(source, path, overwrite=True)
    assert path.read_bytes() == b"original"
    assert not list(tmp_path.glob(".take.vmesh-*"))
    monkeypatch.setattr(_vmesh_format, "_write_record", original_writer)
    pack_vmesh(source, path, overwrite=True)
    assert inspect_vmesh(path)["codec"] == "tsmc"


@pytest.mark.parametrize("case", ["missing", "symlink", "empty"])
def test_pack_requires_self_contained_native_payload(tmp_path, case):
    source, _ = native_directory(tmp_path, "tsmc")
    path = source / "entropy_model.npz"
    path.unlink()
    if case == "symlink":
        path.symlink_to(source / "reference.drc")
    elif case == "empty":
        path.touch()
    with pytest.raises(CodecError):
        pack_vmesh(source, tmp_path / "take.vmesh")
    assert not (tmp_path / "take.vmesh").exists()


@pytest.mark.parametrize("codec", ["tvmc", "tsmc"])
def test_dispatch_selects_embedded_codec_and_rejects_conflicts(tmp_path, monkeypatch, codec):
    source, _ = native_directory(tmp_path, codec)
    path = pack_vmesh(source, tmp_path / "take.vmesh")
    calls = []
    implementation = _api._CODECS[codec]
    monkeypatch.setattr(implementation, "decode", lambda source, **kw: calls.append((source, kw)))
    open4d.load(path)
    decode_sequence(path, codec=codec)
    assert calls == [(path, {}), (path, {})]
    with pytest.raises(CodecError, match="contains"):
        decode_sequence(path, codec="vdmc")
    with pytest.raises(TypeError, match="timestamps"):
        open4d.load(path, fps=24)


def test_raw_vdmc_vmesh_keeps_existing_decoder_and_fps(tmp_path, monkeypatch):
    path = tmp_path / "raw.vmesh"
    path.write_bytes(b"ordinary VDMC stream (decoder stub)")
    calls = []
    monkeypatch.setattr(_api.VDMC_CODEC, "decode", lambda source, **kw: calls.append((source, kw)))
    open4d.load(path, fps=24)
    assert calls == [(path, {"fps": 24})]


def test_tsmc_real_entropy_stream_decodes_after_packing_and_removing_source(tmp_path):
    constriction = pytest.importorskip("constriction")
    from open4d.codec import _tracked_worker

    source, _ = native_directory(tmp_path, "tsmc")
    delta = np.array([[0.125, 4.0], [-0.75, 4.0], [0.33333, 4.0]])
    original = tmp_path / "original.npy"
    np.save(original, delta)
    model_path = source / "entropy_model.npz"
    _tracked_worker._save_entropy_model(original, model_path)
    expected = np.round(delta * 10000).astype(np.int32)
    with np.load(model_path, allow_pickle=False) as model:
        distribution = constriction.stream.model.QuantizedGaussian(int(model["minimum"]), int(model["maximum"]))
        encoder = constriction.stream.stack.AnsCoder()
        encoder.encode_reverse(expected.ravel(), distribution,
                               np.tile(model["means"], 3), np.tile(model["stds"], 3))
        np.save(source / "delta_trajectories_encoded.npy", encoder.get_compressed())
    path = pack_vmesh(source, tmp_path / "tsmc.vmesh")
    original.unlink()
    shutil.rmtree(source)
    recovered = unpack_vmesh(path, tmp_path / "recovered")
    actual = _tracked_worker._decode_entropy(recovered / "delta_trajectories_encoded.npy",
                                             recovered / "entropy_model.npz")
    np.testing.assert_array_equal(actual, expected / 10000)


@pytest.mark.parametrize("codec", ["tvmc", "tsmc"])
def test_usdc_vmesh_usdc_api_round_trip_with_native_worker_stub(tmp_path, monkeypatch, codec):
    pytest.importorskip("pxr.Usd")
    source, metadata = native_directory(tmp_path, codec)
    expected = {p.name: p.read_bytes() for p in source.iterdir() if p.name != "metadata.json"}
    sequence = open4d.Sequence(open4d.MemoryFrameProvider([
        open4d.Frame(record["frame_index"], record["timestamp"], open4d.TriangleMesh(
            [[float(i), 0, 0], [i + 1.0, 0, 0], [float(i), 1, 0]], [[0, 1, 2]]), record["metadata"])
        for i, record in enumerate(metadata["frames"])
    ], metadata=metadata["metadata"]))
    usd = open4d.save(sequence, tmp_path / "input.usdc")
    calls = []

    def run(python, action, request):
        settings = json.loads(request.read_text())
        calls.append(settings)
        output = Path(settings["output"])
        if action == "encode":
            for name, payload in expected.items():
                (output / name).write_bytes(payload)
        else:
            assert {p.name: p.read_bytes() for p in Path(settings["input"]).iterdir()
                    if p.name != "metadata.json"} == expected
            for i in range(settings["frames"]):
                write_obj(output / f"frame_{i:06d}.obj", sequence[i].geometry.positions,
                          sequence[i].geometry.triangles)

    monkeypatch.setattr(_tracked, "_run", run)
    monkeypatch.setattr(_api._CODECS[codec], "_settings", lambda *a, **kw: {"codec": codec, "python": sys.executable})
    with open4d.load(usd) as opened:
        path = open4d.save(opened, tmp_path / "take.vmesh", codec=codec, options={"dotnet": sys.executable})
        timestamps, indices = opened.timestamps, [f.frame_index for f in opened]
        input_metadata = dict(opened.metadata)
    shutil.rmtree(source)
    usd.unlink()
    with open4d.load(path) as decoded:
        assert decoded.timestamps == timestamps
        assert [f.frame_index for f in decoded] == indices
        assert decoded.metadata == input_metadata
        assert decoded[1].metadata == sequence[1].metadata
        reconstructed = open4d.save(decoded, tmp_path / "output.usdc")
        private = Path(calls[-1]["input"]).parent
        assert private.exists()
    assert not private.exists()
    with open4d.load(reconstructed) as result:
        assert result.timestamps == timestamps
        np.testing.assert_allclose(result[1].geometry.positions, sequence[1].geometry.positions)


def test_retired_o4d_wrapper_is_rejected_before_native_decode(tmp_path):
    path = tmp_path / 'old.vmesh'
    path.write_bytes(b'\x60' + bytes(20) + bytes.fromhex('e23b8c4791354e3490ad421c6c57b63a') + b'O4D\x01')
    with pytest.raises(CodecError, match='retired O4D/V3C'):
        decode_sequence(path)


def test_nested_open4d_schema_is_not_a_vmesh_metadata_type(tmp_path):
    source, _ = native_directory(tmp_path, 'tvmc')
    metadata = json.loads((source / 'metadata.json').read_text())
    metadata['schema'] = 'open4d.some-custom-type/1'
    (source / 'metadata.json').write_text(json.dumps(metadata))
    with pytest.raises(CodecError):
        pack_vmesh(source, tmp_path / 'bad.vmesh')


def test_profile_expansion_is_bounded_before_allocating_payload_names():
    from open4d.codec._native_profiles import layout
    with pytest.raises(CodecError, match='outside limits'):
        layout('tvmc', 65536)
    # Sixteen small JSON frame records could otherwise expand to 131k names.
    profile = dict(profile='rerf/1', group_size=1, pca=False,
                   pca_channels=[7, 13], frames=[dict(id=i, quality=90, motion=False,
                   channels=[4096]) for i in range(16)])
    with pytest.raises(CodecError, match='file count outside limits'):
        layout('rerf', 16, profile)


@pytest.mark.parametrize('data', [b'{"x":1e999}', b'{"x":NaN}', b'{"x":1,"x":2}'])
def test_json_rejects_overflow_and_ambiguous_values(data):
    with pytest.raises(CodecError):
        _vmesh_format._json(data)


@pytest.mark.parametrize('codec', ['vdmc', 'faster_vdmc'])
def test_vdmc_adapter_carries_actual_backend_bytes_and_restores_timing(tmp_path, monkeypatch, codec):
    from open4d.codec import _vmesh
    frames = [open4d.Frame(i + 7, t, open4d.TriangleMesh(
        [[float(i), 0, 0], [i + 1., 0, 0], [float(i), 1, 0]], [[0, 1, 2]]))
        for i, t in enumerate((.125, .875))]
    sequence = open4d.Sequence(open4d.MemoryFrameProvider(frames))
    native_bytes = b'\x60native encoder-produced bitstream fixture'
    config = tmp_path / 'decoder.cfg'
    config.write_text('native decoder configuration')
    normalized = []

    def run(command, label):
        options = dict(item[2:].split('=', 1) for item in command[1:] if item.startswith('--'))
        stream = Path(options['compressed'])
        if label.endswith('encoder'):
            with open4d.load(Path(options['srcMesh']).parent) as input_sequence:
                normalized.extend(f.geometry for f in input_sequence)
            stream.write_bytes(native_bytes)
        else:
            assert stream.read_bytes() == native_bytes
            assert Path(options['config']).read_text() == config.read_text()
            for i, mesh in enumerate(normalized):
                write_obj(Path(options['decMesh'] % i), mesh.positions, mesh.triangles)

    monkeypatch.setattr(_vmesh, '_run', run)
    artifact = open4d.encode(sequence, tmp_path / 'take.vmesh', codec=codec,
                             encoder=sys.executable, decoder_config=config)
    info = inspect_vmesh(artifact)
    assert info['codec'] == codec
    assert [record['name'] for record in info['files']] == ['sequence.vmesh', 'decoder.cfg']
    extracted = unpack_vmesh(artifact, tmp_path / 'native')
    assert (extracted / 'sequence.vmesh').read_bytes() == native_bytes
    with open4d.decode(artifact, decoder=sys.executable) as decoded:
        assert decoded.timestamps == sequence.timestamps
        for expected, actual in zip(sequence, decoded):
            np.testing.assert_allclose(actual.geometry.positions, expected.geometry.positions, atol=1 / 4095)
