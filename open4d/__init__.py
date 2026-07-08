"""Open4D — an open toolkit for 4D (time-varying) geometry.

Import the package under a short alias and reach everything through a flat,
stable namespace instead of the internal folder layout::

    import open4d as o4d

    # native container IO (numpy-only, always available)
    with o4d.io.O4DMeshWriter("clip.o4d") as w:
        w.add_frame(vertices, faces, timestamp=0.0)

    reader = o4d.O4DMeshReader("clip.o4d")   # common IO also hoisted to top level

    # interactive playback (needs the optional GUI extras)
    o4d.player.play_o4d_mesh("clip.o4d", fps=30.0)

Public subpackages
------------------
- ``o4d.io``       readers/writers for the ``.o4d`` container (meshes, point
                   clouds, Draco point clouds). numpy-only, always importable.
- ``o4d.player``   PyQt6/pyqtgraph viewers for ``.o4d`` streams (optional GUI
                   extras).
- ``o4d.metrics``  quality/rate-distortion metrics.
- ``o4d.modules``  compression/editing pipelines (TVMC, Unity decoder).
- ``o4d.core``     heavy research cores (TSMC, TVMC, N4MC).
- ``o4d.tools``    command-line helpers for authoring ``.o4d`` assets.

Everything is resolved lazily (PEP 562): importing :mod:`open4d` touches no
optional dependency, so ``import open4d`` never fails because PyQt, torch, or a
research core is missing — the cost is paid only when you touch that attribute.
"""
from importlib import import_module
from typing import TYPE_CHECKING

try:  # keep in sync with pyproject.toml [project].version
    from importlib.metadata import version as _pkg_version

    __version__ = _pkg_version("open4d")
except Exception:  # not installed (running from a source checkout)
    __version__ = "0.1.0"

# Public subpackages, resolved on first attribute access.
_SUBMODULES = frozenset(
    {"io", "player", "metrics", "modules", "core", "tools"}
)

# Convenience names hoisted to the top level -> (submodule, attribute).
# These let ``o4d.O4DMeshReader`` work without spelling out ``o4d.io``.
_ATTR_TO_MODULE = {
    "O4DMeshWriter": "open4d.io",
    "O4DMeshReader": "open4d.io",
    "O4DPointCloudWriter": "open4d.io",
    "O4DPointCloudReader": "open4d.io",
    "O4DDracoPointCloudWriter": "open4d.io",
    "O4DDracoPointCloudReader": "open4d.io",
    "MeshSequence": "open4d.core",
    "MeshFrame": "open4d.core",
}

__all__ = ["__version__", *sorted(_SUBMODULES), *sorted(_ATTR_TO_MODULE)]


def __getattr__(name: str):
    """Lazily import subpackages and hoisted symbols on first access (PEP 562)."""
    if name in _SUBMODULES:
        module = import_module(f"{__name__}.{name}")
        globals()[name] = module  # cache so later lookups skip __getattr__
        return module
    if name in _ATTR_TO_MODULE:
        module = import_module(_ATTR_TO_MODULE[name])
        obj = getattr(module, name)
        globals()[name] = obj
        return obj
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


def __dir__():
    return sorted(set(globals()) | set(__all__))


if TYPE_CHECKING:  # give static tooling the real symbols without runtime cost
    from . import io, player, metrics, modules, core, tools  # noqa: F401
    from .io import (  # noqa: F401
        O4DMeshWriter,
        O4DMeshReader,
        O4DPointCloudWriter,
        O4DPointCloudReader,
        O4DDracoPointCloudWriter,
        O4DDracoPointCloudReader,
    )
