"""Where the module's pieces live on disk.

Resolved from this file's location, which assumes the editable install the README
prescribes (``pip install -e open4d/reconstruction/gs_tools``). A non-editable
install would put the package in site-packages with no ``upstream/`` beside it,
so ``OPEN4D_GS_ROOT`` overrides the guess rather than leaving a confusing
FileNotFoundError deep in a subprocess call.
"""

from __future__ import annotations

import os
from pathlib import Path


def module_root() -> Path:
    """The `gs_tools` module directory (the one holding `upstream/`)."""
    override = os.environ.get("OPEN4D_GS_ROOT")
    if override:
        return Path(override).expanduser().resolve()
    return Path(__file__).resolve().parent.parent


def upstream(name: str) -> Path:
    """Path to a pinned upstream checkout: ``queen`` or ``3dgstream``."""
    return module_root() / "upstream" / name


def patches(name: str) -> Path:
    """Our patch series for one upstream, applied at build time."""
    return module_root() / "patches" / name


def upstream_configs(name: str) -> Path:
    """An upstream's own config directory.

    Defaults point here rather than at copies under this module: a config that
    lists every hyperparameter of a method is exactly the kind of file that drifts
    silently from the code it configures.
    """
    return upstream(name) / "configs"
