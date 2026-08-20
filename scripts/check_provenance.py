#!/usr/bin/env python3
"""Fail closed when audited areas escape the release ledger or package boundary."""

from __future__ import annotations

import configparser
import sys
from pathlib import Path, PurePosixPath

try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib  # type: ignore[no-redef]


ROOT = Path(__file__).resolve().parents[1]
AUDITED_DIRECTORY_ROOTS = (
    "open4d/codecs",
    "open4d/reconstruction",
)
EXPLICIT_REQUIRED_LEDGER_PATHS = (
    "integrations/unity",
)
ALLOWED_PACKAGES = {
    "open4d",
    "open4d.codec",
    "open4d.core",
    "open4d.io",
    "open4d.torch_ops",
    "open4d.visualization",
    "integrations",
    "integrations.open3d",
}


def parse_gitmodule_paths(gitmodules: str) -> set[str]:
    """Return normalized submodule paths from ``.gitmodules`` content."""
    parser = configparser.ConfigParser(interpolation=None)
    parser.read_string(gitmodules)
    paths: set[str] = set()
    for section in parser.sections():
        if not section.startswith("submodule ") or not parser.has_option(
            section, "path"
        ):
            continue
        raw_path = parser.get(section, "path").strip()
        path = PurePosixPath(raw_path)
        if not raw_path or path.is_absolute() or ".." in path.parts:
            raise ValueError(f"unsafe submodule path in .gitmodules: {raw_path!r}")
        paths.add(path.as_posix())
    return paths


def _top_level_component(path: str) -> str:
    """Map a nested audited path to the component row that owns it."""
    for root in AUDITED_DIRECTORY_ROOTS:
        prefix = f"{root}/"
        if path.startswith(prefix):
            child = path[len(prefix):].split("/", 1)[0]
            return f"{root}/{child}"
    return path


def discover_required_ledger_paths(root: Path) -> set[str]:
    """Discover auditable components from the tree and registered submodules."""
    required = set(EXPLICIT_REQUIRED_LEDGER_PATHS)
    for directory_root in AUDITED_DIRECTORY_ROOTS:
        directory = root / directory_root
        if directory.is_dir():
            required.update(
                f"{directory_root}/{child.name}"
                for child in directory.iterdir()
                if child.is_dir()
            )

    gitmodules_path = root / ".gitmodules"
    if gitmodules_path.is_file():
        submodule_paths = parse_gitmodule_paths(
            gitmodules_path.read_text(encoding="utf-8")
        )
        required.update(_top_level_component(path) for path in submodule_paths)
    return required


def uncovered_component_paths(
    required_paths: set[str], component_ledger: dict[str, str]
) -> set[str]:
    """Return discovered components without a BLOCK or EXCLUDED ledger row."""
    return required_paths - component_ledger.keys()


def parse_component_ledger(ledger: str) -> dict[str, str]:
    """Parse the component ledger table and extract path -> decision mappings."""
    component_map = {}
    in_ledger = False
    for line in ledger.split("\n"):
        if "## Component ledger" in line:
            in_ledger = True
            continue
        if in_ledger and line.startswith("##"):
            break
        if in_ledger and line.startswith("|") and not line.startswith("| Path"):
            parts = [p.strip() for p in line.split("|")]
            if len(parts) >= 5:
                path_component = parts[1]
                decision = parts[4]
                # Extract path from backticks if present
                if "`" in path_component:
                    import re
                    match = re.search(r"`([^`]+)`", path_component)
                    if match:
                        path = match.group(1)
                        # Extract decision keyword (BLOCK or EXCLUDED)
                        if "BLOCK" in decision:
                            component_map[path] = "BLOCK"
                        elif "EXCLUDED" in decision:
                            component_map[path] = "EXCLUDED"
    return component_map


def main() -> int:
    """
    Validate release provenance coverage and package distribution boundaries.
    
    Returns:
    	int: `1` if any required validation fails, otherwise `0`.
    """
    errors: list[str] = []
    ledger = (ROOT / "THIRD_PARTY.md").read_text(encoding="utf-8")
    if "## Release decision: blocked" not in ledger:
        errors.append("THIRD_PARTY.md must retain the explicit release block")

    component_ledger = parse_component_ledger(ledger)

    required_ledger_paths = discover_required_ledger_paths(ROOT)
    for path in sorted(required_ledger_paths):
        if not (ROOT / path).is_dir():
            errors.append(f"expected audited area is missing: {path}")
    uncovered = uncovered_component_paths(required_ledger_paths, component_ledger)
    for path in sorted(uncovered):
        errors.append(
            f"THIRD_PARTY.md component ledger has no row with valid "
            f"BLOCK or EXCLUDED decision for {path}"
        )

    with (ROOT / "pyproject.toml").open("rb") as stream:
        project = tomllib.load(stream)
    setuptools = project["tool"]["setuptools"]
    packages = set(setuptools.get("packages", []))
    if packages != ALLOWED_PACKAGES:
        errors.append(
            "package boundary changed: "
            f"expected {sorted(ALLOWED_PACKAGES)}, got {sorted(packages)}"
        )
    if "find" in setuptools:
        errors.append("automatic setuptools package discovery must remain disabled")

    manifest = (ROOT / "MANIFEST.in").read_text(encoding="utf-8")
    for path in ("open4d/codecs", "open4d/reconstruction", "integrations/unity"):
        if f"prune {path}" not in manifest:
            errors.append(f"MANIFEST.in must prune {path}")

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("Provenance coverage and distribution containment verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
