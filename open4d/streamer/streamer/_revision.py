"""The Open4D commit a bundle was produced by."""

from __future__ import annotations

import subprocess
from pathlib import Path


def open4d_revision() -> str | None:
    """The Open4D commit that produced a bundle, dirty flag included.

    Deliberately a copy of the helper in `gs_tools.io.manifest` rather than an
    import of it: that one stamps *run* manifests, and a streaming module must
    not depend on a reconstruction module -- the dependency runs the other way.
    Ten lines of duplication is the cheaper of the two prices.
    """
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
