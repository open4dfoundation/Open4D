"""Adapters over the pinned upstream trainers.

Each adapter translates Open4D's arguments into an upstream CLI and runs it as a
subprocess. They cannot share a process: both upstreams use flat imports and both
define `scene`, `utils`, and `arguments`.

`queen` and `gstream` wrap trainers. `vega` and `rerf` wrap the *output* side
only -- both are produced by their own CLIs under
`open4d/reconstruction/{vega,nevo}`, and what was missing was any way to look at
what those produced. See their module docstrings for why one exports Gaussians
and the other cannot.
"""

from importlib import import_module

__all__ = ["base", "gstream", "queen", "rerf", "vega"]


def __getattr__(name):
    # Building a QUEEN/3DGStream command must not import the optional browser
    # bundle exporters, which depend on the separately installed streamer.
    if name not in __all__:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    module = import_module(f".{name}", __name__)
    globals()[name] = module
    return module
