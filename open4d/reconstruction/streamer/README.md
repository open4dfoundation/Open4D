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
  export.py            any open4d.Sequence as a bundle
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
))
```

That is all. The server builds its extension map from the registry, so frames
get the right `Content-Type` without an edit, and the client keys its renderers
off the same names.

Every representation core defines is playable today, so nothing passes
`playable=False`. The flag stays because the next representation will arrive
before its renderer does, and a bundle declaring something the client cannot
draw should report "no renderer for X" rather than show an empty pane.

A spec answers only what transport actually asks. It does **not** carry
`has_geometry` — whether a free camera is meaningful is
`open4d.core.Representation`'s to answer, and a second copy would be a second
opinion. It does not describe decoding, which is the client's business and for
some representations cannot be ours at all: ReRF's entropy coder exists only as
a CPython 3.8 binary, which is why `pixels` is a first-class representation
rather than a fallback.

## Playing Open4D's own sequences

`export.from_source` hands anything `open4d.load` reads to the client, so a mesh
sequence gets the same viewport, camera and scrubbing a Gaussian one does:

```python
from streamer import from_source
from_source("captures/basketball_player", "~/bundles/basketball", fps=10)
```

Whether a clip is `mesh` or `points` is read from the geometry, not the file
extension -- both write `.ply`. Frames come from `open4d.io.write_sequence`,
which already emits one `frame_NNNNNN.ply` per frame, so this chooses the
representation, records bounds over the whole clip and hands the rest to code
that already existed.

The renderer is flat-shaded from screen-space derivatives rather than from
stored normals: Open4D's PLY writer refuses to store normals, and a normal
derived in the fragment shader is right for whatever geometry actually arrived.
Point clouds are shaded as small spheres from the point sprite's own
coordinates. A mesh frame carrying no faces falls through to points rather than
drawing nothing.

Verified on the 10-frame `basketball_player` OBJ sequence the TVMC codec
vendors -- ~20k vertices and ~39k triangles a frame, loaded through
`open4d.load`, parsed by the shipped client under Node in
`streamer_tests/test_client_parsers.py`.

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

**A held-out camera, and numbers.** Compare puts several methods at one rig
pose, which is the mode a PSNR or SSIM figure could attach to, and nothing
computes one yet. Two caveats have to travel with it when it lands: every rig
camera was a training view for both Vega and ReRF, so this measures
reconstruction rather than generalisation, and ReRF renders at different
intrinsics from the captured pane.

## Tests

```bash
python -m pytest streamer_tests -q
```

No GPU, no display, no dataset. The client's frame parsers are run as shipped,
cut out of `viewer.html` and executed under Node — those cases skip if `node` is
absent, which is stated rather than silently covering less.
