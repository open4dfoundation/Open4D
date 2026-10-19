#!/usr/bin/env python3
"""List release blockers and prove which ledger components are absent from built artifacts.

Without artifacts, print every ``BLOCK`` row of ``THIRD_PARTY.md`` with the
ledger's required action and its recorded remedy. With ``--artifacts`` (and
optionally ``--site``), inspect every built core, adapter and streamer archive
and documentation site without extracting them, and report for each ``BLOCK``
and ``EXCLUDED`` component whether any of its files appears by path or by
SHA-256 content. The JSON evidence (``--json``) is what a maintainer reviews
before recording an ``EXCLUDED`` decision; this tool never changes the ledger.
"""

from __future__ import annotations

import argparse
import datetime as dt
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile
from typing import Callable, Iterator
import zipfile

try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib  # type: ignore[no-redef]

from check_provenance import parse_component_rows
from check_streamer_package import DRACO_DIGESTS


ROOT = Path(__file__).resolve().parents[1]
# Smaller files (empty modules, one-line markers) match by coincidence.
MIN_CONTENT_BYTES = 32
SKIPPED_DIRECTORIES = {".git", "__pycache__", "node_modules", ".pytest_cache"}


def ALL(relative: str) -> bool:
    return True


def TOP_LEVEL(relative: str) -> bool:
    return "/" not in relative


# Where an EXCLUDED component may legitimately appear, as recorded in its row:
# adapters ship plugins/<name>; the core ships only rgbd's top-level Python API;
# the separate streamer archives ship open4d/streamer (its study has its own row).
EXCLUDED_SCOPES: dict[str, dict[str, Callable[[str], bool]]] = {
    "plugins": {"adapter": ALL},
    "open4d/reconstruction/rgbd": {"core": TOP_LEVEL},
    "open4d/streamer": {"streamer": ALL},
}
# Files whose upstream origin is pinned by SHA-256 elsewhere in the checks.
PINNED_UPSTREAM_DIGESTS = frozenset(DRACO_DIGESTS.values())
EXCLUSION_REMEDY = re.compile(
    r"keep (?:it )?outside every|keep outside every|exclude|excluded from|"
    r"moved outside the checkout|link externally", re.IGNORECASE)


@dataclass
class Member:
    """One file inside an artifact; ``repo_path`` is its checkout origin if known."""
    name: str
    repo_path: str | None
    data: bytes | None


@dataclass
class Artifact:
    path: Path
    kind: str  # core, adapter, streamer or docs-site
    sha256: str | None
    members: list[Member] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _normalized(name: str) -> str:
    return re.sub(r"[-_.]+", "_", name).lower()


def adapter_directories(root: Path = ROOT) -> dict[str, str]:
    """Map normalized adapter project names to their ``plugins/<dir>`` path."""
    result = {}
    for pyproject in sorted((root / "plugins").glob("*/pyproject.toml")):
        with pyproject.open("rb") as stream:
            name = tomllib.load(stream)["project"]["name"]
        result[_normalized(name)] = pyproject.parent.relative_to(root).as_posix()
    return result


def _zip_members(path: Path) -> Iterator[tuple[str, bytes | None]]:
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            if not info.is_dir():
                yield info.filename, archive.read(info)


def _tar_members(path: Path) -> Iterator[tuple[str, bytes | None]]:
    with tarfile.open(path, "r:*") as archive:
        for member in archive.getmembers():
            if member.isfile():
                stream = archive.extractfile(member)
                yield member.name, stream.read() if stream else b""
            elif not member.isdir():
                yield member.name, None  # links/special files: path evidence only


