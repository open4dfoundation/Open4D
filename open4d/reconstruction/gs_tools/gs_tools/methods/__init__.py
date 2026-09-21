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

from . import base, gstream, queen, rerf, vega

__all__ = ["base", "gstream", "queen", "rerf", "vega"]
