#!/usr/bin/env python3
"""Check local links in first-party Markdown without crawling the network."""

from __future__ import annotations

import configparser
import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"!?\[[^\]]*\]\((?P<target><[^>]+>|[^\s)]+)(?:\s+[^)]*)?\)")
REMOTE_SCHEMES = {"http", "https", "mailto", "data"}
HEADING = re.compile(r"^#{1,6}\s+(.+?)(?:\s*\{#([^}]+)\})?$", re.MULTILINE)


def markdown_files() -> list[Path]:
    roots = [
        ROOT / name
        for name in (
            "README.md",
            "CONTRIBUTING.md",
            "SECURITY.md",
            "THIRD_PARTY.md",
        )
    ]
    trees = [ROOT / name for name in ("docs", "apps", "examples", "scripts")]
    files = [path for path in roots if path.is_file()]
    for tree in trees:
        if tree.is_dir():
            files.extend(tree.rglob("*.md"))
    for path in (
        ROOT / "integrations/README.md",
        ROOT / "integrations/open3d/README.md",
    ):
        if path.is_file():
            files.append(path)
    return sorted(set(files))


def uninitialized_submodules(root: Path) -> list[Path]:
    """Registered submodule paths that this checkout has not populated.

    `.gitmodules` is parsed here rather than imported from
    `check_provenance.py`, which already does it: this checker has no
    dependency beyond the standard library, and that module reaches for
    `tomli` on interpreters without `tomllib`.

    A link into one of these is not a broken link. CI checks out without
    submodules, so the target is absent for a reason the document cannot
    know about, and asserting on it only says the workflow did not clone
    a tree the repository deliberately does not vendor.
    """
    gitmodules = root / ".gitmodules"
    if not gitmodules.is_file():
        return []
    parser = configparser.ConfigParser(interpolation=None)
    parser.read_string(gitmodules.read_text(encoding="utf-8"))
    absent = []
    for section in parser.sections():
        if not section.startswith("submodule ") or not parser.has_option(
            section, "path"
        ):
            continue
        path = root / parser.get(section, "path").strip()
        if not path.is_dir() or not any(path.iterdir()):
            absent.append(path.resolve())
    return absent


def extract_heading_anchors(text: str) -> set[str]:
    """Extract valid heading anchors from markdown text.

    GitHub-style anchors are lowercase, replace spaces with hyphens,
    and remove special characters except hyphens.
    """
    anchors = set()
    for match in HEADING.finditer(text):
        heading_text = match.group(1)
        explicit_id = match.group(2)
        if explicit_id:
            anchors.add(explicit_id)
        else:
            # GitHub-style anchor generation
            anchor = heading_text.lower()
            anchor = re.sub(r"[^\w\s-]", "", anchor)
            anchor = re.sub(r"[-\s]+", "-", anchor)
            anchor = anchor.strip("-")
            if anchor:
                anchors.add(anchor)
    return anchors


def main() -> int:
    errors: list[str] = []
    unpopulated = uninitialized_submodules(ROOT)
    skipped = 0
    for document in markdown_files():
        text = document.read_text(encoding="utf-8")
        for match in LINK.finditer(text):
            raw = match.group("target").strip("<>")
            parsed = urlsplit(raw)
            line = text.count("\n", 0, match.start()) + 1

            # Handle in-page anchor links
            if raw.startswith("#"):
                fragment = parsed.fragment or raw[1:]
                if fragment:
                    anchors = extract_heading_anchors(text)
                    if fragment not in anchors:
                        errors.append(
                            f"{document.relative_to(ROOT)}:{line}: "
                            f"fragment not found in document: {raw}"
                        )
                continue

            if parsed.scheme.lower() in REMOTE_SCHEMES or parsed.netloc:
                continue
            if not parsed.path:
                continue
            target = Path(unquote(parsed.path))
            if target.is_absolute():
                errors.append(
                    f"{document.relative_to(ROOT)}:{line}: absolute local link: {raw}"
                )
                continue
            resolved = (document.parent / target).resolve()
            try:
                resolved.relative_to(ROOT)
            except ValueError:
                errors.append(
                    f"{document.relative_to(ROOT)}:{line}: link escapes repository: {raw}"
                )
                continue
            if not resolved.exists():
                if any(
                    resolved == module or module in resolved.parents
                    for module in unpopulated
                ):
                    skipped += 1
                    continue
                errors.append(
                    f"{document.relative_to(ROOT)}:{line}: missing local target: {raw}"
                )
                continue

            # Validate fragment if present
            if parsed.fragment:
                if resolved.suffix == ".md":
                    target_text = resolved.read_text(encoding="utf-8")
                    target_anchors = extract_heading_anchors(target_text)
                    if parsed.fragment not in target_anchors:
                        errors.append(
                            f"{document.relative_to(ROOT)}:{line}: "
                            f"fragment not found in {resolved.relative_to(ROOT)}: {raw}"
                        )

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    note = (
        f" ({skipped} skipped in {len(unpopulated)} uninitialized submodules)"
        if skipped
        else ""
    )
    print(f"Local Markdown links verified in {len(markdown_files())} files{note}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
