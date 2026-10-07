#!/usr/bin/env python3
"""Block the supported release workflow while provenance entries are unresolved."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from check_provenance import release_decision_errors


ROOT = Path(__file__).resolve().parents[1]


def blockers(ledger: str | None = None) -> list[str]:
    if ledger is None:
        ledger = (ROOT / "THIRD_PARTY.md").read_text(encoding="utf-8")
    rows: list[str] = []
    for line in ledger.splitlines():
        if line.startswith("|") and re.search(r"\bBLOCK\b", line):
            cells = [cell.strip() for cell in line.strip("|").split("|")]
            rows.append(cells[0] if cells else line)
    if "## Release decision: blocked" in ledger and not rows:
        rows.append("release decision remains blocked")
    rows.extend(release_decision_errors(ledger))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--expect-blocked",
        action="store_true",
        help="succeed only when the repository is still explicitly blocked",
    )
    mode.add_argument(
        "--check-ledger", action="store_true",
        help="validate the recorded release state without authorizing publication",
    )
    args = parser.parse_args()
    if args.check_ledger:
        errors = release_decision_errors((ROOT / "THIRD_PARTY.md").read_text(encoding="utf-8"))
        if errors:
            for error in errors:
                print(f"ERROR: {error}", file=sys.stderr)
            return 1
        print("Release decision and approval record verified.")
        return 0
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
