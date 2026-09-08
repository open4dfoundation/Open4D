"""Run manifests and representation IO.

The viewable-bundle manifest moved to `streamer.bundle`: it is the contract
between a playback client and a server, so it belongs to the streaming module
rather than to a module that produces reconstructions.
"""

from . import manifest, ply, splat

#: The formats a Gaussian frame can be written in. Here rather than in one
#: exporter because two of them offer the choice, and two tuples of format
#: names is one that goes stale -- adding a format has to reach both.
GAUSSIAN_FORMATS = ("ply", "splat")

__all__ = ["GAUSSIAN_FORMATS", "manifest", "ply", "splat"]
