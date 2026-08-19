#!/usr/bin/env python3
"""Assert the exact contents and metadata of the lightweight wheel."""

from __future__ import annotations

import argparse
import sys
import zipfile
from email.parser import Parser
from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib  # type: ignore[no-redef]


ROOT = Path(__file__).resolve().parents[1]
PACKAGE_DIRS = {
    "open4d": ROOT / "open4d",
    "open4d.core": ROOT / "open4d/core",
    "open4d.io": ROOT / "open4d/io",
    "open4d.torch_ops": ROOT / "open4d/torch_ops",
    "integrations": ROOT / "integrations",
    "integrations.open3d": ROOT / "integrations/open3d",
}


def configured_packages() -> set[str]:
    """
    Read the package names configured in the project's `pyproject.toml`.
    
    Returns:
        set[str]: The configured setuptools package names.
    """
    with (ROOT / "pyproject.toml").open("rb") as stream:
        return set(tomllib.load(stream)["tool"]["setuptools"]["packages"])


def expected_python_files() -> set[str]:
    """
    Build the set of Python file paths expected in the wheel archive.
    
    Returns:
    	set[str]: Archive paths for Python files found in the configured package directories.
    """
    expected: set[str] = set()
    for package, directory in PACKAGE_DIRS.items():
        archive_directory = package.replace(".", "/")
        expected.update(
            f"{archive_directory}/{source.name}" for source in directory.glob("*.py")
        )
    return expected


def check_wheel(path: Path) -> list[str]:
    """
    Validate a wheel's contents and metadata against the expected lightweight package layout.
    
    Parameters:
        path (Path): Path to the wheel file to validate.
    
    Returns:
        list[str]: Validation error messages, empty when the wheel passes all checks.
    """
    errors: list[str] = []
    if configured_packages() != set(PACKAGE_DIRS):
        errors.append("pyproject package list differs from the wheel checker")
    if not path.is_file():
        return errors + [f"wheel does not exist: {path}"]

    with zipfile.ZipFile(path) as wheel:
        namelist = [name for name in wheel.namelist() if not name.endswith("/")]
        if len(namelist) != len(set(namelist)):
            seen = set()
            for name in namelist:
                if name in seen:
                    errors.append(f"duplicate wheel member: {name}")
                seen.add(name)

        members = set(namelist)
        python_files = {name for name in members if name.endswith(".py")}
        expected_python = expected_python_files()
        if expected_python - python_files:
            errors.append(f"missing Python files: {sorted(expected_python - python_files)}")
        if python_files - expected_python:
            errors.append(f"unexpected Python files: {sorted(python_files - expected_python)}")

        dist_info = {
            name.split("/", 1)[0]
            for name in members
            if name.split("/", 1)[0].endswith(".dist-info")
        }
        if len(dist_info) != 1:
            return errors + [f"expected one .dist-info directory, found {sorted(dist_info)}"]
        metadata_dir = next(iter(dist_info))
        expected_metadata = {
            f"{metadata_dir}/METADATA",
            f"{metadata_dir}/WHEEL",
            f"{metadata_dir}/RECORD",
            f"{metadata_dir}/top_level.txt",
            f"{metadata_dir}/licenses/LICENSE",
        }
        missing_metadata = expected_metadata - members
        if missing_metadata:
            errors.append(f"missing wheel metadata: {sorted(missing_metadata)}")
        unexpected = members - expected_python - expected_metadata
        if unexpected:
            errors.append(f"unexpected wheel members: {sorted(unexpected)}")

        metadata_path = f"{metadata_dir}/METADATA"
        if metadata_path in members:
            metadata = Parser().parsestr(wheel.read(metadata_path).decode("utf-8"))
            base_requirements = [
                value
                for value in metadata.get_all("Requires-Dist", [])
                if "extra ==" not in value
            ]
            if metadata.get("Name", "").lower() != "open4d":
                errors.append(f"unexpected distribution name: {metadata.get('Name')!r}")
            if base_requirements != ["numpy"]:
                errors.append(f"base wheel is not NumPy-only: {base_requirements}")
            if metadata.get("License-Expression") != "MIT":
                errors.append("wheel must use the MIT SPDX license expression")
            if metadata.get("Requires-Python") != "<3.14,>=3.10":
                errors.append(
                    f"unexpected Python requirement: {metadata.get('Requires-Python')!r}"
                )
    return errors


def main() -> int:
    """Validate the specified wheel and report whether its contents meet the expected boundary.
    
    Returns:
    	int: `1` when validation errors are found, otherwise `0`.
    """
    parser = argparse.ArgumentParser()
    parser.add_argument("wheel", type=Path)
    args = parser.parse_args()
    errors = check_wheel(args.wheel)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"Wheel boundary verified: {args.wheel}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
