"""Offline dataset download, cache integrity and hostile archive regressions."""

import hashlib
import http.server
import io
import json
import stat
import tarfile
import threading
import urllib.request
import zipfile
from dataclasses import FrozenInstanceError, replace

import numpy as np
import pytest

from open4d.core import Representation
from open4d.data import (DatasetSpec, DownloadableDataset,
                         SyntheticGaussianSequence, SyntheticMeshSequence)
from open4d.data import _download


@pytest.fixture
def server(tmp_path, monkeypatch):
    requests = []
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            payload = (tmp_path / "remote").read_bytes()
            self.send_response(200)
            self.end_headers()
            self.wfile.write(payload)
        def log_message(self, *_args):
            pass
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    original = urllib.request.build_opener()
    class Response:
        def __init__(self, response, url):
            self.response, self.url = response, url
        def __enter__(self):
            return self
        def __exit__(self, *_args):
            self.response.close()
        def read(self, size):
            return self.response.read(size)
        def geturl(self):
            return self.url
    class Opener:
        def open(self, request, timeout):
            # HTTPS policy remains active; only the test transport is local HTTP.
            response = original.open(f"http://127.0.0.1:{httpd.server_port}/asset", timeout=timeout)
            return Response(response, request.full_url)
    monkeypatch.setattr(_download.urllib.request, "build_opener", lambda *_: Opener())
    try:
        yield tmp_path / "remote", tmp_path / "cache", requests
    finally:
        httpd.shutdown()
        httpd.server_close()
        thread.join()


def spec(payload, **kwargs):
    return DatasetSpec(name="test", url="https://example.org/asset",
                       sha256=hashlib.sha256(payload).hexdigest(), license="MIT",
                       license_url="https://example.org/LICENSE", kind="mesh", **kwargs)


def zipped(entries):
    data = io.BytesIO()
    with zipfile.ZipFile(data, "w") as archive:
        for name, value in entries:
            archive.writestr(name, value)
    return data.getvalue()


def test_download_lazy_atomic_cached_and_pinned(server):
    remote, cache, requests = server
    payload = b"licensed synthetic fixture"
    remote.write_bytes(payload)
    item = DownloadableDataset(spec(payload, filename="sample.obj"), cache_dir=cache)
    assert not cache.exists() and not requests
    assert item.path.read_bytes() == payload
    assert item.path.read_bytes() == payload and len(requests) == 1
    with pytest.raises(FrozenInstanceError):
        item.spec.license = "unknown"
    item.path.write_bytes(b"corrupted")
    record = json.loads((item.cache_dir / "provenance.json").read_text())
    record["files"]["sample.obj"] = hashlib.sha256(b"corrupted").hexdigest()
    (item.cache_dir / "provenance.json").write_text(json.dumps(record))
    with pytest.raises(ValueError, match="provenance/content mismatch"):
        item.path


@pytest.mark.parametrize("mutation", [
    {"url": "http://example.org/data"}, {"url": "https://u:p@example.org/data"},
    {"sha256": "not-a-digest"}, {"license": ""}, {"name": "../outside"},
    {"filename": "../outside"}, {"kind": "unknown"}, {"max_files": True},
    {"license_url": "http://example.org/LICENSE"},
])
def test_metadata_rejects_unpinned_or_unlicensed_inputs(mutation):
    with pytest.raises(ValueError):
        replace(spec(b""), **mutation)


@pytest.mark.parametrize("member", ["../outside", "/outside", "a/../../outside",
                                         "C:/outside", "a\\outside"])
def test_zip_traversal_rejected_without_cache(server, member):
    remote, cache, _ = server
    payload = zipped([(member, b"bad")])
    remote.write_bytes(payload)
    item = DownloadableDataset(spec(payload, archive="zip"), cache_dir=cache)
    with pytest.raises(ValueError, match="unsafe archive member"):
        item.path
    assert not item.cache_dir.exists()
    assert list(cache.iterdir()) == []


def test_zip_link_and_bomb_limits(server):
    remote, cache, _ = server
    member = zipfile.ZipInfo("link")
    member.create_system = 3
    member.external_attr = (stat.S_IFLNK | 0o777) << 16
    payload = zipped([(member, b"outside")])
    remote.write_bytes(payload)
    with pytest.raises(ValueError, match="links and special"):
        DownloadableDataset(spec(payload, archive="zip"), cache_dir=cache).path
    payload = zipped([("a", b"x" * 100)])
    remote.write_bytes(payload)
    with pytest.raises(ValueError, match="extraction limits"):
        DownloadableDataset(spec(payload, archive="zip", max_extracted_bytes=10), cache_dir=cache).path


