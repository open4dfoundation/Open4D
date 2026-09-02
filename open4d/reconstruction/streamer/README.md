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
  live.py              clips rendered as they are watched
  server/              sending
  client/              playback, and the scheduler that plans a seek
```

## Two transports, because the modules genuinely differ

Measured on this repository's own content, per frame and at 30 fps:

| on the wire | per frame | at 30 fps |
| --- | --- | --- |
| QUEEN `.splat`, 439k gaussians | 14 MB | 422 MB/s |
| Vega decoded PLY | 4.31 MB | 129 MB/s |
| Vega's own bitstream | 2.44 MB | 73 MB/s |
| mesh PLY, 20k verts | 760 kB | 23 MB/s |
| mesh through Draco | 291 kB | 8.7 MB/s |
| ReRF bitstream | 533 kB | 16 MB/s |
| **ReRF rendered, one view** | **47 kB** | **1.4 MB/s** |

Sending geometry buys a free camera and costs LAN-class bandwidth. Sending
pixels costs two orders of magnitude less and fixes the viewpoint. Neither is
the right answer for every module, so both exist:

* **Client-decode** — a frame list, fetched and parsed by the `Scheduler`.
  Free camera, `mesh`/`points`/`gaussians`.
* **Server-render** — `live.mjpeg(url, ...)`, a URL instead of a frame list.
  The only transport ReRF has at all, its entropy coder being a sourceless
  CPython 3.8 binary, and the only one here that works over a tunnel.

A live clip gets **its own scene**, because a live renderer chooses its own
camera and putting one beside a rig pose would break the guarantee Compare
exists to make. It has no timeline either, so the transport bar disables itself
rather than leaving a scrubber that does nothing.

### Live is not the same as replay

MJPEG carries a decode-and-render-on-demand loop and a slideshow of files
equally well, and this repository has both, so `live.mjpeg` requires an
`origin` and has no default — a default would let a slideshow be presented as
live by saying nothing:

| origin | means | example here |
| --- | --- | --- |
| `rendered` | decoded and drawn per frame, on demand | Vega's wall demo |
| `replay` | frames rendered earlier, looped over the same transport | NeVo's viewer |

`rendered` does not claim the *encode* is happening live — it isn't, any more
than it is for a video. It claims the decode is. NeVo is `replay` because a
NeRF frame takes about half a second to ray-march, which is not a playback
rate; its own status page has always said so.

### Running the live demo

The streaming loop is Vega's, driven from this tree:

```bash
cd open4d/reconstruction/vega
PYTHONPATH=. python -m orbitvega.wall_demo \
    --bitstream-dir <orbitvega.prepare output> \
    --objects basketball dancer mitch thomas \
    --chunk-port 8801 --mjpeg-port 8800
```

Then register it and serve:

```python
from streamer import bundle, live, serve
clip = live.mjpeg("http://127.0.0.1:8800/stream", name="vega-live",
                  origin="rendered", scene="Vega live")
bundle.write(out, title="live", source="vega", clips=[clip])
serve(out, port=8770)
```

The chunk server's access log **is** the streaming loop: one `GET
/<object>/frame_NNNN.pt` per object per frame, reassembled by
`vega.player.StreamingPlayer`, colour-decoded from the hash grid, culled and
scheduled per `vega.pipeline`, then rasterised. Measured at about 14 frames in
6 seconds for four objects at 360 px tiles on one RTX 4090 — which is the
honest rate for decoding four Gaussian sequences per frame, not a target.

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

## Planning a seek

A clip declares how its frames depend on one another, in
`open4d.core.Dependency`'s vocabulary: `independent`, `gop` (frames need the
group's key frame first, as in Vega's group-of-volumes) or `sequential` (the
decode stream does not rewind at all, as in ReRF). The manifest carries it per
clip, absent meaning independent, and the client's `Scheduler` plans the chain
rather than requesting a frame and hoping.

The asymmetry is the point. A forward step reuses where the decoder already
sits; a backward seek in a sequential stream comes back as a full replay from
zero, because that stream cannot be rewound. Both bitstreams here declare a
single group spanning every frame, so a cold seek to the last of 30 costs 30
decodes — the kind of thing that was invisible before it was modelled.

`chain` exists twice, in Python and transcribed into the client, because the
browser cannot run Python. Two implementations of one rule drift, so
`streamer_tests/test_scheduler.py` runs both over the same 56 cases and asserts
they agree. Change one and that test tells you about the other.

The Scheduler also owns caching and look-ahead, which the two clip sources used
to keep separately with different policies — and only one of which evicted, so
scrubbing a long pixel clip kept every frame it had ever shown. Buffer
occupancy, decode count and bytes are in the viewer's stats panel; decode order
and replay count appear only when something is not independent, since a replay
count of zero says nothing about an independent clip.

**Every exporter here writes independent frames**, because they all decode
before they write. The `gop` and `sequential` paths are therefore tested rather
than exercised: they are what a producer serving a bitstream *as* its frames
would need, and building them afterwards would mean changing the client at the
same time as trusting a new encoder.

## What is deliberately missing

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
