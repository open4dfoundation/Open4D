#!/usr/bin/env python3
"""Reject research, generated, test, or binary content in the source archive."""

from __future__ import annotations

import argparse
import posixpath
import sys
import tarfile
from pathlib import Path, PurePosixPath

from check_wheel_contents import expected_python_files, expected_data_files


ALLOWED_ROOT_FILES = {
    "docs/CHANGELOG.md",
    "docs/CODE_OF_CONDUCT.md",
    "docs/CONTRIBUTING.md",
    "LICENSE",
    "MANIFEST.in",
    "PKG-INFO",
    "README.md",
    "docs/SECURITY.md",
    "docs/THIRD_PARTY.md",
    "pyproject.toml",
    "setup.cfg",
}
ALLOWED_EGG_INFO = {
    "PKG-INFO",
    "SOURCES.txt",
    "dependency_links.txt",
    "entry_points.txt",
    "requires.txt",
    "top_level.txt",
}


def unsafe_member_errors(member: tarfile.TarInfo) -> list[str]:
    """Reject links, special files and names that leave the archive root."""
    name = member.name
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name:
        return [f"unsafe member path: {name}"]
    if member.issym() or member.islnk():
        target = posixpath.normpath(posixpath.join(posixpath.dirname(name), member.linkname)) \
            if member.issym() else posixpath.normpath(member.linkname)
        root = path.parts[0] if path.parts else ""
        escapes = (posixpath.isabs(member.linkname) or target == ".."
                   or target.startswith("../") or target.split("/", 1)[0] != root)
        kind = "symlink" if member.issym() else "hard link"
        where = "escaping the source tree" if escapes else "inside the source tree"
        return [f"{kind} {where} is not allowed: {name} -> {member.linkname}"]
    if not (member.isfile() or member.isdir()):
        return [f"special file is not allowed: {name}"]
    return []


def check_sdist(path: Path) -> list[str]:
    if not path.is_file():
        return [f"source distribution does not exist: {path}"]
    errors: list[str] = []
    expected_python = expected_python_files()
    with tarfile.open(path, "r:gz") as archive:
        members = archive.getmembers()
        for member in members:
            errors.extend(unsafe_member_errors(member))
        files = [member for member in members
                 if member.isfile() and not unsafe_member_errors(member)]
        raw_names = [member.name for member in files]
        if len(raw_names) != len(set(raw_names)):
            seen = set()
            for name in raw_names:
                if name in seen:
                    errors.append(f"duplicate archive member: {name}")
                seen.add(name)

        stripped_list: list[str] = []
        for member in files:
            parts = PurePosixPath(member.name).parts
            if len(parts) < 2:
                errors.append(f"member has no distribution root: {member.name}")
                continue
            stripped_path = PurePosixPath(*parts[1:]).as_posix()
            stripped_list.append(stripped_path)

        if len(stripped_list) != len(set(stripped_list)):
            seen = set()
            for name in stripped_list:
                if name in seen:
                    errors.append(f"duplicate root-normalized path: {name}")
                seen.add(name)

        stripped: set[str] = set(stripped_list)

    python_files = {name for name in stripped if name.endswith(".py")}
    if python_files != expected_python:
        errors.append(
            "source Python inventory differs: "
            f"missing={sorted(expected_python - python_files)}, "
            f"extra={sorted(python_files - expected_python)}"
        )

    for name in stripped - python_files:
        parts = PurePosixPath(name).parts
        if name in ALLOWED_ROOT_FILES or name in expected_data_files():
            continue
        if len(parts) == 2 and parts[0].endswith(".egg-info") and parts[1] in ALLOWED_EGG_INFO:
            continue
        errors.append(f"unexpected source-distribution member: {name}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("sdist", type=Path)
    args = parser.parse_args()
    errors = check_sdist(args.sdist)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"Source boundary verified: {args.sdist}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
