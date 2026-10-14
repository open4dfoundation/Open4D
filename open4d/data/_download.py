"""Bounded, atomic downloads and extraction with immutable provenance."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import stat
import tarfile
import tempfile
import urllib.parse
import urllib.request
import zipfile
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath


def _https(url):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password:
        raise ValueError("asset and license URLs must be HTTPS without credentials")


@dataclass(frozen=True)
class DatasetSpec:
    """Immutable download provenance, limits, and independent asset rights.

    ``kind`` is mesh, gaussian, or model. ``license`` is a nonempty SPDX ID or
    descriptive license identifier; ``license_url`` points to its actual terms.
    Supplying metadata does not grant redistribution rights. Archive members
    must be regular files/directories; links and device entries are rejected.
    """

    name: str
    url: str
    sha256: str
    license: str
    license_url: str
    kind: str
    filename: str = "asset"
    archive: str | None = None
    max_download_bytes: int = 1_073_741_824
    max_extracted_bytes: int = 4_294_967_296
    max_files: int = 100_000

    def __post_init__(self):
        for name in ("name", "filename"):
            if not isinstance(getattr(self, name), str) or not re.fullmatch(
                    r"[A-Za-z0-9][A-Za-z0-9._-]*", getattr(self, name)):
                raise ValueError(f"{name} must be a safe single path component")
        _https(self.url)
        _https(self.license_url)
        if not isinstance(self.sha256, str) or not re.fullmatch(r"[a-f0-9]{64}", self.sha256):
            raise ValueError("sha256 must be a lowercase SHA-256 digest")
        if not isinstance(self.license, str) or not self.license.strip():
            raise ValueError("an explicit data/model license is required")
        if self.kind not in {"mesh", "gaussian", "model"}:
            raise ValueError("kind must be mesh, gaussian, or model")
        if self.archive not in {None, "zip", "tar"}:
            raise ValueError("archive must be None, zip, or tar")
        for name in ("max_download_bytes", "max_extracted_bytes", "max_files"):
            value = getattr(self, name)
            if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
                raise ValueError(f"{name} must be a positive integer")


def cache_root(path=None):
    return Path(path or os.environ.get("OPEN4D_DATA_DIR", "~/.open4d/data")).expanduser().resolve()


def _hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class _HTTPSRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        _https(newurl)
        return super().redirect_request(request, fp, code, msg, headers, newurl)


def _download(spec, target):
    opener = urllib.request.build_opener(_HTTPSRedirect())
    request = urllib.request.Request(spec.url, headers={"User-Agent": "Open4D-data/1"})
    count = 0
    with opener.open(request, timeout=60) as response, target.open("xb") as output:
        _https(response.geturl())
        for chunk in iter(lambda: response.read(1024 * 1024), b""):
            count += len(chunk)
            if count > spec.max_download_bytes:
                raise ValueError("dataset download exceeds max_download_bytes")
            output.write(chunk)
    if _hash(target) != spec.sha256:
        raise ValueError("dataset SHA-256 mismatch")


def _member(root, name):
    path = PurePosixPath(name)
    if "\\" in name or not name or not path.parts or path.is_absolute() or any(
            part in {"..", "."} or ":" in part for part in path.parts):
        raise ValueError(f"unsafe archive member: {name}")
    target = root.joinpath(*path.parts)
    if not target.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"archive member leaves extraction folder: {name}")
    return target


def _extract(spec, source, root, *, verify_only=False):
    total, count, seen = 0, 0, set()
    checksums = {}

    def copy_member(stream, target, size):
        if verify_only:
            digest = hashlib.sha256()
            count = 0
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                count += len(chunk)
                if count > size:
                    raise ValueError("archive member exceeds declared size")
                digest.update(chunk)
            if count != size:
                raise ValueError("archive member has incorrect size")
            checksums[target.relative_to(root).as_posix()] = digest.hexdigest()
        else:
            with target.open("xb") as output:
                _copy(stream, output, size)

    def destination(name, size, directory):
        nonlocal total, count
        target = _member(root, name)
        if target in seen:
            raise ValueError(f"duplicate archive member: {name}")
        seen.add(target)
        count += 1
        total += size
        if count > spec.max_files or total > spec.max_extracted_bytes:
            raise ValueError("archive exceeds extraction limits")
        if verify_only:
            return target
        if directory:
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
        return target

    if spec.archive == "zip":
        with zipfile.ZipFile(source) as archive:
            for member in archive.infolist():
                mode = member.external_attr >> 16
                if stat.S_ISLNK(mode) or (stat.S_IFMT(mode) not in
                        {0, stat.S_IFREG, stat.S_IFDIR}):
                    raise ValueError("archive links and special files are forbidden")
                target = destination(member.filename, member.file_size, member.is_dir())
                if not member.is_dir():
                    with archive.open(member) as stream:
                        copy_member(stream, target, member.file_size)
    else:
        with tarfile.open(source, mode="r:*") as archive:
            for member in archive:
                if not member.isfile() and not member.isdir():
                    raise ValueError("archive links and special files are forbidden")
                target = destination(member.name, member.size, member.isdir())
                if member.isfile():
                    with archive.extractfile(member) as stream:
                        copy_member(stream, target, member.size)
    return checksums


def _copy(source, output, size):
    count = 0
    for chunk in iter(lambda: source.read(1024 * 1024), b""):
        count += len(chunk)
        if count > size:
            raise ValueError("archive member exceeds declared size")
        output.write(chunk)
    if count != size:
        raise ValueError("archive member has incorrect size")


def _inventory(root):
    result = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError("dataset cache contains a symlink")
        if path.is_file():
            result[path.relative_to(root).as_posix()] = _hash(path)
        elif not path.is_dir():
            raise ValueError("dataset cache contains a special file")
    return result


class DownloadableDataset:
    """Download on first ``path`` access and validate cached contents every use.

    ``path`` is the downloaded file, or the extracted directory for an archive.
    ``download()`` is an explicit equivalent. Corrupt caches raise an error;
    callers can remove the indicated cache directory and retry deliberately.
    Downloads happen in a sibling staging directory before atomic publication.
    """

    def __init__(self, spec: DatasetSpec, *, cache_dir=None):
        if not isinstance(spec, DatasetSpec):
            raise TypeError("spec must be a DatasetSpec")
        self.spec = spec
        identity = hashlib.sha256(json.dumps(asdict(spec), sort_keys=True).encode()).hexdigest()[:16]
        self.cache_dir = cache_root(cache_dir) / f"{spec.name}-{identity}"

    @property
    def path(self):
        return self.download()

    def _validated_path(self):
        root = self.cache_dir
        if root.is_symlink():
            raise ValueError(f"dataset cache is a symlink: {root}")
        try:
            provenance = root / "provenance.json"
            if provenance.is_symlink():
                raise ValueError("dataset provenance is a symlink")
            record = json.loads(provenance.read_text())
            source = root / "download"
            content = root / "content"
            if (source.is_symlink() or not source.is_file() or content.is_symlink()
                    or source.stat().st_size > self.spec.max_download_bytes
                    or _hash(source) != self.spec.sha256):
                raise ValueError("cached download checksum mismatch")
            expected = (_extract(self.spec, source, content, verify_only=True)
                        if self.spec.archive else {self.spec.filename: self.spec.sha256})
            if (not content.is_dir() or record.get("spec") != asdict(self.spec)
                    or record.get("files") != expected or expected != _inventory(content)):
                raise ValueError("cached dataset provenance/content mismatch")
        except (OSError, ValueError, KeyError, TypeError) as error:
            raise ValueError(f"invalid dataset cache {root}: {error}") from error
        return content if self.spec.archive else content / self.spec.filename

    def download(self):
        if self.cache_dir.exists() or self.cache_dir.is_symlink():
            return self._validated_path()
        self.cache_dir.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".open4d-data-", dir=self.cache_dir.parent) as temp:
            staging = Path(temp) / "dataset"
            staging.mkdir()
            source = staging / "download"
            _download(self.spec, source)
            content = staging / "content"
            content.mkdir()
            if self.spec.archive:
                _extract(self.spec, source, content)
            else:
                if source.stat().st_size > self.spec.max_extracted_bytes:
                    raise ValueError("dataset exceeds max_extracted_bytes")
                shutil.copyfile(source, content / self.spec.filename)
            record = {"spec": asdict(self.spec), "files": _inventory(content)}
            (staging / "provenance.json").write_text(json.dumps(record, sort_keys=True))
            try:
                staging.rename(self.cache_dir)
            except OSError:
                if not self.cache_dir.exists():
                    raise
        return self._validated_path()
