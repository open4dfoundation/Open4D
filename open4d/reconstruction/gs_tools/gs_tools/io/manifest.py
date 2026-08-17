"""The per-run manifest docs/artifacts.md asks every benchmark result to carry.

One JSON file per run directory, written by whichever verb produced the run and
extended by later verbs. `render` and `metrics` add to the manifest `train`
wrote, so a finished run carries dataset, frame range, upstream commit, config,
environment, byte counts, timing, and quality in one place -- which is the
minimum for a result to be comparable to anything else.
"""

from __future__ import annotations

import json
import subprocess
import time
from pathlib import Path
from typing import Any

MANIFEST_NAME = "manifest.json"


def _open4d_revision() -> str | None:
    """The Open4D commit that produced the run, dirty flag included."""
    here = Path(__file__).resolve()
    try:
        sha = subprocess.run(
            ["git", "-C", str(here.parent), "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        ).stdout.strip()
        dirty = subprocess.run(
            ["git", "-C", str(here.parent), "status", "--porcelain"],
            capture_output=True,
            text=True,
            check=False,
        ).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None
    if not sha:
        return None
    return f"{sha}-dirty" if dirty else sha


def path_for(run_dir: Path) -> Path:
    return Path(run_dir) / MANIFEST_NAME


def read(run_dir: Path) -> dict[str, Any]:
    """Existing manifest, or an empty dict if the run has none yet."""
    path = path_for(run_dir)
    if not path.exists():
        return {}
    return json.loads(path.read_text())


def write(run_dir: Path, data: dict[str, Any]) -> Path:
    """Replace the manifest wholesale."""
    run_dir = Path(run_dir)
    run_dir.mkdir(parents=True, exist_ok=True)
    path = path_for(run_dir)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    return path


def update(run_dir: Path, **fields: Any) -> dict[str, Any]:
    """Merge fields into the manifest, one level deep.

    Shallow merge so that `metrics` adding a key under "quality" does not discard
    what `train` recorded there, while a plain scalar is still just replaced.
    """
    data = read(run_dir)
    for key, value in fields.items():
        if isinstance(value, dict) and isinstance(data.get(key), dict):
            data[key].update(value)
        else:
            data[key] = value
    data["updated"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    write(run_dir, data)
    return data


def start(
    run_dir: Path,
    *,
    method: str,
    scene: Path,
    layout: str,
    frame_count: int,
    config: Path | None,
    environment: dict[str, Any],
    command: list[str],
) -> dict[str, Any]:
    """Open a manifest for a run that is about to start."""
    return update(
        run_dir,
        method=method,
        source={
            "scene": str(scene),
            "layout": layout,
            "frame_count": frame_count,
        },
        config=str(config) if config else None,
        revision={"open4d": _open4d_revision(), "upstream": environment.get("upstream", {})},
        environment=environment,
        command=command,
        started=time.strftime("%Y-%m-%dT%H:%M:%S%z"),
    )


def finish(run_dir: Path, *, seconds: float, exit_status: int) -> dict[str, Any]:
    """Record how a run ended. A nonzero status is kept, not hidden."""
    return update(run_dir, timing={"wall_seconds": round(seconds, 3)}, exit_status=exit_status)
