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
from open4d.codec import _api, _tracked, _v3c
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
        stream.seek(len(_v3c._BOOTSTRAP))
        result = []
        while stream.peek(1):
            result.append(_v3c._read_record(stream))
        return result


def rewrite(path, items):
    with path.open("wb") as stream:
        stream.write(_v3c._BOOTSTRAP)
        for item in items:
            _v3c._write_record(stream, *item)


@pytest.mark.parametrize("codec", ["tvmc", "tsmc"])
def test_native_bytes_and_irregular_timing_survive_single_file_carriage(tmp_path, codec):
    source, metadata = native_directory(tmp_path, codec, large=True)
    expected = {p.name: p.read_bytes() for p in source.iterdir()}
    (source / "frame_000.obj").write_text("scratch OBJ must not be packaged")
    path = pack_vmesh(source, tmp_path / "take.vmesh")
    shutil.rmtree(source)
    assert path.is_file()
    assert _v3c.probe_codec(path) == codec
    manifest = inspect_vmesh(path)
    assert manifest["sequence"] == metadata
    assert manifest["frame_count"] == 2
    assert manifest["dependency_mode"] == ("shared-reference" if codec == "tvmc" else "whole-group")
    for record in manifest["files"]:
        assert hashlib.sha256(expected[record["name"]]).hexdigest() == record["sha256"]
    recovered = unpack_vmesh(path, tmp_path / "recovered")
    assert {p.name: p.read_bytes() for p in recovered.iterdir()} == expected
    with pytest.raises(FileExistsError):
        unpack_vmesh(path, recovered)


def test_wire_is_v3c_atlas_sei_with_no_private_top_level_units(tmp_path):
    source, _ = native_directory(tmp_path, "tvmc")
    data = pack_vmesh(source, tmp_path / "take.vmesh").read_bytes()
    # Independent framing walk: V3C four-byte lengths; VPS then AD; each AD
    # has an atlas NAL sample stream containing user_data_unregistered SEI.
    assert data[0] == 0x60
    pos, types, messages = 1, [], []
    while pos < len(data):
        size = int.from_bytes(data[pos:pos + 4], "big")
        unit = data[pos + 4:pos + 4 + size]
        assert len(unit) == size
        pos += 4 + size
        kind = int.from_bytes(unit[:4], "big") >> 27
        types.append(kind)
        if kind == 0:
            assert len(types) == 1
            assert unit[:4] == bytes(4)
            assert b"O4D\x01" in unit
            continue
        assert unit[:5] == b"\x08\x00\x00\x00\x60"
        assert int.from_bytes(unit[5:9], "big") == len(unit) - 9
        assert unit[9:12] == b"\x56\x01\x04"
        index, length = 12, 0
        while unit[index] == 255:
            length += 255
            index += 1
        length += unit[index]
        payload = unit[index + 1:-1]
        assert len(payload) == length and unit[-1] == 128
        assert payload[:16].hex() == "e23b8c4791354e3490ad421c6c57b63a"
        assert payload[16] == 1
        messages.append(payload[17])
    assert pos == len(data) and types == [0] + [1] * (len(types) - 1)
    assert messages == [0] + [1] * 6 + [2]
    assert b"PK\x03\x04" not in data  # TVMC fixture contains no ZIP data


@pytest.mark.parametrize("length", [224, 225, 226, 480, 481, 1024 * 1024, 1024 * 1024 + 1])
def test_sei_extended_lengths_and_chunk_boundaries(tmp_path, length):
    source, _ = native_directory(tmp_path, "tvmc")
    payload = bytes(range(256)) * (length // 256) + bytes(range(length % 256))
    (source / "reference.drc").write_bytes(payload)
    path = pack_vmesh(source, tmp_path / "take.vmesh")
    chunks = [item for item in records(path) if item[0] == 1 and item[1] == 1]
    assert b"".join(item[3] for item in chunks) == payload
    assert all(len(item[3]) <= 1024 * 1024 for item in chunks)
    recovered = unpack_vmesh(path, tmp_path / "native")
    assert (recovered / "reference.drc").read_bytes() == payload


@pytest.mark.parametrize("damage", ["truncated", "no-end", "trailing", "hash", "order", "offset",
                                    "bootstrap", "huge-unit", "wrong-nal", "wrong-end"])
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
            data[len(_v3c._BOOTSTRAP):len(_v3c._BOOTSTRAP) + 4] = b"\xff" * 4
        else:
            data[len(_v3c._BOOTSTRAP) + 4 + 9] ^= 2
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
        sequence = json.loads(items[1][3])
        sequence["frames"][1]["timestamp"] = float("nan")
        payload = json.dumps(sequence).encode()
        items[1] = (1, 0, 0, payload)
        manifest["files"][0].update(size=len(payload), sha256=hashlib.sha256(payload).hexdigest())
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
    original_writer = _v3c._write_record

    def failing_writer(stream, kind, file_id, offset, data):
        if kind == 1:
            raise OSError("disk full")
        original_writer(stream, kind, file_id, offset, data)

    monkeypatch.setattr(_v3c, "_write_record", failing_writer)
    with pytest.raises(OSError, match="disk full"):
        pack_vmesh(source, path, overwrite=True)
    assert path.read_bytes() == b"original"
    assert not list(tmp_path.glob(".take.vmesh-*"))
    monkeypatch.setattr(_v3c, "_write_record", original_writer)
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
