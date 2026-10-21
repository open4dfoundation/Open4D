#!/usr/bin/env python3
"""Verify adapter archives contain only registration metadata and MIT license."""

from __future__ import annotations

import argparse
import configparser
from email.parser import Parser
from pathlib import Path, PurePosixPath
import tarfile
import zipfile

try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib

from check_wheel_contents import unsafe_zip_member_errors

# Every adapter ships the root MIT license.
LICENSE = Path(__file__).resolve().parents[1] / "LICENSE"


def check_archive(path: Path, project: Path) -> list[str]:
    with (project / "pyproject.toml").open("rb") as stream:
        config = tomllib.load(stream)["project"]
    expected_name = config["name"]
    expected_entries = config["entry-points"]
    errors = []
    if path.suffix == ".whl":
        with zipfile.ZipFile(path) as archive:
            unsafe = [error for info in archive.infolist() for error in unsafe_zip_member_errors(info)]
            if unsafe:
                return unsafe
            members = archive.namelist()
            roots = {name.split("/", 1)[0] for name in members}
            if len(roots) != 1 or not next(iter(roots)).endswith(".dist-info"):
                return ["adapter wheel contains implementation files"]
            root = next(iter(roots))
            allowed = {f"{root}/{name}" for name in (
                "METADATA", "WHEEL", "RECORD", "entry_points.txt", "top_level.txt", "licenses/LICENSE")}
            if len(members) != len(set(members)) or set(members) != allowed:
                errors.append("adapter wheel has unexpected or missing members")
            metadata = Parser().parsestr(archive.read(f"{root}/METADATA").decode())
            if metadata["Name"] != expected_name or metadata["License-Expression"] != "MIT":
                errors.append("adapter wheel name/license differs from its manifest")
            if metadata["Version"] != config["version"]:
                errors.append("adapter wheel version differs from its manifest")
            base_requirements = [value.replace(" ", "")
                                 for value in metadata.get_all("Requires-Dist", []) if "extra ==" not in value]
            if (len(base_requirements) != 1 or not base_requirements[0].startswith("open4d")
                    or set(base_requirements[0][len("open4d"):].split(",")) != {">=0.3.0", "<0.4"}):
                errors.append("adapter wheel must require the matching 0.3 core only")
            entries = configparser.ConfigParser()
            entries.read_string(archive.read(f"{root}/entry_points.txt").decode())
            if {section: dict(entries[section]) for section in entries.sections()} != expected_entries:
                errors.append("adapter entry points differ from its manifest")
            if archive.read(f"{root}/licenses/LICENSE") != LICENSE.read_bytes():
                errors.append("adapter license bytes differ")
    else:
        with tarfile.open(path, "r:gz") as archive:
            if any(not (member.isfile() or member.isdir()) for member in archive.getmembers()):
                errors.append("adapter source archive contains links or special files")
            members = [member.name for member in archive.getmembers() if member.isfile()]
            roots = {PurePosixPath(name).parts[0] for name in members}
            if len(roots) != 1:
                return ["adapter source archive has multiple roots"]
            stripped = [PurePosixPath(*PurePosixPath(name).parts[1:]).as_posix() for name in members]
            allowed = {"LICENSE", "pyproject.toml", "PKG-INFO", "setup.cfg"}
            egg_files = {"PKG-INFO", "SOURCES.txt", "dependency_links.txt", "entry_points.txt",
                         "requires.txt", "top_level.txt"}
            for name in stripped:
                parts = PurePosixPath(name).parts
                if name in allowed or (len(parts) == 2 and parts[0].endswith(".egg-info") and parts[1] in egg_files):
                    continue
                errors.append(f"adapter source archive contains unexpected member: {name}")
            if len(stripped) != len(set(stripped)):
                errors.append("adapter source archive has duplicate members")
            if not {"LICENSE", "pyproject.toml", "PKG-INFO"} <= set(stripped):
                errors.append("adapter source archive is missing required metadata")
            else:
                root = next(iter(roots))
                for filename, expected in (("LICENSE", LICENSE), ("pyproject.toml", project / "pyproject.toml")):
                    if archive.extractfile(f"{root}/{filename}").read() != expected.read_bytes():
                        errors.append(f"adapter source {filename} differs from its manifest")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--project", type=Path, required=True)
    args = parser.parse_args()
    errors = check_archive(args.archive, args.project)
    if errors:
        parser.exit(1, "\n".join(errors) + "\n")
    print(f"MIT adapter metadata boundary verified: {args.archive.name}")


if __name__ == "__main__":
    main()
