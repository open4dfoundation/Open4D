#!/usr/bin/env python3
"""Fail closed when audited areas escape the release ledger or package boundary."""

from __future__ import annotations

import configparser
import datetime as dt
import re
import sys
from pathlib import Path, PurePosixPath

try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib  # type: ignore[no-redef]


ROOT = Path(__file__).resolve().parents[1]
AUDITED_DIRECTORY_ROOTS = (
    "open4d/reconstruction",
)
# Each codec folder pairs a packaged adapter with a research tree in source/;
# the research tree is the auditable component.
CODEC_ROOT = "open4d/codecs"
EXPLICIT_REQUIRED_LEDGER_PATHS = (
    "docs/assets",
    "integrations/unity",
    "open4d/streamer",
    "open4d/streamer/study",
    "plugins",
)
ALLOWED_PACKAGES = {
    "open4d",
    "open4d.codecs",
    "open4d.codecs._backends",
    "open4d.codecs.draco",
    "open4d.codecs.klt",
    "open4d.codecs.n4mc",
    "open4d.codecs.qndf",
    "open4d.codecs.qndf_int8",
    "open4d.codecs.vdmc",
    "open4d.codecs.faster_vdmc",
    "open4d.codecs.tvmc",
    "open4d.codecs.tsmc",
    "open4d.codecs.queen",
    "open4d.codecs.rerf",
    "open4d.codecs.vega",
    "open4d.core",
    "open4d.data",
    "open4d.ml",
    "open4d.io",
    "open4d.reconstruction",
    "open4d.reconstruction.rgbd",
    "open4d.transport",
    "open4d.torch_ops",
    "open4d.visualization",
    "open4d.integrations",
    "open4d.integrations.open3d",
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
    parts = path.split("/")
    if path.startswith(f"{CODEC_ROOT}/") and len(parts) > 3 and parts[3] == "source":
        return "/".join(parts[:4])
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
                and child.name != "__pycache__"
                and not child.name.startswith(".")
            )
    codecs = root / CODEC_ROOT
    if codecs.is_dir():
        required.update(
            f"{CODEC_ROOT}/{child.name}/source"
            for child in codecs.iterdir()
            if (child / "source").is_dir()
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
    """Return discovered components without a valid ledger decision."""
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
                    match = re.search(r"`([^`]+)`", path_component)
                    if match:
                        path = match.group(1)
                        match = re.match(r"`?(BLOCK|EXCLUDED|APPROVED)`?(?:;|\s|$)", decision)
                        if match:
                            component_map[path] = match[1]
    return component_map


# Values that name a role or a template field instead of an accountable person.
PLACEHOLDER_VALUES = {
    "", "approver", "changeme", "example", "github-login", "login", "maintainer",
    "n/a", "na", "name", "none", "pending", "placeholder", "tbc", "tbd", "todo",
    "unassigned", "unknown", "user", "username", "xxx", "your-github-login",
}
GITHUB_LOGIN = re.compile(r"[A-Za-z0-9](?:[A-Za-z0-9]|-(?=[A-Za-z0-9])){0,38}")
# GitHub was founded in 2008; earlier dates are typing errors, not approvals.
EARLIEST_APPROVAL_DATE = dt.date(2008, 1, 1)


def is_placeholder(value: object) -> bool:
    """Return whether a recorded value is empty, a template or a generic role."""
    text = str(value).strip().lower().lstrip("@")
    return (
        text in PLACEHOLDER_VALUES
        or any(marker in text for marker in ("example", "tbd", "todo", "placeholder", "<", ">"))
    )


def github_login_errors(value: object, field: str = "approver") -> list[str]:
    """Validate a GitHub account name rather than a free-form display name."""
    if not isinstance(value, str) or is_placeholder(value):
        return [f"{field} must name the approving maintainer's GitHub login, not a placeholder"]
    if not GITHUB_LOGIN.fullmatch(value.strip().lstrip("@")):
        return [f"{field} is not a valid GitHub login: {value!r}"]
    return []


def latest_valid_date(now: dt.datetime | None = None) -> dt.date:
    """Latest calendar date anywhere on Earth (UTC+14) at this instant."""
    now = now or dt.datetime.now(dt.timezone.utc)
    return (now.astimezone(dt.timezone.utc) + dt.timedelta(hours=14)).date()


def approval_date_errors(
    value: object, field: str = "date", *, now: dt.datetime | None = None
) -> list[str]:
    """Require a real calendar date that is neither implausibly old nor future."""
    if isinstance(value, dt.datetime):
        return [f"{field} must be a calendar date (YYYY-MM-DD), not a timestamp"]
    if isinstance(value, str):
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", value.strip()):
            return [f"{field} must use YYYY-MM-DD: {value!r}"]
        try:
            value = dt.date.fromisoformat(value.strip())
        except ValueError:
            return [f"{field} is not a real calendar date: {value!r}"]
    if not isinstance(value, dt.date):
        return [f"{field} must be a calendar date (YYYY-MM-DD)"]
    if value > latest_valid_date(now):
        return [f"{field} is in the future: {value.isoformat()}"]
    if value < EARLIEST_APPROVAL_DATE:
        return [f"{field} predates any possible approval: {value.isoformat()}"]
    return []


def ledger_approval(ledger: str) -> tuple[str, str] | None:
    """Return the ledger's ``(login, date)`` approval line, if one is recorded."""
    approval = re.search(
        r"^Maintainer approval: (.+) \(([^)]*)\)\s*$", ledger, re.MULTILINE
    )
    return (approval[1].strip().lstrip("@"), approval[2].strip()) if approval else None


def release_decision_errors(ledger: str, *, now: dt.datetime | None = None) -> list[str]:
    """Require one explicit release state and recorded approval before clearance."""
    decisions = re.findall(r"^## Release decision: (\w+)\s*$", ledger, re.MULTILINE)
    if len(decisions) != 1 or decisions[0] not in {"blocked", "approved"}:
        return ["THIRD_PARTY.md must contain one release decision: blocked or approved"]
    if decisions[0] == "blocked":
        return []
    errors = []
    if any(line.startswith("|") and re.search(r"\bBLOCK\b", line)
           for line in ledger.splitlines()):
        errors.append("an approved release cannot retain BLOCK entries")
    approval = ledger_approval(ledger)
    if not approval:
        errors.append(
            "an approved release requires Maintainer approval: @github-login (YYYY-MM-DD)"
        )
    else:
        login, date = approval
        errors.extend(f"Maintainer approval {error}" for error in
                      github_login_errors(login, "login") + approval_date_errors(date, now=now))
    return errors


def parse_component_rows(ledger: str) -> list[dict[str, object]]:
    """Return every component-ledger row, including rows without a path cell.

    ``paths`` holds the repository paths a row governs: the backticked path in
    the first cell, or for descriptive rows such as ``Tracked media`` the
    backticked paths in the evidence cell (brace lists are expanded).
    """
    rows = []
    in_ledger = False
    for line in ledger.splitlines():
        if line.startswith("## Component ledger"):
            in_ledger = True
            continue
        if in_ledger and line.startswith("##"):
            break
        if not in_ledger or not line.startswith("|") or line.startswith("| Path"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) < 4 or set(cells[0]) <= {"-", " "}:
            continue
        decision = re.match(r"`?(BLOCK|EXCLUDED|APPROVED)`?(?:;|\s|$)", cells[3])
        label_path = re.fullmatch(r"`([^`]+)`", cells[0])
        if label_path:
            paths = [label_path[1]]
        else:
            paths = [expanded for raw in re.findall(r"`([^`]+)`", cells[1])
                     for expanded in _expand_braces(raw) if "/" in expanded]
        rows.append({
            "component": label_path[1] if label_path else cells[0],
            "paths": paths,
            "decision": decision[1] if decision else None,
            "action": cells[3],
            "reviewer": cells[4] if len(cells) > 4 else "",
        })
    return rows


def _expand_braces(path: str) -> list[str]:
    match = re.search(r"\{([^{}]+)\}", path)
    if not match:
        return [path]
    return [expanded for option in match[1].split(",")
            for expanded in _expand_braces(path[:match.start()] + option.strip() + path[match.end():])]


def main() -> int:
    errors: list[str] = []
    ledger = (ROOT / "docs/THIRD_PARTY.md").read_text(encoding="utf-8")
    errors.extend(release_decision_errors(ledger))

    component_ledger = parse_component_ledger(ledger)

    required_ledger_paths = discover_required_ledger_paths(ROOT)
    for path in sorted(required_ledger_paths):
        if not (ROOT / path).is_dir():
            errors.append(f"expected audited area is missing: {path}")
    uncovered = uncovered_component_paths(required_ledger_paths, component_ledger)
    for path in sorted(uncovered):
        errors.append(
            f"THIRD_PARTY.md component ledger has no row with valid "
            f"BLOCK, EXCLUDED, or APPROVED decision for {path}"
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
    for path in (
        *(f"{CODEC_ROOT}/{name}/source" for name in ("draco", "klt", "n4mc", "qndf", "qndf_int8", "vdmc",
                                                    "faster_vdmc", "tvmc", "tsmc", "queen", "rerf", "vega", "3dgstream")),
        "open4d/reconstruction", "integrations/unity",
        "open4d/reconstruction/rgbd/app", "open4d/reconstruction/rgbd/src",
        "open4d/reconstruction/rgbd/include", "open4d/reconstruction/rgbd/python",
        "open4d/reconstruction/rgbd/tools",
    ):
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
