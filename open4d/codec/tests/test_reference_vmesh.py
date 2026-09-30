from io import BytesIO
import json

import numpy as np
import pytest

from open4d.codec import CodecError, decode_sequence, inspect_vmesh, unpack_vmesh
from open4d.codec._npz import NumPyZipCodec, REFERENCE_CODECS, _array_from_bytes
from open4d.codec._temporal import TEMPORAL_DELTA_CODEC, TEMPORAL_PCA_CODEC
from open4d.codec.tests.test_codec import sequence


@pytest.mark.parametrize("codec", REFERENCE_CODECS)
def test_reference_vmesh_carries_standard_npz_arrays_without_application_manifest(tmp_path, codec):
    source = sequence()
    artifact = codec.encode(source, tmp_path / "take.vmesh")
    manifest = inspect_vmesh(artifact)
    assert artifact.read_bytes().startswith(b"VMESH\x00\x01\x00")
    assert manifest["codec"] == codec.id and "open4d." not in json.dumps(manifest)
    assert [record["name"] for record in manifest["files"]] == ["frame_000000.npz", "frame_000001.npz"]
    native = tmp_path / "native"
    unpack_vmesh(artifact, native)
    with np.load(native / "frame_000000.npz", allow_pickle=False) as arrays:
        assert "manifest" not in arrays.files
        if codec.rle:
            assert arrays["positions"].dtype == np.uint8
        else:
            np.testing.assert_array_equal(arrays["positions"], source[0].geometry.positions)
    with decode_sequence(artifact) as decoded:
        np.testing.assert_array_equal(decoded[0].geometry.positions, source[0].geometry.positions)
        np.testing.assert_array_equal(decoded[0].geometry.attributes["labels"], source[0].geometry.attributes["labels"])


@pytest.mark.parametrize("payload", (
    (2 ** 40).to_bytes(8, "little") + bytes([255, 1]),
    (1).to_bytes(8, "little") + bytes([255, 1]),
    (1).to_bytes(8, "little") + bytes([0, 1]),
))
def test_rle_rejects_invalid_length_before_repeat_allocates(payload, monkeypatch):
    monkeypatch.setattr(np, "repeat", lambda *args: pytest.fail("invalid RLE allocated decoded values"))
    with pytest.raises(CodecError, match="RLE"):
        NumPyZipCodec("rle", rle=True).unpack(payload)


def test_npy_shape_bomb_fails_before_numpy_load_allocates(monkeypatch):
    stream = BytesIO()
    np.lib.format.write_array_header_1_0(stream, {
        "descr": "<f4", "fortran_order": False, "shape": (2 ** 50,),
    })
    monkeypatch.setattr(np, "load", lambda *args, **kwargs: pytest.fail("invalid NPY allocated an array"))
    with pytest.raises(CodecError, match="NPY size"):
        _array_from_bytes(stream.getvalue())


@pytest.mark.parametrize("codec", (NumPyZipCodec(), TEMPORAL_DELTA_CODEC, TEMPORAL_PCA_CODEC))
def test_private_codecs_reject_retired_extensions(tmp_path, codec):
    with pytest.raises(ValueError, match=".vmesh"):
        codec.encode(sequence(), tmp_path / "take.o4d")
    with pytest.raises(CodecError, match=".vmesh"):
        codec.decode(tmp_path / "take.o4d")