def read_artifact(path: Path, root: Path = ROOT) -> Artifact:
    """Classify a distribution by filename and map its members to checkout paths."""
    filename = path.name
    project = filename.split("-", 1)[0].lower()
    adapters = adapter_directories(root)
    wheel = filename.endswith(".whl")
    if project == "open4d":
        kind, prefix = "core", ""
    elif project == "open4d_streamer":
        kind, prefix = "streamer", "open4d/streamer/"
    elif project in adapters:
        kind, prefix = "adapter", adapters[project] + "/"
    else:
        artifact = Artifact(path, "unknown", None)
        artifact.errors.append(f"not a known Open4D distribution: {filename}")
        return artifact
    artifact = Artifact(path, kind, _sha256(path.read_bytes()))
    try:
        records = list(_zip_members(path) if wheel else _tar_members(path))
    except (OSError, tarfile.TarError, zipfile.BadZipFile) as error:
        artifact.errors.append(f"cannot read {filename}: {error}")
        return artifact
    for name, data in records:
        parts = PurePosixPath(name).parts
        if PurePosixPath(name).is_absolute() or ".." in parts:
            artifact.errors.append(f"unsafe member path in {filename}: {name}")
            continue
        if wheel:
            top = parts[0] if parts else ""
            if top.endswith(".dist-info"):
                licensed = parts[2:] if parts[1:2] == ("licenses",) else None
                repo = prefix + "/".join(licensed) if licensed else None
            elif top.endswith(".data"):
                repo = None
            else:
                # Adapter wheels hold metadata only; other files are compared by content.
                repo = None if kind == "adapter" else prefix + name
        else:
            stripped = "/".join(parts[1:])
            repo = prefix + stripped if stripped else None
        artifact.members.append(Member(name, repo, data))
    return artifact


def read_site(site: Path) -> Artifact:
    """Treat a built documentation site as an artifact: files plus rendered sources."""
    artifact = Artifact(site, "docs-site", None)
    if not site.is_dir():
        artifact.errors.append(f"documentation site does not exist: {site}")
        return artifact
    for path in sorted(site.rglob("*")):
        relative = path.relative_to(site).as_posix()
        if path.is_symlink():
            artifact.errors.append(f"documentation site contains a link: {relative}")
        elif path.is_file():
            artifact.members.append(Member(relative, None, path.read_bytes()))
    catalog = site / "documentation.json"
    try:
        documents = json.loads(catalog.read_text(encoding="utf-8"))["documents"]
    except (OSError, ValueError, KeyError) as error:
        artifact.errors.append(f"documentation catalog is missing or invalid: {error}")
        documents = []
    for record in documents:
        if record.get("mode") == "page":
            artifact.members.append(Member(f"page:{record.get('route')}", record.get("source"), None))
    return artifact


class Ledger:
    """Ledger rows indexed by path, with longest-prefix ownership of checkout files."""

    def __init__(self, text: str):
        self.rows = parse_component_rows(text)
        self.by_path = {path: row for row in self.rows for path in row["paths"] if row["decision"]}

    def owner(self, repo_path: str | None) -> tuple[str, dict] | None:
        if not repo_path:
            return None
        matches = [path for path in self.by_path
                   if repo_path == path or repo_path.startswith(path + "/")]
        if not matches:
            return None
        path = max(matches, key=len)
        return path, self.by_path[path]


def allowed_by_scope(row: dict, path: str, repo_path: str, kind: str) -> bool:
    """Whether an EXCLUDED component file may appear in an artifact of ``kind``."""
    if row["decision"] == "APPROVED":
        return True
    if row["decision"] != "EXCLUDED":
        return False
    predicate = EXCLUDED_SCOPES.get(path, {}).get(kind)
    relative = repo_path[len(path):].lstrip("/")
    return bool(predicate and predicate(relative))


def index_component_files(ledger: Ledger, sizes: set[int], root: Path = ROOT):
    """Hash checkout files of unapproved components whose size matches a member."""
    digests: dict[str, list[str]] = {}
    counts: dict[str, int] = {}
    for path, row in ledger.by_path.items():
        if row["decision"] == "APPROVED":
            continue
        counts.setdefault(path, 0)
        base = root / path
        if not base.exists():
            continue
        for directory, subdirectories, files in os.walk(base, followlinks=False):
            subdirectories[:] = [name for name in subdirectories if name not in SKIPPED_DIRECTORIES]
            for name in files:
                file_path = Path(directory) / name
                relative = file_path.relative_to(root).as_posix()
                owner = ledger.owner(relative)
                if owner is None or owner[0] != path or file_path.is_symlink():
                    continue
                counts[path] += 1
                size = file_path.stat().st_size
                if size >= MIN_CONTENT_BYTES and size in sizes:
                    digests.setdefault(_sha256(file_path.read_bytes()), []).append(relative)
    return digests, counts


def _legitimate_source(member: Member, kind: str, ledger: Ledger, root: Path) -> bool:
    """Whether a member is byte-identical to its own permitted checkout file."""
    if not member.repo_path:
        return False
    source = root / member.repo_path
    if not source.is_file() or source.is_symlink() or source.read_bytes() != member.data:
        return False
    owned = ledger.owner(member.repo_path)
    return owned is None or allowed_by_scope(owned[1], owned[0], member.repo_path, kind)


