"""Shared plumbing for `visualize_sequence.py`.

Absorbs the boring parts: finding the repository when Open4D is not installed,
and reporting missing optional dependencies.
"""

from __future__ import annotations

import sys
from importlib import import_module
from pathlib import Path
from typing import Any

# examples/visualization/_common.py -> the repository root is two levels up.
REPO_ROOT = Path(__file__).resolve().parents[2]

# Allow `python examples/visualization/visualize_sequence.py` to work in a fresh
# clone, before `pip install -e .` has been run.
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))


def require(module: str, extra: str) -> Any:
    """Import *module*, or exit with the pip command that provides it."""
    try:
        return import_module(module)
    except ImportError:
        sys.exit(
            f"This example needs {module!r}, which is not installed.\n"
            f"Install it with: python -m pip install -e '.[{extra}]'"
        )


def existing_source(path: Path) -> Path:
    """Return *path*, or exit explaining what a source looks like.

    Whether the suffix is actually supported is decided by
    `frame_sources.source_kind`, which reports the full format list.
    """
    if path.exists():
        return path
    sys.exit(
        f"{path} does not exist.\n"
        "Pass a folder holding one mesh file per frame, or a USD container.\n"
        "Run with --help to see every format."
    )
