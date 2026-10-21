"""Catalog assets: verified, resumable, locked downloads and format-aware loading."""

from __future__ import annotations

import fnmatch
import json
import os
import shutil
import types
from pathlib import Path

from . import _catalog, _samples, _transfer
from ._download import _extract, cache_root
from ._loaders import load_content

class LicenseNotAccepted(PermissionError):
    """The asset's license must be read and accepted before downloading."""


class ManualAcquisitionRequired(FileNotFoundError):
    """Open4D cannot fetch this asset; obtain it upstream and pass its path."""


def _license_lines(record):
    lines = []
    for role, value in record.get("licenses", {}).items():
        lines.append(f"  {role}: {value['id']} - {value['url']}")
        lines.append(f"    evidence: {value['evidence']}")
        if value.get("note"):
            lines.append(f"    note: {value['note']}")
    return lines


def describe(record):
    """Human-readable terms, provenance and acquisition steps for a record."""
    hosting = record["hosting"]
    lines = [f"{record['title']} ({record['id']}@{record['version']}, {record['kind']})",
             f"Publisher: {record['publisher']['name']} - {record['publisher']['url']}",
             f"Hosting: {hosting['status']}"
             + (f" ({hosting.get('provider')}: {hosting.get('repository')})"
                if hosting.get("repository") else ""),
             f"Redistribution review: {record['redistribution']['status']} - "
             f"{record['redistribution']['notes']}",
             "Licenses:", *_license_lines(record)]
    if record.get("files"):
        lines.append(f"Download size: {_catalog.total_size(record):,} bytes in "
                     f"{len(record['files'])} file(s)")
    if hosting["status"] == "manual-acquisition":
        lines += ["Manual acquisition: Open4D neither downloads nor redistributes this asset.",
                  "  1. Read and accept the license terms listed above.",
                  f"  2. Obtain it from {hosting['acquisition_url']}",
                  f"     {hosting['instructions']}",
                  f"  3. Register the local copy: open4d data download {record['id']} "
                  "--path <folder>"]
    elif record.get("requires_license_acceptance"):
        lines.append("Downloading requires accepting the licenses above "
                     "(--accept-license, or accept_license=True).")
    lines.append(f"Cite: {record['citation']}")
    return "\n".join(lines)


def _sizes(root):
    result = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"asset cache contains a symlink: {path}")
        if path.is_file():
            result[path.relative_to(root).as_posix()] = path.stat().st_size
        elif not path.is_dir():
            raise ValueError(f"asset cache contains a special file: {path}")
    return result


