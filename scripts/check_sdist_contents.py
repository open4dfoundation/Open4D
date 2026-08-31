#!/usr/bin/env python3
"""Reject research, generated, test, or binary content in the source archive."""

from __future__ import annotations

import argparse
import sys
import tarfile
from pathlib import Path, PurePosixPath

from check_wheel_contents import expected_python_files


ALLOWED_ROOT_FILES = {
    "CONTRIBUTING.md",
    "LICENSE",
    "MANIFEST.in",
    "PKG-INFO",
    "README.md",
    "SECURITY.md",
    "THIRD_PARTY.md",
    "pyproject.toml",
    "setup.cfg",
}
ALLOWED_EGG_INFO = {
    "PKG-INFO",
    "SOURCES.txt",
    "dependency_links.txt",
    "requires.txt",
    "top_level.txt",
}


def check_sdist(path: Path) -> list[str]:
    if not path.is_file():
        return [f"source distribution does not exist: {path}"]
    errors: list[str] = []
    expected_python = expected_python_files()
    with tarfile.open(path, "r:gz") as archive:
        files = [member for member in archive.getmembers() if member.isfile()]
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
        if name in ALLOWED_ROOT_FILES:
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
