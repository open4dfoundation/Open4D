# streamer

Streaming and playback for 4D reconstructions — whatever their representation.

Separate from the modules that *produce* reconstructions (`gs_tools`, `vega`,
`nevo`, `queen`, `3dgstream`) because it is specific to none of them, and was
never specific to Gaussians. The dependency runs one way: producers import this
to describe and serve their output, and this imports none of them.

```
streamer/
  codecs.py            what is on the wire, and who can decode it
  representations.py   what a decoded frame is, and whether it renders here
  bundle.py            the view.json contract, shared by both sides
  monitor.py           what actually went over the wire
  export.py            any open4d.Sequence as a bundle
  live.py              clips rendered as they are watched
  server/              sending
  client/              playback, the scheduler, and the decode worker
```

Decoding happens in a worker (`client/worker.js`), not on the page's thread.
Parsing is the one expensive synchronous step in playback — 16.5 ms for a 3DGS
PLY, 32.1 ms for a 439k-Gaussian `.splat`, 17.8 ms for a Draco mesh — and on
the main thread each of those is a missed `requestAnimationFrame` for *every*
pane, because there is only one main thread. Four Gaussian panes blocked it for
66 ms a frame, two whole budgets at 30 fps; now they block it for about 1 ms
and parse while the previous frame draws.

The codecs live in the worker and nowhere else. Sharing them with the page would
mean either duplicating them, which drifts, or a build step, which this client
does not have. Results come back with their buffers **transferred**, since a
4.3 MB frame parses to several megabytes of typed arrays and copying those back
would return much of what the worker saved.

`decodeImage` is the exception and stays on the page: it needs `Image`, and a
browser already decodes an image off the main thread, so there is nothing to
move.

## The codec axis

Two questions, deliberately separate. A **representation** is what a decoded
frame *is* — `mesh`, `points`, `gaussians`, `pixels`. A **codec** is the format
it travels in and whether the client can decode it:

| representation | suffix | codec | decodes | |
| --- | --- | --- | --- | --- |
| mesh | `.ply` | `mesh-ply` | client | interchange |
| mesh | `.drc` | `mesh-draco` | client | lossy, 12.9x |
| points | `.ply` / `.drc` | `points-ply` / `points-draco` | client | |
| gaussians | `.ply` | `3dgs-ply` | client | interchange |
| gaussians | `.splat` | `splat` | client | lossy, degree-0 only |
| pixels | `.jpg` / `.png` | `jpeg` / `png` | client | fixed viewpoint |
| pixels | `.rerf` | `rerf` | **server** | undecodable in a browser |

**The key is (representation, suffix), not suffix.** `.ply` appears three times
above and needs two different parsers — a 3DGS PLY and a mesh PLY share an
extension and nothing else. Keying on the extension alone is what previously
forced the client to carry a hand-written dispatcher per representation.

`decodes` is what makes the platform honest about heterogeneous modules.
`codecs.client_decodable(representation)` answers "can I stream this to a
browser at all" as a lookup rather than by reading the client's source. For
ReRF the answer is permanently no — its entropy coder ships only as a CPython
3.8 binary — so it is registered as producing `pixels` server-side, which is
the representation it actually delivers.

A lossy codec **must** state its cost; the constructor refuses one that does
not, because that line is what reaches the person looking at the render.

Adding a codec is one registration plus a parser:

```python
codecs.register(codecs.CodecSpec(
    name="mesh-vdmc", suffix=".v4d", representation=Representation.MESH,
    lossy=True, cost="V-DMC is lossy at any rate worth using",
))
```

The server's extension map, each representation's `media_types`, and the
client's `CODECS` table all follow from this. The Python registry and the
client's table are two hand-maintained lists in two languages, so
`gs_tools_tests/test_representation_vocabulary.py` asserts they agree — a
promise on one side with no parser on the other is a blank pane.

## Two transports, because the modules genuinely differ

Measured on this repository's own content, per frame and at 30 fps:

