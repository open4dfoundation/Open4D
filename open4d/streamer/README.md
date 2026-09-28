# streamer

Serves 4D reconstructions (meshes, point clouds, Gaussians or rendered pixels)
to a browser for playback. [`../reconstruction`](../reconstruction) makes the content and
this package delivers it. It imports none of the methods that produce it.

[`study/`](study/README.md) next to this package is a separate browser user study,
not part of the pip package.

## Install

```bash
pip install -e open4d/streamer
```

`open4d.stream()` uses this package. Without it, `open4d.stream` raises
`StreamerDependencyError`.

## Quick start

```python
import open4d
from streamer import Bundle, serve

with open4d.load("capture.usdc") as sequence:
    with Bundle("out/", title="Capture") as clips:
        clips.add(sequence, name="capture", rungs=["draco", "draco@11"])

serve("out/")        # open the printed URL
```

For one sequence, `open4d.stream("capture.usdc")` does all of this.

A **bundle** is a directory of frames plus a `view.json` index. The index is
written only if the `with` block finishes without an error. Each **rung** is
the same clip at another quality. The first rung is the default.

## Modules

| Module | Does |
|---|---|
| `export` | `from_sequence` / `from_source`: anything `open4d.load` reads becomes a clip |
| `bundle`, `session` | the `view.json` format; `Bundle` collects clips into one index |
| `codecs`, `representations` | the wire formats and what they decode to |
| `server` | `serve()`: static HTTP/1.1 file server, counters at `/stats.json` |
| `transfer` | `fetch()`: download a bundle; interrupted downloads resume |
| `live` | `live.mjpeg()`: server-rendered clips (e.g. ReRF, Vega live) |
| `link` | `Link` / `Trace`: simulated bandwidth and latency, applied inside the server |
| `metrics` | PSNR/SSIM of each clip against the captured reference |
| `policy` | picks a rung per clip for a bandwidth budget |
| `sequence` | packs a clip's frames into one `.seq` file |
| `adopt` | imports frames exported from another Python (e.g. ReRF on 3.8) |
| `client/` | the browser player; decoding runs in `client/worker.js` |

## Formats

The codec depends on both the representation and the suffix, since `.ply` can
hold either a mesh or 3DGS.

| Representation | Suffix | Codec | Decoded in |
|---|---|---|---|
| mesh | `.ply` / `.drc` | `mesh-ply` / `mesh-draco` | browser |
| points | `.ply` / `.drc` | `points-ply` / `points-draco` | browser |
| gaussians | `.ply` / `.splat` | `3dgs-ply` / `splat` | browser |
| pixels | `.jpg` / `.png` | `jpeg` / `png` | browser (fixed view) |
| pixels | `.rerf` | `rerf` | server |

Draco cuts a mesh frame from 761 kB to 59 kB (12.9×) at 14-bit positions.
It needs `open4d[draco]` to encode; the browser needs nothing installed.

```python
from streamer import from_source
from_source("captures/basketball_player", "out/", fps=10, frame_format="draco")
```

To add a codec, register it and add a parser to the client's `CODECS` table.
A test checks that the two lists match:

```python
codecs.register(codecs.CodecSpec(
    name="mesh-vdmc", suffix=".v4d", representation=Representation.MESH,
    lossy=True, cost="V-DMC is lossy at any rate worth using",
))
```

Lossy codecs must state their `cost`.

## Serving, fetching, shaping

```python
from streamer import serve, fetch, Link, Trace

server = serve("bundle/", host="0.0.0.0")      # no authentication
fetch("http://gpu-box:8770", "local-copy/")    # resumes partial files
server.monitor.snapshot()                      # requests, bytes, errors

serve("bundle/", link=Link(capacity=20e6, latency=0.020))
serve("bundle/", link=Link(trace=Trace.read("walk.trace")))
```

`serve` binds to loopback unless you pass `host`. `Link` shapes traffic in the
server and needs no root. Measured, it delivers 2.6–15% below its target
capacity, with latency within 1 ms.
It does not model TCP congestion control.

The browser measures its own throughput and splits it evenly across panes.
Each pane plays the best rung its share affords.

## Command line

```bash
python -m streamer.metrics  bundle/ [--per-clip] [--json] [--write]
python -m streamer.policy   bundle/ --scene thomas
python -m streamer.sequence bundle/ [--clip NAME] [--keep-frames]
python -m streamer.adopt    exported/ --bundle bundle/ [--replace]
```

## Live clips

```python
from streamer import bundle, live, serve
clip = live.mjpeg("http://127.0.0.1:8800/stream", name="vega-live",
                  origin="rendered", scene="Vega live")
bundle.write("out/", title="live", source="vega", clips=[clip])
serve("out/", port=8770)
```

`origin` is required: `rendered` means frames are drawn on demand, `replay`
means pre-rendered frames on a loop. For the Vega source, run
`orbitvega.wall_demo` from `open4d/reconstruction/vega`.

## Limits

- No buffer model: `policy` picks rungs for one moment in time and does not
  simulate playback over a trace.
- Every rig camera was a training view for Vega and ReRF, so `metrics` measures
  reconstruction quality, not how well a method generalizes to new views.
- Every exporter writes independent frames. Seeking in `gop` and `sequential`
  clips is tested but no real content uses it yet.

## Tests

```bash
cd open4d/streamer && python -m pytest -q
```

No GPU, display or dataset needed. The client parser tests need `node` and are
skipped without it.
