"""Resolve separately acquired runtime trees in current and legacy checkouts.

Optional research codec implementations used by :mod:`open4d.codec`.
Their physical ``codecs/`` directory is not a Python package, because that
name belongs to the standard library; imports use a private namespace.
"""

import os
from pathlib import Path


def default_runtime(component: str) -> Path:
    """Find ``codecs/<component>`` without breaking authorized older snapshots.

    Explicit per-runtime paths remain the caller's first choice. The global
    OPEN4D_RESEARCH_ROOT names a checkout, whose old source layout is also
    accepted. No runtime code is imported or downloaded by this lookup.
    """
    checkout = os.environ.get("OPEN4D_RESEARCH_ROOT")
    root = Path(checkout).expanduser().resolve() if checkout else Path(__file__).resolve().parents[1]
    candidates = (root / "codecs" / component,
                  root / "open4d" / "codecs" / component,
                  root / "open4d" / "reconstruction" / component)
    return next((candidate for candidate in candidates if candidate.is_dir()), candidates[0])


def runtime_checkout(runtime: Path) -> Path | None:
    """Find the enclosing checkout for either layout, without fixed depth."""
    return next((parent for parent in runtime.resolve().parents
                 if (parent / "open4d" / "__init__.py").is_file()), None)