| on the wire | per frame | at 30 fps |
| --- | --- | --- |
| QUEEN `.splat`, 439k gaussians | 14 MB | 422 MB/s |
| Vega decoded PLY | 4.31 MB | 129 MB/s |
| Vega's own bitstream | 2.44 MB | 73 MB/s |
| mesh PLY, 20k verts | 760 kB | 23 MB/s |
| mesh through **Draco**, decoded in the browser | **59 kB** | **1.8 MB/s** |
| ReRF bitstream | 533 kB | 16 MB/s |
| **ReRF rendered, one view** | **47 kB** | **1.4 MB/s** |

Sending geometry buys a free camera and costs LAN-class bandwidth. Sending
pixels costs two orders of magnitude less and fixes the viewpoint. Neither is
the right answer for every module, so both exist:

* **Client-decode** — a frame list, fetched and parsed by the `Scheduler`.
  Free camera, `mesh`/`points`/`gaussians`. For meshes this is a genuinely
  *compressed* wire format: see Draco below.
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

## Draco: a compressed format, decoded in the client

The first thing here that puts a *compression* of geometry on the wire rather
than an interchange dump of it. Measured on the mesh sequence the TVMC codec
vendors, at Draco's 14-bit position quantisation:

| | per frame | 10-frame clip | at 30 fps |
| --- | --- | --- | --- |
| PLY | 761 kB | 7.4 MB | 23 MB/s |
| Draco | **59 kB** | **0.61 MB** | **1.8 MB/s** |

12.9x, and it decodes *faster* — 17.8 ms against 20.1 ms for parsing the PLY in
JavaScript, because the WASM decoder does the work the JS parser was doing by
hand.

```python
from streamer import from_source
from_source("captures/basketball_player", out, fps=10, frame_format="draco")
```

The decoder is Google's, vendored under `client/vendor/draco` and served by the
bundle server from this origin — never a CDN, which is what keeps the page free
of external dependencies. `open4d[draco]` is needed on the encoding side only;
the client needs nothing installed.

Lossy in two bounded ways, both in the clip's notes and measured in
`streamer_tests/test_draco.py`:

* **Positions are quantised.** At 14 bits the worst vertex moved 0.0046% of the
  model's diagonal on that sequence. 11 bits saves a further 20% for eight
  times the error, which is why 14 is the default.
* **Duplicate vertices are merged**, so the decoded count is lower than the
  encoder was given — 20,672 to 19,747 there, because the source OBJ splits
  vertices at seams. Deduplication, not quantisation; it does not change the
  surface.

A `.drc` frame is a delivery form. The PLY stays the source of truth, and
re-encoding from a `.drc` would compound the quantisation.

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

It speaks HTTP/1.1 with keep-alive, which is not a detail. The default in
Python's `http.server` is 1.0, closing after every response: one handshake and
one fresh slow-start per frame. On loopback that is invisible, which is why it
survived a long time; over a 20 ms link a 59 kB Draco frame then costs a setup
round trip plus about three more while the congestion window opens — roughly
80 ms a frame, a ~12 fps ceiling *regardless of bandwidth*. Any rate measured
through it would have been measuring that rather than the network.

Keep-alive needs `TCP_NODELAY` to be worth having. Without it a response's two
writes — headers, then body — hit Nagle's algorithm against the client's
delayed-ACK timer, and 30 frames took **1.20 s instead of 0.01 s**, 40 ms
each. Closing the connection had been masking it, so the stall only appeared
once keep-alive worked. There is a timed regression test.

`fetch` pulls a bundle, or `only=[...]` some clips of one, onto the machine you
are sitting at — a 30-frame Gaussian clip is over 100 MB as PLY and nobody
wants that twice over a tunnel. Complete files are skipped and a partial one is
**continued from where it stopped** with a byte range, so a tunnel that drops
mid-frame costs what was missed rather than the whole frame. It checks that the
server actually answered `206` before appending: a `200` means the range was
ignored, and appending then would build a corrupt file of exactly the size the
completeness check accepts.

`Monitor` counts requests, bytes, errors and a per-clip rollup, exposed at
`/stats.json`. It is counters and nothing more, deliberately: a monitor that
decided things would be a second scheduler.

## Quality rungs

A clip can carry the same content at several rates:

```python
bundle.Clip(
    name="thomas-rerf-cam00", representation="pixels",
    frames=[...],                                   # the default rendition
    variants=[bundle.Variant(name="low", frames=[...], bytes=91_000,
                             quality={"psnr": 40.45, "ssim": 0.9723}).as_dict()],
)
```

