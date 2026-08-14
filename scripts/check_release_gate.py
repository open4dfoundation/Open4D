#!/usr/bin/env python3
"""Block the supported release workflow while provenance entries are unresolved."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def blockers() -> list[str]:
    """
    Collect unresolved release blockers from the third-party ledger.
    
    Returns:
    	list[str]: Blocker descriptions found in matching ledger rows, or a release-decision message when the ledger remains blocked without matching rows.
    """
    ledger = (ROOT / "THIRD_PARTY.md").read_text(encoding="utf-8")
    rows: list[str] = []
    for line in ledger.splitlines():
        if line.startswith("|") and re.search(r"\bBLOCK\b", line):
            cells = [cell.strip() for cell in line.strip("|").split("|")]
            rows.append(cells[0] if cells else line)
    if "## Release decision: blocked" in ledger and not rows:
        rows.append("release decision remains blocked")
    return rows


def main() -> int:
    """
    Run the release gate and report whether unresolved release blockers remain.
    
    With ``--expect-blocked``, succeeds only when at least one blocker exists.
    Otherwise, succeeds when the release ledger has no unresolved blockers.
    
    Returns:
        int: ``0`` when the release state matches the selected mode, otherwise ``1``.
    """
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--expect-blocked",
        action="store_true",
        help="succeed only when the repository is still explicitly blocked",
    )
    args = parser.parse_args()
    unresolved = blockers()
    if args.expect_blocked:
        if not unresolved:
            print("ERROR: release block disappeared without a cleared ledger", file=sys.stderr)
            return 1
        print(f"Release remains blocked by {len(unresolved)} ledger entries.")
        return 0
    if unresolved:
        print("ERROR: release is blocked by unresolved provenance entries:", file=sys.stderr)
        for path in unresolved:
            print(f"  - {path}", file=sys.stderr)
        return 1
    print("Release ledger has no unresolved BLOCK entries.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
