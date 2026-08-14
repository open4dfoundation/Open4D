#!/usr/bin/env python3
"""Fail closed when audited areas escape the release ledger or package boundary."""

from __future__ import annotations

import sys
from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib  # type: ignore[no-redef]


ROOT = Path(__file__).resolve().parents[1]
REQUIRED_LEDGER_PATHS = (
    "open4d/codecs/draco",
    "open4d/codecs/klt",
    "open4d/codecs/n4mc",
    "open4d/codecs/qndf",
    "open4d/codecs/qndf_int8",
    "open4d/codecs/tsmc",
    "open4d/codecs/tvmc",
    "open4d/codecs/vdmc",
    "open4d/reconstruction/rgbd",
    "open4d/reconstruction/3dgstream",
    "open4d/reconstruction/queen",
    "open4d/reconstruction/gs_tools",
    "integrations/unity",
)
ALLOWED_PACKAGES = {
    "open4d",
    "open4d.core",
    "open4d.torch_ops",
    "integrations",
    "integrations.open3d",
}


def main() -> int:
    errors: list[str] = []
    ledger = (ROOT / "THIRD_PARTY.md").read_text(encoding="utf-8")
    if "## Release decision: blocked" not in ledger:
        errors.append("THIRD_PARTY.md must retain the explicit release block")

    for path in REQUIRED_LEDGER_PATHS:
        if not (ROOT / path).is_dir():
            errors.append(f"expected audited area is missing: {path}")
        if f"`{path}`" not in ledger:
            errors.append(f"THIRD_PARTY.md has no entry for {path}")

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
