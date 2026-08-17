"""Adapters over the pinned upstream trainers.

Each adapter translates Open4D's arguments into an upstream CLI and runs it as a
subprocess. They cannot share a process: both upstreams use flat imports and both
define `scene`, `utils`, and `arguments`.
"""

from . import base, gstream, queen

__all__ = ["base", "gstream", "queen"]
