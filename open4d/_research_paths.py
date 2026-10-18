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
    accepted. Without it, a backend installed or attached with ``open4d
    backend`` is used before this checkout's own tree. No runtime code is
    imported or downloaded by this lookup.
    """
    checkout = os.environ.get("OPEN4D_RESEARCH_ROOT")
    if not checkout:
        managed = managed_runtime(component)
        if managed is not None:
            return managed
    root = Path(checkout).expanduser().resolve() if checkout else Path(__file__).resolve().parents[1]
    candidates = (root / "codecs" / component,
                  root / "open4d" / "codecs" / component,
                  root / "open4d" / "reconstruction" / component)
    return next((candidate for candidate in candidates if candidate.is_dir()), candidates[0])


def managed_runtime(component: str) -> Path | None:
    """The runtime registered for a component in the backend store, if any."""
    from .backends import _registry

    return _registry.runtime(component)


def managed_python(component: str) -> str | None:
    """The registered interpreter for a component; explicit choices win."""
    from .backends import _registry

    return _registry.python(component)


def managed_tool(component: str, name: str) -> str | None:
    from .backends import _registry

    return _registry.tool(component, name)


def runtime_checkout(runtime: Path) -> Path | None:
    """Find the enclosing checkout for either layout, without fixed depth."""
    return next((parent for parent in runtime.resolve().parents
                 if (parent / "open4d" / "__init__.py").is_file()), None)