class Asset:
    """One catalog record with its cache; obtain one with ``open4d.data.get``.

    ``download()`` returns the verified content folder, fetching (or, for a
    pending upload, rebuilding deterministically) on first use. ``load()``
    opens that content with the record's format loader.
    """

    def __init__(self, record, *, cache_dir=None):
        self.record = record
        digest = _catalog.record_digest(record)
        self.cache_dir = (cache_root(cache_dir) / "catalog" / record["id"]
                          / f"{record['version']}-{digest[:16]}")
        self._digest = digest

    id = property(lambda self: self.record["id"])
    version = property(lambda self: self.record["version"])
    kind = property(lambda self: self.record["kind"])
    licenses = property(lambda self: self.record["licenses"])
    metadata = property(lambda self: self.record.get("metadata", {}))
    hosting_status = property(lambda self: self.record["hosting"]["status"])

    def __repr__(self):
        return f"<open4d.data asset {self.id}@{self.version} ({self.kind}, {self.hosting_status})>"

    def describe(self):
        return describe(self.record)

    @property
    def _local(self):
        return self.cache_dir.with_name(self.cache_dir.name + ".local.json")

    @property
    def _lock(self):
        return self.cache_dir.with_name(self.cache_dir.name + ".lock")

    @property
    def path(self):
        """The verified content; downloads only when no license acceptance is needed."""
        return self.download()

    def is_available(self):
        return self.cache_dir.is_dir() or self._local.is_file()

    def load(self, **download_options):
        """Download if needed, then open the content with its format loader."""
        return load_content(self.record, self.download(**download_options))

    def download(self, *, accept_license=False, local_path=None, max_download_bytes=None,
                 max_extracted_bytes=None, max_files=None, lock_timeout=None, progress=None):
        """Return the verified content folder, obtaining it first if needed.

        Limits default to the record's ``limits``; pass larger values to allow
        more. Concurrent callers wait on a lock file; interrupted transfers
        resume from their partial files on the next call.
        """
        status = self.hosting_status
        if status == "manual-acquisition":
            return self._manual(local_path)
        if local_path is not None:
            raise ValueError(f"{self.id} is {status}; local_path applies to manual acquisition")
        if self.cache_dir.exists() or self.cache_dir.is_symlink():
            return self._validated()
        if self.record["requires_license_acceptance"] and not accept_license:
            raise LicenseNotAccepted(
                f"{self.id} requires accepting its license before download:\n"
                + "\n".join(_license_lines(self.record))
                + "\nPass accept_license=True (CLI: --accept-license) once you accept them.")
        limits = _catalog.limits(self.record)
        for name, value in (("max_download_bytes", max_download_bytes),
                            ("max_extracted_bytes", max_extracted_bytes), ("max_files", max_files)):
            if value is not None:
                if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
                    raise ValueError(f"{name} must be a positive integer")
                limits[name] = value
        with _transfer.locked(self._lock, timeout=lock_timeout):
            if self.cache_dir.exists():  # another process finished while we waited
                return self._validated()
            self._obtain(limits, progress)
        return self._validated()

    def verify(self):
        """Re-hash every cached byte against the catalog and archive contents."""
        if self.hosting_status == "manual-acquisition":
            return self._manual(None)
        return self._validated(full=True)

    # Internals -------------------------------------------------------------

    def _obtain(self, limits, progress):
        total = _catalog.total_size(self.record)
        if total > limits["max_download_bytes"]:
            raise ValueError(f"{self.id} downloads {total:,} bytes, above max_download_bytes "
                             f"{limits['max_download_bytes']:,}; raise the limit to proceed")
        work = self.cache_dir.with_name(self.cache_dir.name + ".partial")
        if work.is_symlink():
            raise ValueError(f"download folder is a symlink: {work}")
        downloads, transfers = work / "download", work / "transfers"
        downloads.mkdir(parents=True, exist_ok=True)
        transfers.mkdir(exist_ok=True)
        largest = max(item["size"] for item in self.record["files"])
        pending = sum(item["size"] for item in self.record["files"]
                      if not (downloads / item["name"]).is_file())
        if pending and shutil.disk_usage(work).free < pending + largest:
            raise OSError(f"not enough free space in {work} for {pending + largest:,} bytes "
                          "(downloads plus multipart assembly)")
        digests = {}
        for item in self.record["files"]:
            digests[item["name"]] = self._file(item, downloads, transfers, progress)
        content = work / "content"
        if content.exists():
            shutil.rmtree(content)
        content.mkdir()
        for item in self.record["files"]:
            source = downloads / item["name"]
            used = _sizes(content)
            remaining = types.SimpleNamespace(
                archive=item.get("archive"),
                max_extracted_bytes=limits["max_extracted_bytes"] - sum(used.values()),
                max_files=limits["max_files"] - len(used))
            if item.get("archive"):
                _extract(remaining, source, content)
            else:
                if item["size"] > remaining.max_extracted_bytes or remaining.max_files < 1:
                    raise ValueError(f"{self.id} exceeds its extraction limits")
                try:
                    os.link(source, content / item["name"])
                except OSError:
                    shutil.copyfile(source, content / item["name"])
        shutil.rmtree(transfers)
        provenance = {"record": self.record, "record_digest": self._digest, "files": digests,
                      "content": _sizes(content), "limits": limits,
                      "source": "deterministic local build" if self.hosting_status ==
                      "pending-upload" else "download"}
        (work / "provenance.json").write_text(json.dumps(provenance, indent=1, sort_keys=True))
        work.rename(self.cache_dir)

    def _file(self, item, downloads, transfers, progress):
        target, declared = downloads / item["name"], item.get("sha256")
        if target.is_file() and not target.is_symlink():
            if target.stat().st_size == item["size"] and (
                    declared is None or _transfer.sha256_file(target).hexdigest() == declared):
                return _transfer.sha256_file(target).hexdigest() if declared is None else declared
            target.unlink()
        temporary = transfers / f"{item['name']}.partial"
        if self.hosting_status == "pending-upload":
            data = _samples.build(self.id, self.version).get(item["name"])
            if data is None:
                raise ValueError(f"the deterministic build has no {item['name']}")
            temporary.write_bytes(data)
            if (len(data) != item["size"]
                    or _transfer.sha256_file(temporary).hexdigest() != declared):
                temporary.unlink()
                raise ValueError(f"local build of {item['name']} does not reproduce the "
                                 "catalog digest; the generator or catalog changed")
            os.replace(temporary, target)
            return declared
        if item.get("parts"):
            paths = []
            for number, part in enumerate(item["parts"]):
                path = transfers / f"{item['name']}.{number:03d}"
                done = path.with_name(path.name + ".ok")
                if not done.is_file():
                    _transfer.fetch(part["url"], path, size=part["size"],
                                    sha256=part["sha256"], progress=progress)
                    done.write_text(part["sha256"])
                paths.append(path)
            digest = _transfer.assemble(paths, temporary, size=item["size"], sha256=declared)
            for path in paths:
                path.unlink()
                path.with_name(path.name + ".ok").unlink()
        else:
            _transfer.fetch(item["url"], temporary, size=item["size"], sha256=declared,
                            progress=progress)
            digest = declared
        os.replace(temporary, target)
        return digest

    def _validated(self, full=None):
        root = self.cache_dir
        try:
            if root.is_symlink() or not root.is_dir():
                raise ValueError("cache is not a folder")
            provenance_path = root / "provenance.json"
            if provenance_path.is_symlink():
                raise ValueError("provenance is a symlink")
            provenance = json.loads(provenance_path.read_text())
            if provenance.get("record_digest") != self._digest or provenance.get("record") != self.record:
                raise ValueError("cached record differs from the catalog")
            downloads, content = root / "download", root / "content"
            if _sizes(downloads) != {item["name"]: item["size"] for item in self.record["files"]}:
                raise ValueError("cached downloads differ from the catalog")
            if _sizes(content) != provenance.get("content"):
                raise ValueError("cached content differs from its provenance")
            if full is None:
                full = True
            if full:
                expected = {}
                limits = types.SimpleNamespace(**provenance["limits"])
                for item in self.record["files"]:
                    source = downloads / item["name"]
                    digest = _transfer.sha256_file(source).hexdigest()
                    if digest != provenance["files"].get(item["name"]) or (
                            item.get("sha256") not in (None, digest)):
                        raise ValueError(f"checksum mismatch for {item['name']}")
                    if item.get("archive"):
                        limits.archive = item["archive"]
                        expected.update(_extract(limits, source, content, verify_only=True))
                    else:
                        expected[item["name"]] = digest
                actual = {name: _transfer.sha256_file(content / name).hexdigest()
                          for name in provenance["content"]}
                if actual != expected:
                    raise ValueError("cached content does not match the verified archives")
        except (OSError, ValueError, KeyError, TypeError) as error:
            raise ValueError(f"invalid asset cache {root}: {error}; remove it to fetch "
                             "again") from error
        return content

    def _manual(self, local_path):
        if local_path is None:
            if not self._local.is_file():
                raise ManualAcquisitionRequired(describe(self.record))
            registered = json.loads(self._local.read_text())
            path = Path(registered["path"])
            if (registered.get("record_digest") != self._digest or not path.exists()
                    or (_sizes(path) if path.is_dir() else {path.name: path.stat().st_size})
                    != registered["sizes"]):
                raise ValueError(f"registered local copy of {self.id} changed or is missing: "
                                 f"{path}; register it again with local_path=")
            return path
        path = Path(local_path).expanduser().resolve()
        if not path.exists():
            raise FileNotFoundError(f"local copy does not exist: {path}")
        sizes = _sizes(path) if path.is_dir() else {path.name: path.stat().st_size}
        for pattern in self.record["hosting"].get("expected_entries", ()):
            if not any(fnmatch.fnmatch(name, pattern) for name in sizes):
                raise ValueError(f"{path} does not look like {self.id}: nothing matches "
                                 f"{pattern!r}")
        declared = {item["name"]: item for item in self.record.get("files", ())
                    if item.get("sha256")}
        for name, item in declared.items():
            candidate = path / name if path.is_dir() else path
            if candidate.is_file() and _transfer.sha256_file(candidate).hexdigest() != item["sha256"]:
                raise ValueError(f"{candidate} does not match the catalog SHA-256")
        self._local.parent.mkdir(parents=True, exist_ok=True)
        temporary = self._local.with_name(self._local.name + ".tmp")
        temporary.write_text(json.dumps({"path": str(path), "record_digest": self._digest,
                                         "sizes": sizes}, sort_keys=True))
        os.replace(temporary, self._local)
        return path


