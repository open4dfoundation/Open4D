"""Streaming and playback for 4D reconstructions.

Separated from the modules that *produce* reconstructions (`gs_tools`, `vega`,
`nevo`, `queen`, `3dgstream`) because it is not specific to any of them, and was
never specific to Gaussians: what it streams is whatever
`open4d.core.Representation` a bundle declares. The dependency runs one way --
producers import this to describe and serve their output; this imports none of
them.

The pieces, smallest first:

``codecs``
    What is on the wire, and who can decode it. A codec is a wire format that
    produces a representation, keyed by both because the same ``.ply`` is a
    3DGS cloud or a mesh depending on which is asking. ``decodes`` says whether
    a browser can turn it back into geometry, which is the question that decides
    whether a module can be streamed at all.
``representations``
    What a decoded frame is, and whether the packaged client can render it.
    Suffixes are derived from the codecs rather than listed again here.
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

The scheduler that consumes `open4d.core.Dependency` lives in the client rather
than in this package, because seeking is the client's side of the problem: a
codec's frames may need the group's key frame decoded first (``gop``), or its
decode stream may not rewind at all (``sequential``), and a client that requests
a frame and hopes gets the wrong picture. ``Scheduler`` in
``client/viewer.html`` plans the chain a seek needs, then caches and prefetches
along it. Its ``chain`` is a transcription of `Dependency.chain` into
JavaScript, which is the liability two implementations of one rule always carry,
so ``streamer_tests/test_scheduler.py`` runs both over the same cases.

What is deliberately *not* here is adaptation. `monitor` records what a playback
cost and decides nothing with it: a bundle carries one quality per clip, so
there is nothing yet to adapt between, and a monitor that chose would be a
second scheduler with no second option to choose from.
"""

from __future__ import annotations

from . import (
    bundle,
    client,
    codecs,
    export,
    live,
    monitor,
    representations,
    server,
    transfer,
)
from .bundle import Clip
from .client import viewer_path
from .export import from_sequence, from_source
from .monitor import Monitor, Transfer
from .codecs import CodecSpec
from .representations import RepresentationSpec
from .server import DEFAULT_PORT, serve
from .transfer import fetch

__all__ = [
    "Clip",
    "CodecSpec",
    "DEFAULT_PORT",
    "Monitor",
    "RepresentationSpec",
    "Transfer",
    "bundle",
    "client",
    "codecs",
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
