#!/usr/bin/env python3
"""Check local links in first-party Markdown without crawling the network."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"!?\[[^\]]*\]\((?P<target><[^>]+>|[^\s)]+)(?:\s+[^)]*)?\)")
REMOTE_SCHEMES = {"http", "https", "mailto", "data"}


def markdown_files() -> list[Path]:
    """
    Collect the existing first-party Markdown files to validate.
    
    Returns:
        list[Path]: Unique Markdown file paths in the designated repository locations,
        sorted lexicographically.
    """
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


def main() -> int:
    """
    Validate local links in selected Markdown files.
    
    Reports invalid links to standard error and returns a failure status when any
    absolute, out-of-repository, or missing local targets are found.
    
    Returns:
    	int: 1 if validation errors are found, otherwise 0.
    """
    errors: list[str] = []
    for document in markdown_files():
        text = document.read_text(encoding="utf-8")
        for match in LINK.finditer(text):
            raw = match.group("target").strip("<>")
            if raw.startswith("#"):
                continue
            parsed = urlsplit(raw)
            if parsed.scheme.lower() in REMOTE_SCHEMES or parsed.netloc:
                continue
            if not parsed.path:
                continue
            target = Path(unquote(parsed.path))
            line = text.count("\n", 0, match.start()) + 1
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
                errors.append(
                    f"{document.relative_to(ROOT)}:{line}: missing local target: {raw}"
                )

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"Local Markdown links verified in {len(markdown_files())} files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
