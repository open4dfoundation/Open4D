import hashlib
from pathlib import Path

import pytest

from open4d import data
from open4d.data import _samples, _catalog, _transfer


@pytest.mark.parametrize("name", [_samples.MESH_ID, _samples.GAUSSIAN_ID])
def test_generated_catalog_sample_hash_and_load(tmp_path, name):
    record = _catalog.find(name)
    files = _samples.build(name)
    for item in record["files"]:
        assert len(files[item["name"]]) == item["size"]
        assert hashlib.sha256(files[item["name"]]).hexdigest() == item["sha256"]
    asset = data.get(name, cache_dir=tmp_path)
    path = asset.download()
    loaded = asset.load()
    assert len(loaded) == 8
    if hasattr(loaded, "close"):
        loaded.close()
    frame = next(path.rglob("frame_0000.*"))
    content = frame.read_bytes()
    frame.write_bytes(bytes([content[0] ^ 1]) + content[1:])
    with pytest.raises(ValueError, match="cache"):
        asset.download()


def test_incomplete_transfer_keeps_partial_and_rejects_wrong_hash(tmp_path, monkeypatch):
    from io import BytesIO
    class Response(BytesIO):
        status = 200
        headers = {}
        def geturl(self):
            return "https://example.org/data"
    partial = tmp_path / "part"
    monkeypatch.setattr(_transfer, "_open", lambda url, start: Response(b"partial"))
    with pytest.raises(OSError, match="ended"):
        _transfer.fetch("https://example.org/data", partial, size=20, sha256="a" * 64)
    assert partial.read_bytes() == b"partial"
    monkeypatch.setattr(_transfer, "_open", lambda url, start: Response(b"x" * 20))
    with pytest.raises(ValueError, match="SHA-256"):
        _transfer.fetch("https://example.org/data", partial, size=20, sha256="a" * 64)
    assert not partial.exists()