def ledger_remedy(action: str) -> str:
    """Summarize whether the ledger itself prescribes exclusion or resolution."""
    return "exclude" if EXCLUSION_REMEDY.search(action) else "resolve"


def collect_evidence(artifacts: list[Artifact], ledger_text: str, root: Path = ROOT) -> dict:
    """Return per-component presence evidence across every inspected artifact."""
    ledger = Ledger(ledger_text)
    sizes = {len(member.data) for artifact in artifacts for member in artifact.members
             if member.data is not None}
    digests, counts = index_component_files(ledger, sizes, root)
    findings: dict[str, dict[str, dict]] = {path: {} for path in ledger.by_path}
    notes: list[str] = []
    for artifact in artifacts:
        name = artifact.path.name
        for member in artifact.members:
            owned = ledger.owner(member.repo_path)
            if owned:
                path, row = owned
                record = findings[path].setdefault(name, {"paths": [], "content": [], "allowed": []})
                if allowed_by_scope(row, path, member.repo_path, artifact.kind):
                    record["allowed"].append(member.name)
                elif artifact.kind == "docs-site" and row["decision"] == "EXCLUDED":
                    record["allowed"].append(member.name)
                    notes.append(f"{name}: renders {member.repo_path} from EXCLUDED {path}")
                else:
                    record["paths"].append(member.name)
            if member.data is None or len(member.data) < MIN_CONTENT_BYTES:
                continue
            own_file = _legitimate_source(member, artifact.kind, ledger, root)
            digest = _sha256(member.data)
            for origin in digests.get(digest, []):
                path, row = ledger.owner(origin)
                if allowed_by_scope(row, path, origin, artifact.kind):
                    continue
                if own_file and origin != member.repo_path:
                    # A permitted file duplicated in another EXCLUDED component (such
                    # as the shared MIT license) does not place that component in the
                    # artifact. Copies of BLOCK content are only explained by an
                    # independently pinned upstream digest.
                    if row["decision"] != "BLOCK":
                        continue
                    if digest in PINNED_UPSTREAM_DIGESTS:
                        notes.append(f"{name}: {member.name} matches the pinned upstream digest of "
                                     f"{member.repo_path}; an identical copy exists in BLOCK "
                                     f"component {origin}")
                        continue
                record = findings[path].setdefault(name, {"paths": [], "content": [], "allowed": []})
                record["content"].append(f"{member.name} == {origin}")

    components = []
    for row in ledger.rows:
        if row["decision"] not in {"BLOCK", "EXCLUDED"}:
            continue
        per_artifact = {}
        present_in, site_present = [], []
        for artifact in artifacts:
            merged = {"paths": [], "content": [], "allowed": []}
            for path in row["paths"]:
                for key, values in findings.get(path, {}).get(artifact.path.name, {}).items():
                    merged[key] += values
            status = "present" if merged["paths"] or merged["content"] else (
                "allowed" if merged["allowed"] else "absent")
            if status == "present":
                (site_present if artifact.kind == "docs-site" else present_in).append(artifact.path.name)
            per_artifact[artifact.path.name] = {"status": status, **merged}
        compared = sum(counts.get(path, 0) for path in row["paths"])
        package_count = sum(artifact.kind != "docs-site" for artifact in artifacts)
        if row["decision"] == "EXCLUDED":
            needed = "verify-exclusion"
        elif present_in:
            needed = "resolve"
        elif package_count:
            needed = "exclude-eligible"
        else:
            needed = "unverified"
        components.append({
            "component": row["component"],
            "paths": row["paths"],
            "decision": row["decision"],
            "reviewer": row["reviewer"],
            "required_action": row["action"],
            "ledger_remedy": ledger_remedy(row["action"]),
            "checkout_files_compared": compared,
            "needed": needed,
            "present_in": present_in,
            "website_present_in": site_present,
            "artifacts": per_artifact,
        })
    return {"components": components, "notes": sorted(set(notes))}


