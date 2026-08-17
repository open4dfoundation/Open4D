"""Importing a module out of a pinned upstream checkout.

The two upstreams both expect to be run from their own root with flat imports
(`from scene import Scene`, `from utils.loss_utils import ssim`), and their module
names collide: both have `scene`, `utils`, and `arguments`. So they can never be
on `sys.path` at the same time in one process. Anything that needs to run both --
a parity test, a comparison render -- runs them as subprocesses instead, which is
also how the method adapters work.

This helper is for the narrow in-process case: pulling one leaf module out of one
tree, like `lpipsPyTorch`, where reproducing published numbers means running
upstream's code rather than a reimplementation.
"""

from __future__ import annotations

import importlib
import os
import sys
from contextlib import contextmanager
from pathlib import Path
from types import ModuleType

from . import paths


@contextmanager
def on_path(name: str):
    """Temporarily put an upstream checkout at the front of `sys.path`."""
    root = paths.upstream(name)
    if not root.exists():
        raise FileNotFoundError(f"{root} is missing; run scripts/setup.sh")
    entry = str(root)
    sys.path.insert(0, entry)
    try:
        yield root
    finally:
        try:
            sys.path.remove(entry)
        except ValueError:
            pass


def module(upstream_name: str, dotted: str) -> ModuleType:
    """Import `dotted` from the named upstream checkout.

    Not cached, and not removed from `sys.modules` afterwards: undoing an import
    is not something Python supports cleanly, and a second call for a colliding
    name from the other tree would be a bug in the caller, not something this
    function can paper over.
    """
    with on_path(upstream_name):
        return importlib.import_module(dotted)


def python_path(upstream_name: str) -> str:
    """PYTHONPATH value for running an upstream script as a subprocess."""
    root = Path(paths.upstream(upstream_name))
    existing = os.environ.get("PYTHONPATH", "")
    return f"{root}:{existing}" if existing else str(root)
