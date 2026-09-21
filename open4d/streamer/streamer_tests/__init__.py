"""Shared fixtures locating repository data, wherever this package sits.

`open4d_tree` exists because three tests counted ``parents[3]`` from their own
file to reach the `open4d` package directory, and moving `streamer` up one
level made all three resolve one directory too high. They did not fail: the
dataset path simply did not exist, so they skipped, reporting "not present"
about data that was there all along. A hop count is a claim about where this
package lives, and this package has now moved twice.
"""

from __future__ import annotations

from pathlib import Path


def open4d_tree() -> Path:
    """The `open4d` package directory, from the installed package itself."""
    import open4d

    return Path(open4d.__file__).resolve().parent
