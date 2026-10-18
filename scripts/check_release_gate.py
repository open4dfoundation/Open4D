#!/usr/bin/env python3
"""Block publication until the ledger is cleared and a structured approval matches.

The ledger (``THIRD_PARTY.md``) records whether a release is ``blocked`` or
``approved``. Publication additionally requires a maintainer-authored approval
file (default ``.github/release-approval.toml``) that names the approver's
GitHub login, role and date, the reviewed ledger revision, the approved
publication scope, the build run that produced the artifacts and the exact
SHA-256 digest of every core and adapter artifact. With ``--artifacts`` the gate
recomputes the digests of the files about to be published and refuses any
difference. ``--draft-approval`` prints a template with the measured digests;
its personal fields stay placeholders that the gate rejects until a maintainer
fills them in.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import re
import subprocess
import sys
from pathlib import Path
from typing import Callable

try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib  # type: ignore[no-redef]

from check_provenance import (
    approval_date_errors,
    github_login_errors,
    is_placeholder,
    ledger_approval,
    release_decision_errors,
)


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_APPROVAL = Path(".github/release-approval.toml")
APPROVAL_SCHEMA = 1
APPROVAL_ROLES = ("maintainer", "release-manager", "license-reviewer")
APPROVAL_SCOPES = ("testpypi", "pypi")
MAINTAINER_LISTS = (".github/CODEOWNERS", "CODEOWNERS", "docs/CODEOWNERS", "MAINTAINERS",
                    "MAINTAINERS.txt", ".github/MAINTAINERS")
HEX40 = re.compile(r"[0-9a-f]{40}")
HEX64 = re.compile(r"[0-9a-f]{64}")


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


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def distribution_digests(directory: Path) -> tuple[dict[str, str], list[str]]:
    """Digest every distribution in ``directory``; anything else is an error."""
    if not directory.is_dir():
        return {}, [f"artifact directory does not exist: {directory}"]
    digests: dict[str, str] = {}
    errors: list[str] = []
    for path in sorted(directory.iterdir()):
        if path.is_symlink() or not path.is_file():
            errors.append(f"artifact directory contains a link or non-file entry: {path.name}")
        elif path.name.endswith((".whl", ".tar.gz")):
            digests[path.name] = sha256_file(path)
        else:
            errors.append(f"artifact directory contains a non-distribution file: {path.name}")
    return digests, errors


def _normalized(name: str) -> str:
    return re.sub(r"[-_.]+", "_", name).lower()


def release_projects(root: Path = ROOT) -> dict[str, str]:
    """Map each published project (core and adapters) to its declared version."""
    projects = {}
    for pyproject in [root / "pyproject.toml", *sorted((root / "plugins").glob("*/pyproject.toml"))]:
        with pyproject.open("rb") as stream:
            project = tomllib.load(stream)["project"]
        projects[project["name"]] = project["version"]
    return projects


def expected_artifact_errors(names: set[str], root: Path = ROOT) -> list[str]:
    """Require core sdist and wheel plus one wheel per adapter, and nothing foreign."""
    errors = []
    accounted: set[str] = set()
    for project, version in release_projects(root).items():
        stem = f"{_normalized(project)}-{re.escape(version)}"
        wheels = {name for name in names if re.fullmatch(stem + r"-[^-]+-[^-]+-[^-]+\.whl", name)}
        sdists = {name for name in names if re.fullmatch(stem + r"\.tar\.gz", name)}
        if len(wheels) != 1:
            errors.append(f"expected exactly one {project} {version} wheel, found {sorted(wheels)}")
        if project == "open4d" and len(sdists) != 1:
            errors.append(f"expected the open4d {version} source distribution, found {sorted(sdists)}")
        accounted |= wheels | sdists
    for name in sorted(names - accounted):
        errors.append(f"artifact is not a core or adapter distribution of this revision: {name}")
    return errors


def maintainer_logins(root: Path = ROOT) -> set[str] | None:
    """Return logins listed in CODEOWNERS/MAINTAINERS, or None when no list exists."""
    logins: set[str] | None = None
    for relative in MAINTAINER_LISTS:
        path = root / relative
        if not path.is_file():
            continue
        logins = logins or set()
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0]
            # Individual @login entries only; @org/team handles are not people.
            logins.update(match.lower() for match in re.findall(r"(?<![\w/])@([A-Za-z0-9-]+)(?![\w/])", line))
    return logins


def git_ledger_digest(root: Path, commit: str) -> str | None:
    """SHA-256 of ``THIRD_PARTY.md`` at ``commit``; None when unavailable."""
    result = subprocess.run(["git", "show", f"{commit}:THIRD_PARTY.md"], cwd=root,
                            capture_output=True, check=False)
    return hashlib.sha256(result.stdout).hexdigest() if result.returncode == 0 else None


def load_approval(path: Path) -> tuple[dict, list[str]]:
    if not path.is_file():
        return {}, [f"approval file does not exist: {path}"]
    try:
        with path.open("rb") as stream:
            return tomllib.load(stream), []
    except tomllib.TOMLDecodeError as error:
        return {}, [f"approval file is not valid TOML: {error}"]


def approval_errors(
    approval: dict,
    *,
    root: Path = ROOT,
    ledger: str | None = None,
    digests: dict[str, str] | None = None,
    require_scope: str | None = None,
    build_commit: str | None = None,
    now: dt.datetime | None = None,
    ledger_at_commit: Callable[[Path, str], str | None] = git_ledger_digest,
    maintainers: set[str] | None | bool = True,
) -> list[str]:
    """Validate a structured approval record and, optionally, the artifacts it names.

    ``maintainers=True`` reads CODEOWNERS/MAINTAINERS from ``root``; pass a set
    to supply the list directly or None to skip the membership check.
    """
    if ledger is None:
        ledger = (root / "THIRD_PARTY.md").read_text(encoding="utf-8")
    errors: list[str] = []
    known = {"schema", "approver", "role", "date", "scope", "version", "ledger", "build", "artifacts"}
    if unknown := sorted(set(approval) - known):
        errors.append(f"approval has unknown fields: {unknown}")
    if approval.get("schema") != APPROVAL_SCHEMA:
        errors.append(f"approval schema must be {APPROVAL_SCHEMA}")

    approver = approval.get("approver")
    errors.extend(github_login_errors(approver))
    login = str(approver).strip().lstrip("@").lower() if isinstance(approver, str) else ""
    if maintainers is True:
        maintainers = maintainer_logins(root)
    if maintainers is not None and login and not github_login_errors(approver) \
            and login not in {name.lower() for name in maintainers}:
        errors.append(f"approver @{login} is not listed in the repository's CODEOWNERS/MAINTAINERS")

    if approval.get("role") not in APPROVAL_ROLES:
        errors.append(f"role must be one of {list(APPROVAL_ROLES)}")
    errors.extend(approval_date_errors(approval.get("date"), now=now))

    scope = approval.get("scope")
    if (not isinstance(scope, list) or not scope or len(set(scope)) != len(scope)
            or not set(scope) <= set(APPROVAL_SCOPES)):
        errors.append(f"scope must be a non-empty list drawn from {list(APPROVAL_SCOPES)}")
    elif require_scope and require_scope not in scope:
        errors.append(f"approval scope {scope} does not authorize {require_scope}")

    versions = set(release_projects(root).values())
    if len(versions) != 1:
        errors.append(f"core and adapter versions differ: {sorted(versions)}")
    if approval.get("version") not in versions or len(versions) != 1:
        errors.append(f"approval version {approval.get('version')!r} does not match the packages {sorted(versions)}")

    record = approval.get("ledger")
    current = hashlib.sha256(ledger.encode("utf-8")).hexdigest()
    if not isinstance(record, dict) or not HEX40.fullmatch(str(record.get("commit", ""))) \
            or not HEX64.fullmatch(str(record.get("sha256", ""))):
        errors.append("[ledger] requires the reviewed 40-hex commit and the 64-hex sha256 of THIRD_PARTY.md")
    else:
        if record["sha256"] != current:
            errors.append("THIRD_PARTY.md changed after approval: ledger sha256 differs")
        at_commit = ledger_at_commit(root, record["commit"])
        if at_commit is None:
            errors.append(f"reviewed ledger commit {record['commit']} is not available to verify")
        elif at_commit != record["sha256"]:
            errors.append(f"THIRD_PARTY.md at {record['commit']} does not match the approved ledger sha256")

    recorded = ledger_approval(ledger)
    if recorded and isinstance(approver, str):
        if recorded[0].lower() != login or recorded[1] != str(approval.get("date")):
            errors.append("approver and date differ from the ledger's Maintainer approval line")
    if release_decision_errors(ledger, now=now) or "## Release decision: approved" not in ledger:
        errors.append("the ledger must record an approved release without BLOCK entries")

    build = approval.get("build")
    if not isinstance(build, dict) or not isinstance(build.get("run_id"), int) \
            or isinstance(build.get("run_id"), bool) or build["run_id"] <= 0 \
            or not HEX40.fullmatch(str(build.get("commit", ""))):
        errors.append("[build] requires the positive integer run_id and 40-hex commit "
                      "of the TestPyPI-verified publish run")
    elif build_commit is not None and build["commit"] != build_commit:
        errors.append(f"artifacts were built from {build_commit}, not the approved {build['commit']}")

    approved: dict[str, str] = {}
    artifacts = approval.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        errors.append("[[artifacts]] must list every approved distribution file")
        artifacts = []
    for entry in artifacts:
        name = entry.get("filename") if isinstance(entry, dict) else None
        digest = entry.get("sha256") if isinstance(entry, dict) else None
        if not isinstance(name, str) or "/" in name or "\\" in name or is_placeholder(name):
            errors.append(f"artifact entry has an invalid filename: {entry!r}")
            continue
        if not isinstance(digest, str) or not HEX64.fullmatch(digest):
            errors.append(f"artifact {name} needs a lowercase 64-hex sha256 digest")
            continue
        if name in approved:
            errors.append(f"artifact listed twice: {name}")
        approved[name] = digest
    errors.extend(expected_artifact_errors(set(approved), root))

    if digests is not None:
        for name in sorted(approved.keys() - digests.keys()):
            errors.append(f"approved artifact is missing: {name}")
        for name in sorted(digests.keys() - approved.keys()):
            errors.append(f"artifact was not approved: {name}")
        for name in sorted(approved.keys() & digests.keys()):
            if approved[name] != digests[name]:
                errors.append(f"artifact digest differs from the approval: {name}")
    return errors


def draft_approval(digests: dict[str, str], *, root: Path = ROOT,
                   run_id: int | None = None, build_commit: str | None = None) -> str:
    """Render an approval template; personal fields remain rejected placeholders."""
    ledger = (root / "THIRD_PARTY.md").read_bytes()
    commit = subprocess.run(["git", "log", "-1", "--format=%H", "--", "THIRD_PARTY.md"], cwd=root,
                            capture_output=True, text=True, check=False).stdout.strip() or "TBD"
    version = sorted(set(release_projects(root).values()))[0]
    lines = [
        "# Open4D publication approval. A maintainer must replace every TBD value;",
        "# the release gate rejects placeholders, future dates and digest changes.",
        f"schema = {APPROVAL_SCHEMA}",
        'approver = "TBD"  # GitHub login of the approving maintainer',
        f'role = "TBD"  # one of {", ".join(APPROVAL_ROLES)}',
        'date = "TBD"  # YYYY-MM-DD of the approval',
        'scope = ["pypi"]',
        f'version = "{version}"',
        "",
        "[ledger]",
        f'commit = "{commit}"',
        f'sha256 = "{hashlib.sha256(ledger).hexdigest()}"',
        "",
        "[build]",
        f"run_id = {run_id if run_id is not None else 0}  # publish run that passed the TestPyPI check",
        f'commit = "{build_commit or "TBD"}"',
    ]
    for name, digest in sorted(digests.items()):
        lines += ["", "[[artifacts]]", f'filename = "{name}"', f'sha256 = "{digest}"']
    return "\n".join(lines) + "\n"


def _report(errors: list[str], success: str) -> int:
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(success)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
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
    mode.add_argument(
        "--draft-approval", action="store_true",
        help="print an approval template for --artifacts; it does not approve anything",
    )
    parser.add_argument("--approval", type=Path, default=None,
                        help=f"structured approval file (default {DEFAULT_APPROVAL})")
    parser.add_argument("--artifacts", type=Path,
                        help="directory holding exactly the distributions to publish")
    parser.add_argument("--require-scope", choices=APPROVAL_SCOPES,
                        help="publication target the approval must authorize")
    parser.add_argument("--build-run-id", type=int, help="run that built --artifacts")
    parser.add_argument("--build-commit", help="commit that built --artifacts")
    args = parser.parse_args()
    ledger = (ROOT / "THIRD_PARTY.md").read_text(encoding="utf-8")
    approval_path = args.approval or ROOT / DEFAULT_APPROVAL
    approved_state = "## Release decision: approved" in ledger

    if args.draft_approval:
        if not args.artifacts:
            parser.error("--draft-approval requires --artifacts")
        digests, errors = distribution_digests(args.artifacts)
        errors += expected_artifact_errors(set(digests))
        if errors:
            return _report(errors, "")
        print(draft_approval(digests, run_id=args.build_run_id, build_commit=args.build_commit), end="")
        return 0

    if args.check_ledger:
        errors = release_decision_errors(ledger)
        if approved_state or approval_path.is_file():
            approval, load_errors = load_approval(approval_path)
            errors += load_errors or approval_errors(approval, ledger=ledger)
        return _report(errors, "Release decision and approval record verified.")

    if args.expect_blocked:
        unresolved = blockers(ledger)
        if not unresolved:
            print("ERROR: release block disappeared without a cleared ledger", file=sys.stderr)
            return 1
        print(f"Release remains blocked by {len(unresolved)} ledger entries.")
        return 0

    unresolved = blockers(ledger)
    if unresolved:
        print("ERROR: release is blocked by unresolved provenance entries:", file=sys.stderr)
        for path in unresolved:
            print(f"  - {path}", file=sys.stderr)
        return 1
    if args.artifacts or args.require_scope or args.approval or approved_state:
        approval, errors = load_approval(approval_path)
        digests = None
        if args.artifacts:
            digests, directory_errors = distribution_digests(args.artifacts)
            errors += directory_errors
        if approval:
            errors += approval_errors(approval, ledger=ledger, digests=digests,
                                      require_scope=args.require_scope,
                                      build_commit=args.build_commit)
            build = approval.get("build") if isinstance(approval.get("build"), dict) else {}
            if args.build_run_id is not None and build.get("run_id") != args.build_run_id:
                errors.append(f"artifacts came from run {args.build_run_id}, not the approved "
                              f"run {build.get('run_id')}")
        if args.require_scope and not args.artifacts:
            errors.append("--require-scope needs --artifacts so digests can be verified")
        return _report(errors, "Release ledger cleared and approval matches "
                       + ("the artifacts." if args.artifacts else "its record."))
    print("Release ledger has no unresolved BLOCK entries.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