class AssetPair:
    """A dataset and the checkpoint trained on it, resolved by id and version."""

    kind = "dataset-checkpoint-pair"

    def __init__(self, record, *, cache_dir=None):
        self.record = record
        members = record["members"]
        self.dataset = get(members["dataset"]["id"], members["dataset"]["version"],
                           cache_dir=cache_dir)
        self.checkpoint = get(members["checkpoint"]["id"], members["checkpoint"]["version"],
                              cache_dir=cache_dir)

    id = property(lambda self: self.record["id"])
    version = property(lambda self: self.record["version"])
    hosting_status = property(lambda self: self.record["hosting"]["status"])

    def __repr__(self):
        return f"<open4d.data pair {self.id}@{self.version}: {self.dataset!r} + {self.checkpoint!r}>"

    def describe(self):
        return "\n\n".join((describe(self.record), self.dataset.describe(),
                            self.checkpoint.describe()))

    def download(self, *, dataset_path=None, **options):
        """Return ``{"dataset": path, "checkpoint": path}``."""
        return {"dataset": self.dataset.download(local_path=dataset_path)
                if self.dataset.hosting_status == "manual-acquisition"
                else self.dataset.download(**options),
                "checkpoint": self.checkpoint.download(**options)}

    def load(self, **_options):
        raise TypeError("a dataset-checkpoint pair has no single loader; use "
                        ".dataset.load() and .checkpoint.load()")


def get(asset_id, version=None, *, cache_dir=None):
    """Return the catalog asset ``asset_id`` (newest version unless given)."""
    record = _catalog.find(asset_id, version)
    if record["kind"] == "dataset-checkpoint-pair":
        return AssetPair(record, cache_dir=cache_dir)
    return Asset(record, cache_dir=cache_dir)


def catalog(kind=None):
    """All catalog records, optionally of one kind (copies)."""
    if kind is not None and kind not in _catalog.KINDS:
        raise ValueError(f"kind must be one of {', '.join(_catalog.KINDS)}")
    return [item for item in _catalog.records() if kind in (None, item["kind"])]
