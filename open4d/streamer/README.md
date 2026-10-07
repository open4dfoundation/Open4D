# streamer

Serves 4D reconstructions (meshes, point clouds, Gaussians or rendered pixels)
to a browser for playback.

[`study/`](study/README.md) is a separate browser user study, excluded from the
pip package.

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

serve("out/")
```

For one sequence, use `open4d.stream("capture.usdc")` and open the printed URL.

A **bundle** is a directory of frames plus a `view.json` index. The index is
written only if the `with` block finishes without an error. Each **rung** is
the same clip at another quality. The first rung is the default.

A rung is a frame format (`ply`, `draco`, `draco@11`), or one of Open4D's mesh
codecs in front of one (`klt`, `tsmc/draco@11`). Codec rungs are encoded and
decoded on the server; clients receive frame payloads. `bytes` records served
bytes and `detail.codec_bytes` the native bitstream size. Gaussian
clips take `ply` or `splat`, and `splat@25%` keeps the 25% most significant
Gaussians of each frame, ranked by opacity times volume^(2/3).

`add(..., score=True)` scores each rung against the source sequence with
`open4d.compare_sequences` and stores `point_psnr`, `point_rms` and
`hausdorff`. Pass
`metric="point_psnr"` to `policy.choose` and `playback.Playback`. It works on
meshes and point clouds; Gaussian clips use `metrics` against a rendered
reference instead.

`add` also takes Open4D's Gaussian sequences: a QUEEN/3DGStream
`GaussianRun`, a Gaussian `NativeSequence`, Vega's decoded frames (colour
baked from one direction), or a list of `GaussianSplats`.

## Modules

| Module | Does |
|---|---|
| `export` | `from_sequence` / `from_source`: anything `open4d.load` reads becomes a clip |
| `gaussians` | `from_frames`: Open4D's Gaussian sequences as 3DGS PLY or `.splat` clips |
| `score` | geometric quality of each rung, via `open4d.compare_sequences` |
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

Draco encoding requires `DracoPy`; decoding runs in the browser.

```python
from streamer import from_source
from_source("captures/basketball_player", "out/", fps=10, frame_format="draco")
```

To add a codec, register it and add a parser to the client's `CODECS` table.
A test checks that the two lists match:

```python
codecs.register(codecs.CodecSpec(
    name="mesh-vdmc", suffix=".v4d", representation=Representation.MESH,
    lossy=True, cost="Lossy geometry compression",
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
server and needs no root. It does not model TCP congestion control.

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