Measured on `g_thomas`, from one export at `--rungs high,medium,low`:

| rung | resolution | kB/frame | Mbit/s @30 | PSNR | SSIM |
| --- | --- | --- | --- | --- | --- |
| default | 1280×960 q92 | 33.5 | 8.04 | 45.91 | 0.9902 |
| medium | 640×480 q88 | 9.9 | 2.38 | 43.72 | 0.9853 |
| low | 320×240 q80 | 3.0 | 0.73 | 40.45 | 0.9723 |

Four things about the shape, each of which was a choice:

**Rungs live inside a clip, not as sibling clips.** A clip is one method's
output for one subject at one station; a variant is one of the ways to get it.
Three sibling clips would be three panes with nothing able to switch between
them mid-playback.

**`frames` stays the default rendition** rather than moving into the variant
list, so a reader that has never heard of variants plays the clip correctly and
needs no change. That is why this did not bump `VERSION` — it is additive.

**Rate and quality are measured, not predicted.** A system that chooses before
encoding has to model them; a bundle holds content that already exists, so
bytes come from the files and quality from `metrics`. A prediction here would
be strictly worse data than the measurement it stands in for.

**A lower rung is resampled, not re-rendered.** Re-rendering at half
resolution samples the volume differently and gives a slightly *different*
picture — fine as an image, wrong as a rendition, because switching between two
renditions has to be a rate change and not a visible cut.

`write` refuses a ladder a consumer would trip over: renditions must share the
frame count (something switching at frame *n* has to land on frame *n*), rung
names must be unique, and a live clip cannot have rungs because it has no frame
list to offer at another quality.

## Measuring what arrived

```bash
python -m streamer.metrics ~/bundles/basketball          # per scene and rung
python -m streamer.metrics ~/bundles/basketball --per-clip --json
```

`Monitor` says how many bytes moved; this says how good the picture was, which
is the other half of any comparison. It works on **a bundle, not a method**: a
scene's `captured` clip at a station is the reference, and every other clip at
that station is scored against it. Same code, same reference, so two numbers
are comparable — a metric shipped inside each method would be nine metrics.

It reports what it *could not* measure rather than omitting it. Geometry clips
have no pixels until something renders them from the reference's pose; live
clips have no frame list; a depth map is not an attempt to reproduce a
photograph, which a clip says with `detail["depicts"]`. That last one exists
because scoring depth against colour yields 0.4 dB — a number that reads as
catastrophic failure rather than as a comparison that was never meaningful.

SSIM is implemented here rather than imported, so this package still needs only
`open4d`. That shortcut is checked, not trusted: the tests hold it to
scikit-image's implementation, and on a real frame the two agree to 2.4×10⁻⁸.
scipy is used for the blur when present, with a numpy fallback — and the test
that compares the two backends is what caught them disagreeing, since scipy's
`reflect` repeats the edge sample and numpy's does not.

## A free camera for a method that renders on a GPU

Vega ships explicit geometry, so the browser rasterises whatever viewpoint the
mouse asks for. ReRF is also free-viewpoint — that is the paper's title, and
`rerf_render.py --render_360` is the path `rerf_stream.export --orbit` drives —
but its ray-march runs on CUDA, so the pixels are made offline and the browser
receives a set of rendered views rather than something it can re-aim.

There is also a third option, which is the best of them where it applies:
`rerf_stream.geometry` reads the density field out as a coloured point cloud,
and a `points` clip gets the same genuinely free camera Vega does — the browser
rasterises it, nothing is pre-rendered, nothing snaps. It costs about 14 dB
against the ray-march of the same frame, so the bundle carries both and the
comparison is the point.

For the pixel clips that remain, the viewer treats a drag as the same gesture
rather than a different mode, and snaps to the nearest rendered view:

- `shellOf` decides whether a rig can stand in for an orbit at all. Its
  stations have to lie at one radius about a common look-at point — otherwise
  this is an arbitrary cloud of capture positions, and snapping a free camera
  onto it would move the camera somewhere nobody pointed it. Rigs that fail the
  test keep the old behaviour: pixels are compare-only.
