import pytest

from open4d.codec import available_codecs, encode_sequence
from open4d.codec._api import _CODECS


def test_public_codecs_share_one_custom_output_extension():
    assert len(available_codecs()) == 12
    assert all(info.suffixes == (".o4d",) for info in available_codecs())


@pytest.mark.parametrize("identifier", sorted(_CODECS))
def test_invalid_destination_fails_before_codec_backend(identifier, tmp_path):
    from open4d.codec.tests.test_research_cpu import moving_cube

    destination = tmp_path / "retired.custom"
    with moving_cube() as sequence:
        with pytest.raises(ValueError, match=".o4d"):
            _CODECS[identifier].encode(sequence, destination)
        with pytest.raises(ValueError, match=".o4d"):
            encode_sequence(sequence, destination, codec=identifier)
    assert not destination.exists()


def test_pack_is_not_an_implicit_archive_converter(tmp_path):
    from open4d.codec import CodecError, pack_o4d

    source = tmp_path / "archive.n4d"
    source.write_bytes(b"not a native directory")
    destination = tmp_path / "output.o4d"
    with pytest.raises(CodecError):
        pack_o4d(source, destination)
    assert not destination.exists()


@pytest.mark.parametrize("identifier", ("vega", "queen", "3dgstream", "rerf"))
def test_native_codec_rejects_a_retired_extension_before_loading(identifier, tmp_path):
    from open4d.codec import CodecError

    source = tmp_path / "renamed.retired"
    source.write_bytes(b"VMESH\x00\x01\x00")
    assert not _CODECS[identifier].can_decode(source)
    with pytest.raises(CodecError, match=".o4d"):
        _CODECS[identifier].decode(source)


@pytest.mark.parametrize("identifier", ("npz", "raw", "deflate", "bzip2", "lzma", "rle"))
def test_private_profile_can_be_selected_explicitly_without_registration(identifier, tmp_path):
    from open4d.codec import decode_sequence
    from open4d.codec._npz import REFERENCE_CODECS
    from open4d.codec.tests.test_codec import sequence

    codec = next(item for item in REFERENCE_CODECS if item.id == identifier)
    with sequence() as source:
        artifact = codec.encode(source, tmp_path / "arrays.o4d")
        with decode_sequence(artifact, codec=identifier) as decoded:
            assert len(decoded) == len(source)
            assert decoded.timestamps == source.timestamps


@pytest.fixture
def installed_streamer_namespace(monkeypatch):
    import sys
    from pathlib import Path

    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2] / "streamer"))
    for name in tuple(sys.modules):
        if name.startswith("open4d.streamer."):
            monkeypatch.delitem(sys.modules, name)
    monkeypatch.setitem(sys.modules, "open4d.streamer", None)


def browser_ply():
    return (b"ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\n"
            b"property float y\nproperty float z\nelement face 1\n"
            b"property list uchar int vertex_indices\nend_header\n"
            b"0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n")


def legacy_browser_clip(path):
    import struct

    payload = browser_ply()
    path.write_bytes(struct.pack("<8sIII", b"O4DSEQ\x00\x00", 1, 1, 3)
                     + b"ply" + struct.pack("<II", 31, len(payload)) + payload)
    return path


def test_frame_profile_load_uses_installed_streamer_package(tmp_path, installed_streamer_namespace):
    from open4d.codec import decode_sequence
    from streamer.sequence import pack

    frame = tmp_path / "triangle.ply"
    frame.write_bytes(browser_ply())
    artifact = tmp_path / "delivery.o4d"
    pack([frame], artifact)
    with decode_sequence(artifact) as decoded:
        assert len(decoded) == 1
        assert decoded[0].geometry.triangles.tolist() == [[0, 1, 2]]


def test_legacy_frame_migration_uses_installed_streamer_package(tmp_path, installed_streamer_namespace):
    from open4d.codec import decode_sequence, migrate_legacy

    source = legacy_browser_clip(tmp_path / "old.seq")
    artifact = migrate_legacy(source, tmp_path / "delivery.o4d", fps=8)
    with decode_sequence(artifact) as decoded:
        assert decoded.timestamps == (0.,)
        assert decoded[0].geometry.positions.tolist() == [[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]]


@pytest.mark.parametrize("operation", ("load", "migrate"))
def test_frame_profile_missing_streamer_has_actionable_error(tmp_path, monkeypatch, installed_streamer_namespace, operation):
    import sys
    from open4d._streamer import StreamerDependencyError
    from open4d.codec import decode_sequence, migrate_legacy
    from streamer.sequence import pack

    frame = tmp_path / "triangle.ply"
    frame.write_bytes(browser_ply())
    artifact = tmp_path / "delivery.o4d"
    pack([frame], artifact)
    source = legacy_browser_clip(tmp_path / "old.seq")
    monkeypatch.setitem(sys.modules, "streamer", None)
    with pytest.raises(StreamerDependencyError, match="pip install -e open4d/streamer"):
        if operation == "load":
            decode_sequence(artifact)
        else:
            migrate_legacy(source, tmp_path / "converted.o4d")
