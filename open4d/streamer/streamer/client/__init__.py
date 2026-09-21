"""The playback client: one self-contained page, served to a browser.

A browser rather than a native window because of where the content is. Training
and rendering happen on the machine with the GPU, and that machine frequently
has no display attached -- SIBR needs OpenGL 4.5 and X11 forwarding does not
substitute -- so a native viewer cannot be pointed at a run from a laptop. A
local HTTP server and WebGL2 can, and the same page then costs a public user
nothing to install.

`viewer.html` plays whichever representations it has a renderer for, keyed by
`open4d.core.Representation` (see the ``REPRESENTATIONS`` table at the top of
its script). Adding a representation is an entry in that table plus a renderer,
not a new branch in every function that touches a clip.
"""

from __future__ import annotations

from pathlib import Path

VIEWER_NAME = "viewer.html"


def viewer_path() -> Path:
    """The packaged client page."""
    return Path(__file__).resolve().parent / VIEWER_NAME