- The centre is solved from the poses' own view rays, not taken as the centroid
  of their positions. For one ring those agree, which is why the centroid was
  good enough at first; stack rings at different elevations and the centroid
  sits off the axis and every radius measured from it differs.
- `nearestStation` compares *directions* from the subject via a dot product,
  so azimuth and elevation are handled together, the ±180° seam needs no
  special case, and there is no need to weigh a degree of azimuth against a
  degree of elevation.
- Following is debounced by 140 ms. Crossing a station changes which clip the
  pane plays, so re-resolving per `pointermove` would fetch a container per
  station — a spin across 216 of them is 330 MB of pictures nobody stopped on.
  The geometry panes are unaffected: for them the camera is the only input.

The pane label and the notes state the caveats, because a pane that quietly
shows the nearest view looks like one that followed exactly — and someone
comparing it against a rasterised pane would read the offset as a
reconstruction error. On `g_basketball`, 216 stations: ±2.5° azimuth, 3
elevations (-25°, 0°, 25°).

## Packing a clip into one file

```bash
python -m streamer.sequence ~/bundles/basketball        # every packable clip
python -m streamer.sequence ~/bundles/basketball --clip basketball-vega
```

A clip's frames are separate files, and fetching them costs a round trip each.
On loopback that is free — 30 frames in 87 ms — which is why per-frame fetching
survived so long; over a 20 ms link it is 30 round trips, 600 ms of pure
latency before anything can play.

`streamer.sequence` packs a clip into one `.seq` container: a header naming the
frames and their absolute offsets, then their bytes end to end. Deliberately
not a zip — the frames are already compressed, so an archive's own compression
would spend CPU to save nothing, and a client would need a decoder for it.

The offsets being absolute is what makes it useful in flight: a client with the
first few hundred bytes already knows how many frames are coming, and can slice
any one of them out without walking the ones before it. Measured through
`streamer.server`, the Vega clip is 62.9 MB behind a 265-byte header, and a
`Range: bytes=0-511` request is enough to read the whole frame table.

This is the on-demand shape and it suits a free camera exactly. A player
fetches a few frames ahead and discards them behind the playhead, because it
only ever draws the frame it is showing. A viewer that can be spun around needs
the geometry resident to redraw it from a new angle — so the frame has to stay,
and if it has to stay there was no reason to fetch it late.

Packing removes the frame files it replaced, since a client handed a container
never asks for the pieces; `--keep-frames` leaves both. Live clips are skipped
rather than refused — there is no end to pack — and so are clips already packed,
so the command is safe to re-run.

## Adopting frames from another interpreter

```bash
python -m streamer.adopt ~/rerf-clips --bundle ~/bundles/basketball
```

Some methods cannot be driven from this package at all: ReRF's entropy coder is
a prebuilt Python 3.8 binary, and this needs 3.10, so neither side can import
the other. Rather than force one interpreter on both, the exporting side writes
its frames plus a small `clips.json`, and `adopt` reads that, copies the frames
in and adds the clips. Every frame the sidecar names is checked to exist first,
because a clip whose frames are half there plays for two seconds and then 404s
while the manifest insists nothing is wrong.

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

## Choosing a rung, and a link to choose against

```bash
python -m streamer.metrics ~/bundles/basketball --write   # fill in what rungs buy
python -m streamer.policy  ~/bundles/basketball --scene thomas
```

```
   budget    spent    mean  cam00    cam01    cam02    cam03    cam04    cam05
     6.4M     6.4M   40.47  low      low      low      low      low      low
    38.8M    34.0M   44.43  medium   default  medium   medium   medium   medium
    71.3M    71.3M   46.16  default  default  default  default  default  default
```

The choice is a **multiple-choice knapsack**, solved exactly by dynamic
programming. Greedy "best quality per byte" is the usual approximation and it
is wrong in a way that matters here: with rungs 3.2× and 10.6× apart it spends
everything on the first pane it considers and leaves the rest at their floor,
which is precisely the lopsided allocation a comparison view must not have.
Nine clips by three rungs makes exactness free.

Weights decide who gets headroom, and who is dropped when even the floor does
not fit — there is no rung cheaper than the cheapest, so something has to go,
and it is named rather than silently missing. `switch_penalty` charges churn
against a previous selection.

