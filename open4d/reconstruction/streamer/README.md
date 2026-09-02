# streamer

Streaming and playback for 4D reconstructions — whatever their representation.

Separate from the modules that *produce* reconstructions (`gs_tools`, `vega`,
`nevo`, `queen`, `3dgstream`) because it is specific to none of them, and was
never specific to Gaussians. The dependency runs one way: producers import this
to describe and serve their output, and this imports none of them.

```
streamer/
  representations.py   what transport needs to know about a representation
  bundle.py            the view.json contract, shared by both sides
  monitor.py           what actually went over the wire
  server/              sending
  client/              playback: one self-contained browser page
```

## The one abstraction that matters

A **representation** is what a decoded frame *is* — `mesh`, `points`,
`gaussians`, `pixels` — and it is deliberately not the codec that produced it. A
triangle mesh is a mesh whether it arrived as OBJ, as a Draco payload, or out of
a V-DMC bitstream, so one renderer serves every codec of a given shape. The
vocabulary is `open4d.core.Representation`'s, not this module's, so a bundle and
Open4D's own sequence model name the same things the same way.

Adding one is a registration:

```python
from open4d.core import Representation
from streamer.representations import RepresentationSpec, register

register(RepresentationSpec(
    representation=Representation.MESH,
    media_types={".drc": "application/octet-stream"},
    playable=False,          # no renderer in this repo's client yet
))
```

That is all. The server builds its extension map from the registry, so frames
get the right `Content-Type` without an edit; the client keys its renderers off
the same names; and `playable=False` makes a bundle report "no renderer for
mesh" instead of showing an empty pane.

A spec answers only what transport actually asks. It does **not** carry
`has_geometry` — whether a free camera is meaningful is
`open4d.core.Representation`'s to answer, and a second copy would be a second
opinion. It does not describe decoding, which is the client's business and for
some representations cannot be ours at all: ReRF's entropy coder exists only as
a CPython 3.8 binary, which is why `pixels` is a first-class representation
rather than a fallback.

## Sending, receiving, measuring

```python
from streamer import serve, fetch

server = serve("~/bundles/basketball", host="0.0.0.0")   # send
fetch("http://gpu-box:8770", "~/local-copy")             # receive
server.monitor.snapshot()                                # measure
```

`serve` is the simplest transport the model admits: every frame is a file,
reachable in one request, in any order. It binds loopback unless told otherwise
and has **no authentication of any kind**.

`fetch` pulls a bundle, or `only=[...]` some clips of one, onto the machine
you are sitting at — because a 30-frame Gaussian clip is over 100 MB as PLY and
nobody wants that twice over a tunnel. Existing files of the right size are
skipped, so an interrupted transfer resumes by running it again. That is the
whole policy; it is not a sync tool.

`Monitor` counts requests, bytes, errors and a per-clip rollup, exposed at
`/stats.json`. NeVo models byte arrival offline — a bandwidth trace gives
queueing delay, a loss trace gives drops — and this is the same measurement
taken live rather than simulated. It is counters and nothing more: a monitor
that decided things would be a second scheduler, and there is not yet a second
transport to adapt between.

## What is deliberately missing

**The scheduler.** `open4d.core.Dependency` declares that a codec's frames may
need a key frame decoded first (`gop`, as in Vega's group-of-volumes), or that
its decode stream cannot be rewound at all (`sequential`, as in ReRF), and
`Dependency.chain()` returns the frames to decode, in order, to reach a target —
reusing the decoder's current position on a forward seek and reporting a full
replay on a backward one. The declaration exists and is tested. The client that
consumes it, so that seeking in such a stream plans the chain instead of
requesting a frame and hoping, is the next piece and belongs here.

Both bitstreams in this repository currently declare a single group spanning
every frame, so a cold seek to the last of 30 frames costs 30 decodes. That is
the kind of thing the model is for: it was invisible before.

**A mesh or point renderer.** Both have geometry in core and are registered
here; neither has a WebGL renderer yet, which is the one thing standing between
Open4D's mesh sequences and this client.

## Tests

```bash
python -m pytest streamer_tests -q
```

No GPU, no display, no dataset.