def _git(root: Path, *command: str) -> str | None:
    result = subprocess.run(["git", *command], cwd=root, capture_output=True, text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else None


def expected_inventory_errors(artifacts: list[Artifact], root: Path = ROOT) -> list[str]:
    """Evidence must cover the core wheel and sdist and every adapter wheel."""
    names = [artifact.path.name for artifact in artifacts]
    errors = []
    if not any(name.startswith("open4d-") and name.endswith(".whl") for name in names):
        errors.append("evidence lacks the core wheel")
    if not any(name.startswith("open4d-") and name.endswith(".tar.gz") for name in names):
        errors.append("evidence lacks the core source distribution")
    for project in adapter_directories(root):
        if not any(name.startswith(project + "-") and name.endswith(".whl") for name in names):
            errors.append(f"evidence lacks the {project} adapter wheel")
    return errors


def build_report(artifact_dirs: list[Path], site: Path | None, root: Path = ROOT) -> dict:
    ledger_text = (root / "docs/THIRD_PARTY.md").read_text(encoding="utf-8")
    artifacts = []
    for directory in artifact_dirs:
        for path in sorted(directory.iterdir()) if directory.is_dir() else []:
            if path.name.endswith((".whl", ".tar.gz")) and path.is_file() and not path.is_symlink():
                artifacts.append(read_artifact(path, root))
    errors = [f"artifact directory does not exist: {directory}"
              for directory in artifact_dirs if not directory.is_dir()]
    if artifact_dirs:
        errors += expected_inventory_errors(artifacts, root)
    if site is not None:
        artifacts.append(read_site(site))
    errors += [error for artifact in artifacts for error in artifact.errors]
    evidence = collect_evidence(artifacts, ledger_text, root)
    for component in evidence["components"]:
        for name in component["present_in"] + component["website_present_in"]:
            detail = component["artifacts"][name]
            errors.append(f"{component['component']} ({component['decision']}) is present in {name}: "
                          + "; ".join(detail["paths"] + detail["content"]))
    return {
        "schema": 1,
        "generated": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "repository_commit": _git(root, "rev-parse", "HEAD"),
        "working_tree_clean": _git(root, "status", "--porcelain") == "",
        "ledger_sha256": _sha256(ledger_text.encode("utf-8")),
        "artifacts": [{"file": a.path.name, "kind": a.kind, "sha256": a.sha256,
                       "members": len(a.members)} for a in artifacts],
        "blockers": sum(c["decision"] == "BLOCK" for c in evidence["components"]),
        "errors": errors,
        **evidence,
    }


def render(report: dict) -> str:
    lines = []
    blocks = [c for c in report["components"] if c["decision"] == "BLOCK"]
    inspected = report["artifacts"]
    lines.append(f"{len(blocks)} BLOCK entries in THIRD_PARTY.md"
                 + (f"; {len(inspected)} artifacts inspected" if inspected else "; no artifacts inspected"))
    groups = (
        ("resolve", "Present in a package artifact: resolve the evidence or remove it from the artifact"),
        ("exclude-eligible", "Absent from every inspected package artifact: eligible for a maintainer-recorded EXCLUDED decision"),
        ("unverified", "Not yet checked against artifacts (run with --artifacts)"),
    )
    for key, title in groups:
        selected = [c for c in blocks if c["needed"] == key]
        if not selected:
            continue
        lines += ["", f"{title}:"]
        for component in selected:
            evidence = ""
            if inspected:
                evidence = (f" [compared {component['checkout_files_compared']} checkout files]"
                            if component["checkout_files_compared"] else " [no checkout files: path evidence only]")
            remedy = "ledger prescribes exclusion" if component["ledger_remedy"] == "exclude" \
                else "ledger requires resolution before the component itself is distributed"
            lines.append(f"  - {component['component']}: {remedy}{evidence}")
            if component["website_present_in"]:
                lines.append(f"      rendered on the documentation site: {component['website_present_in']}")
    excluded = [c for c in report["components"] if c["decision"] == "EXCLUDED"]
    if excluded and inspected:
        lines += ["", "Existing EXCLUDED decisions:"]
        for component in excluded:
            state = "present outside its recorded scope" if component["present_in"] else "within recorded scope"
            lines.append(f"  - {component['component']}: {state}")
    for note in report["notes"]:
        lines.append(f"NOTE: {note}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--artifacts", type=Path, action="append", default=[],
                        help="directory of built distributions; repeat for core/adapter/streamer outputs")
    parser.add_argument("--site", type=Path, help="built documentation site to include as an artifact")
    parser.add_argument("--json", type=Path, help="write the evidence record here (outside the repository)")
    args = parser.parse_args()
    if args.json:
        target = args.json.expanduser().resolve()
        if target == ROOT or ROOT in target.parents:
            parser.error("write evidence outside the repository")
    report = build_report([path.expanduser() for path in args.artifacts], args.site)
    print(render(report))
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"Evidence written to {args.json}")
    if report["errors"]:
        for error in report["errors"]:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