Rate and quality are **measured, not predicted**, which is what removes the
trained regressors a system choosing before encoding would need.
`metrics --write` puts the quality half into the manifest beside the bytes.

### The link

```python
from streamer import Link, Trace, serve

serve(bundle_dir, link=Link(capacity=20e6, latency=0.020))
serve(bundle_dir, link=Link(trace=Trace.read("walk.trace")))
```

Until this existed every transport here ran on loopback, which made the budget
`policy` needs unmeasurable and any throughput figure a statement about the
disk. `Link` is a single-queue bottleneck: bytes leave in request order, at the
capacity in force, after a propagation delay, and queueing emerges from
contention — which is the behaviour that matters when panes share a pipe. It is
shared by every connection on purpose.

Shaped in the server rather than with `tc`. Kernel shaping is more faithful and
was rejected because it needs root, perturbs the whole machine, and cannot be
exercised by a test — which for a measurement instrument is disqualifying.
Measured accuracy: **2.6% at 5 Mbit/s, 7.4% at 20, 15% at 50**, always
*under*-delivering, and latency within 1 ms. What it does not reproduce is TCP:
no congestion window, no slow start. Loss is charged as the delay a
retransmission costs, because that is what an application above TCP sees.

### The viewer adapts

The page measures the link from its own fetches (`RateMeter`, an exponentially
weighted mean over recent transfers, byte-gated so a cache hit cannot read as a
gigabit link), reads each clip's ladder, and picks a rung per pane. There is a
checkbox to pin every pane to its default rendition, and the stats panel shows
the measured rate, the per-pane share, and which rung each pane is on.

**The client's rule is deliberately not `policy`'s.** `policy` maximises total
weighted quality, and the way to maximise a sum is to make the panes *unequal*
— spending on whichever gains most per byte. A viewer whose purpose is judging
two methods against each other must not decide that one of them gets the
bandwidth. So the client splits the budget evenly and each pane takes the best
rung its share affords. `policy` remains what answers "what would an optimising
client do", from Python, where it can be measured — and not transcribing it
also means there is no second implementation of it to drift.

Switching rung keeps the cache. Renditions share a timeline, so a decoded frame
is still the right picture for its index; dropping the cache would re-fetch what
is already in hand and stall at exactly the moment the link is under pressure.

Tracking a trace end to end, through a link that steps 25 → 3 → 12 Mbit/s:

```
    t      ewma    share  rung mix
    0      7.0M    0.87M  low=8
   26      9.9M    1.23M  low=6 medium=2
   33      6.2M    0.78M  low=7  dropped 1
   42      3.4M    0.43M  low=4  dropped 4
   51      6.1M    0.76M  low=7  dropped 1
```

The estimate lags the cliff by a few seconds, which is what a half-life of
eight samples means and is the interesting part of an adaptive client rather
than a defect. It tops out near 10 Mbit/s on a 25 Mbit/s link because serial
requests at 15 ms each cannot fill it — the honest budget for that request
pattern.

`link.observed()` reports what actually arrived, split into the rate while it
was *delivering* and how much of the time it was busy at all. That split is not
cosmetic: dividing bytes by wall clock reported **0.3 Mbit/s for a 25 Mbit/s
pipe** when the client fetched a few frames and then waited, and a chooser
handed that drops every pane. It is also **cumulative since the link started**,
so it summarises a run rather than reading a current rate — a client tracking a
changing link needs the recent-window estimate the page uses instead.

## What is deliberately missing

**A buffer model.** Per-clip buffer occupancy, a deliberate freeze
told apart from a stall, churn charged across segments. `policy` allocates for
a budget at an instant; none of it plays continuously against a trace and
reacts. `switch_penalty` is the only part of that dynamics testable today, and
the rest needs a client that streams for minutes rather than a function that
returns a selection.

**A held-out camera.** Every rig camera was a training view for both Vega and
ReRF, so the numbers `metrics` reports measure reconstruction rather than
generalisation. That caveat has to travel with them.

## Tests

```bash
python -m pytest streamer_tests -q
```

No GPU, no display, no dataset. The client's frame parsers are run as shipped,
cut out of `viewer.html` and executed under Node — those cases skip if `node` is
absent, which is stated rather than silently covering less.
