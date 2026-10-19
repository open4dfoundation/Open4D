"""Resolve separately acquired runtime trees in current and legacy checkouts.

Each research codec's implementation lives in ``open4d/codecs/<name>/source``
next to its adapter; QUEEN/3DGStream training tooling lives in
``open4d/reconstruction/gs_tools``. Neither tree is part of the wheel, and
imports use a private namespace (see :mod:`open4d.codecs._research`).
"""

import os
from pathlib import Path


def default_runtime(component: str) -> Path:
    """Find a component's research tree without breaking older snapshots.

    Explicit per-runtime paths remain the caller's first choice. The global
    OPEN4D_RESEARCH_ROOT names a checkout, whose older source layouts are also
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
    candidates = [root / "open4d" / "codecs" / component / "source",
                  root / "open4d" / "reconstruction" / component]
    if checkout:
        # Snapshots from before the per-codec layout kept research trees in
        # codecs/<name> (October 2026) or open4d/codecs/<name> (September).
        candidates += [root / "codecs" / component, root / "open4d" / "codecs" / component]
    return next((candidate for candidate in candidates if candidate.is_dir()), candidates[0])


def managed_runtime(component: str) -> Path | None:
    """The runtime registered for a component in the backend store, if any."""
    from .codecs._backends import _registry

    return _registry.runtime(component)


def managed_python(component: str) -> str | None:
    """The registered interpreter for a component; explicit choices win."""
    from .codecs._backends import _registry

    return _registry.python(component)


def managed_tool(component: str, name: str) -> str | None:
    from .codecs._backends import _registry

    return _registry.tool(component, name)


def runtime_checkout(runtime: Path) -> Path | None:
    """Find the enclosing checkout for either layout, without fixed depth."""
    return next((parent for parent in runtime.resolve().parents
                 if (parent / "open4d" / "__init__.py").is_file()), None)
