#!/usr/bin/env python3
"""Check the separate streamer wheel/sdist, including Draco bytes and notices."""

from __future__ import annotations

import argparse
from email.parser import BytesParser
import hashlib
from pathlib import Path, PurePosixPath
import tarfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "open4d" / "streamer"
VENDOR = "streamer/client/vendor/draco/"
DRACO_DIGESTS = {
    "draco_wasm_wrapper.js": "e8049906ef3f8f75d3456c22a3f31bfdfe5b5b5bd09ccdec613b9e9a49d554d8",
    "draco_decoder.wasm": "2516a4e43526d71787bf2f678f951329f7f858f8f15f42d4bc9e370b31a0da3a",
    "LICENSE": "d3709b0fb4b8a94bbb1d02b8a2e484f258b0d9c5c5a01f940391f3fe662cd1a4",
    "AUTHORS": "7399754545fa82fbf402e3e91d50de0b63772870fd807c48dd9af25c042efb10",
}
LICENSE_FILES = ("LICENSE", VENDOR + "LICENSE", VENDOR + "NOTICE", VENDOR + "AUTHORS")
FORBIDDEN = {"study", "streamer_tests", "tests", "benchmarks", "node_modules", ".git", "__pycache__"}


def _members(archive):
    if archive.suffix == ".whl":
        with zipfile.ZipFile(archive) as package:
            return [(item.filename, package.read(item)) for item in package.infolist()
                    if not item.is_dir()]
    with tarfile.open(archive, "r:gz") as package:
        records = []
        for item in package.getmembers():
            if item.isdir():
                continue
            if not item.isfile():
                raise ValueError(f"unsupported archive member: {item.name}")
            records.append((item.name, package.extractfile(item).read()))
        return records


def check_archive(archive: Path, project: Path = PROJECT) -> list[str]:
    """Return violations; inspect without extracting potentially unsafe archives."""
    errors, contents = [], {}
    wheel = archive.suffix == ".whl"
    try:
        records = _members(archive)
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile) as error:
        return [f"cannot read {archive.name}: {error}"]
    roots = set()
    for filename, data in records:
        path = PurePosixPath(filename)
        if path.is_absolute() or ".." in path.parts or "\\" in filename:
            errors.append(f"unsafe member: {filename}")
            continue
        if not wheel:
            roots.add(path.parts[0])
            path = PurePosixPath(*path.parts[1:])
        name = path.as_posix()
        if name in contents:
            errors.append(f"duplicate member: {name}")
        contents[name] = data
        if FORBIDDEN.intersection(path.parts):
            errors.append(f"excluded research/test content: {name}")
    if not wheel and len(roots) != 1:
        errors.append("sdist must contain exactly one project root")

    expected = {
        path.relative_to(project).as_posix(): path.read_bytes()
        for path in (project / "streamer").rglob("*.py")
        if not FORBIDDEN.intersection(path.relative_to(project).parts)
    }
    for name in ("streamer/client/viewer.html", "streamer/client/worker.js",
                 *(VENDOR + item for item in (*DRACO_DIGESTS, "NOTICE", "README.md"))):
        expected[name] = (project / name).read_bytes()
    if not wheel:
        for name in ("LICENSE", "README.md", "pyproject.toml", "MANIFEST.in"):
            expected[name] = (project / name).read_bytes()
    for name, original in expected.items():
        if name not in contents:
            errors.append(f"missing runtime/license file: {name}")
        elif contents[name] != original:
            errors.append(f"packaged file differs from source: {name}")
    for name, digest in DRACO_DIGESTS.items():
        if VENDOR + name in contents and hashlib.sha256(contents[VENDOR + name]).hexdigest() != digest:
            errors.append(f"Draco upstream digest mismatch: {name}")

    if wheel:
        metadata_roots = {name.split("/")[0] for name in contents if ".dist-info/" in name}
        if len(metadata_roots) != 1:
            errors.append("wheel must contain exactly one metadata directory")
        metadata_root = next(iter(metadata_roots), "missing.dist-info")
        metadata_path = metadata_root + "/METADATA"
        allowed_metadata = {metadata_root + "/" + name for name in ("METADATA", "WHEEL", "RECORD", "top_level.txt")}
        for name in LICENSE_FILES:
            target = metadata_root + "/licenses/" + name
            allowed_metadata.add(target)
            if contents.get(target) != (project / name).read_bytes():
                errors.append(f"missing or changed wheel license attribution: {target}")
    else:
        metadata_path = "PKG-INFO"
        allowed_metadata = {"PKG-INFO", "setup.cfg"} | {
            "open4d_streamer.egg-info/" + name for name in
            ("PKG-INFO", "SOURCES.txt", "dependency_links.txt", "requires.txt", "top_level.txt")
        }
    for name in contents.keys() - expected.keys() - allowed_metadata:
        errors.append(f"unexpected packaged file: {name}")
    metadata = BytesParser().parsebytes(contents.get(metadata_path, b""))
    if metadata.get("License-Expression") != "MIT AND Apache-2.0":
        errors.append("package metadata must declare MIT AND Apache-2.0")
    if set(metadata.get_all("License-File", [])) != set(LICENSE_FILES):
        errors.append("package metadata must declare all project/Draco license and attribution files")
    return sorted(set(errors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives", type=Path, nargs="+")
    args = parser.parse_args()
    violations = []
    for archive in args.archives:
        errors = check_archive(archive)
        if errors:
            violations.extend(f"{archive.name}: {error}" for error in errors)
        else:
            print(f"Verified streamer contents and Draco provenance: {archive.name}")
    if violations:
        parser.exit(1, "\n".join(violations) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