def test_archive_content_checked_against_pinned_source(server):
    remote, cache, _ = server
    payload = zipped([("nested/a.obj", b"v 0 0 0"), ("b.obj", b"v 1 0 0")])
    remote.write_bytes(payload)
    item = DownloadableDataset(spec(payload, archive="zip"), cache_dir=cache)
    (item.path / "b.obj").write_bytes(b"edited")
    record = json.loads((item.cache_dir / "provenance.json").read_text())
    record["files"]["b.obj"] = hashlib.sha256(b"edited").hexdigest()
    (item.cache_dir / "provenance.json").write_text(json.dumps(record))
    with pytest.raises(ValueError, match="provenance/content mismatch"):
        item.path


def test_tar_regular_files_and_forbidden_links(server):
    remote, cache, _ = server
    def tar(link=False):
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w:gz") as archive:
            member = tarfile.TarInfo("frame.obj")
            if link:
                member.type, member.linkname = tarfile.SYMTYPE, "../outside"
                archive.addfile(member)
            else:
                member.size = 3
                archive.addfile(member, io.BytesIO(b"obj"))
        return buffer.getvalue()
    payload = tar()
    remote.write_bytes(payload)
    assert (DownloadableDataset(spec(payload, archive="tar"), cache_dir=cache).path / "frame.obj").read_bytes() == b"obj"
    payload = tar(link=True)
    remote.write_bytes(payload)
    with pytest.raises(ValueError, match="links and special"):
        DownloadableDataset(spec(payload, archive="tar"), cache_dir=cache).path


def test_checksum_download_size_and_redirect_guards(server):
    remote, cache, _ = server
    remote.write_bytes(b"wrong")
    with pytest.raises(ValueError, match="SHA-256 mismatch"):
        DownloadableDataset(spec(b"right"), cache_dir=cache).path
    with pytest.raises(ValueError, match="max_download_bytes"):
        DownloadableDataset(spec(b"wrong", max_download_bytes=2), cache_dir=cache).path
    with pytest.raises(ValueError, match="max_extracted_bytes"):
        DownloadableDataset(spec(b"wrong", max_extracted_bytes=2), cache_dir=cache).path
    with pytest.raises(ValueError, match="HTTPS"):
        _download._HTTPSRedirect().redirect_request(
            urllib.request.Request("https://example.org"), None, 302, "", {}, "http://example.org")
    assert list(cache.iterdir()) == []


def test_duplicate_archive_names_count_limit_and_cache_symlinks(server):
    remote, cache, _ = server
    payload = zipped([("a", b"one"), ("b", b"two")])
    remote.write_bytes(payload)
    with pytest.raises(ValueError, match="extraction limits"):
        DownloadableDataset(spec(payload, archive="zip", max_files=1), cache_dir=cache).path
    item = DownloadableDataset(spec(payload, archive="zip"), cache_dir=cache)
    folder = item.path
    outside = cache / "outside"
    outside.write_bytes(b"one")
    (folder / "a").unlink()
    (folder / "a").symlink_to(outside)
    with pytest.raises(ValueError, match="symlink|leaves extraction folder"):
        item.path
    with pytest.warns(UserWarning, match="Duplicate name"):
        payload = zipped([("a", b"one"), ("a", b"two")])
    remote.write_bytes(payload)
    with pytest.raises(ValueError, match="duplicate archive member"):
        DownloadableDataset(spec(payload, archive="zip"), cache_dir=cache).path


def test_synthetic_sequences_are_mit_usable_and_verified(tmp_path, monkeypatch):
    monkeypatch.setenv("OPEN4D_DATA_DIR", str(tmp_path))
    mesh = SyntheticMeshSequence()
    assert not mesh.cache_dir.exists()
    with mesh.load() as sequence:
        assert len(sequence) == 8
        assert sequence[0].geometry.representation is Representation.MESH
        assert not np.array_equal(sequence[0].geometry.positions, sequence[1].geometry.positions)
    gaussian = SyntheticGaussianSequence()
    with gaussian.load() as sequence:
        assert sequence[0].geometry.representation is Representation.GAUSSIANS
        assert len(sequence) == 8
        assert sequence.metadata["license"] == "MIT" and sequence.metadata["synthetic"]
    assert mesh.path.is_dir() and gaussian.path.is_dir()
    (gaussian.path / "frame_0000.npz").write_bytes(b"corrupt")
    with pytest.raises(ValueError, match="invalid synthetic"):
        gaussian.path
