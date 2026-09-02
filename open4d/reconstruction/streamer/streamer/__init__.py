"""Streaming and playback for 4D reconstructions.

Separated from the modules that *produce* reconstructions (`gs_tools`, `vega`,
`nevo`, `queen`, `3dgstream`) because it is not specific to any of them, and was
never specific to Gaussians: what it streams is whatever
`open4d.core.Representation` a bundle declares. The dependency runs one way --
producers import this to describe and serve their output; this imports none of
them.

The pieces, smallest first:

``representations``
    The pluggable bit. A representation registers what transport needs to know
    about it -- frame suffixes and their ``Content-Type``, and whether the
    packaged client can render it -- and nothing else. The server builds its
    extension map from this rather than a hardcoded list, so a new
    representation is a registration, not an edit in three files.
``bundle``
    The ``view.json`` contract: clips, their representation, their frames, and
    the capture rig that lets several of them share a camera. Belongs to neither
    side, which is why it sits above both.
``monitor``
    Counters for what actually went over the wire. NeVo models byte arrival
    offline; this is the same measurement taken live.
``server``
    Sending. Today one transport: static files for a local bundle, plus the
    counters at ``/stats.json``.
``live``
    Clips rendered as they are watched -- the transport for a representation
    that cannot be decoded in a browser, and the only one here cheap enough
    for a real link.
``transfer``
    Receiving. Pull a bundle, or some clips of one, onto the machine you are
    sitting at.
``export``
    Any `open4d.Sequence` as a bundle -- which is how Open4D's own mesh and
    point-cloud sequences, in any format `open4d.load` reads, reach this
    client.
``client``
    Playback. One self-contained browser page, no build step and no CDN.

What is deliberately *not* here yet is the scheduler: `open4d.core.Dependency`
declares that a codec's frames may need a key frame first, or that its decode
stream cannot be rewound, and a client that seeks in such a stream needs to plan
the chain rather than request a frame and hope. The declaration exists and is
tested; the scheduler that consumes it is the next piece, and belongs here.
"""

from __future__ import annotations

from . import bundle, client, export, live, monitor, representations, server, transfer
from .bundle import Clip
from .client import viewer_path
from .export import from_sequence, from_source
from .monitor import Monitor, Transfer
from .representations import RepresentationSpec
from .server import DEFAULT_PORT, serve
from .transfer import fetch

__all__ = [
    "Clip",
    "DEFAULT_PORT",
    "Monitor",
    "RepresentationSpec",
    "Transfer",
    "bundle",
    "client",
    "export",
    "live",
    "fetch",
    "from_sequence",
    "from_source",
    "monitor",
    "representations",
    "server",
    "serve",
    "transfer",
    "viewer_path",
]
