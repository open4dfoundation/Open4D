# Python API

The public API loads, saves, unloads, and visualizes whole finite triangle-mesh
sequences independently of their storage format:

```python
import open4d

with open4d.load("capture.usdc") as sequence:
    open4d.save(sequence, "capture-copy.usdc")
    open4d.visualize(sequence)

# A path can go straight to the lazy viewer; it is closed when the window exits.
open4d.visualize("capture-copy.usdc")
```

`.usd`, `.usda`, `.usdc`, and `.usdz` are OpenUSD interchange containers.
The registered codec suffixes identify codec artifacts. Both carry whole
sequences and use the same `Sequence` interface. `open4d.unload(sequence)` is an
explicit, idempotent alternative to the context manager.

Frame folders and individual meshes remain supported as import paths; frames
are decoded on access:

```python
from open4d.io import open_sequence

with open_sequence("path/to/frames", fps=30.0) as sequence:
    print(len(sequence), sequence.duration, sequence.fps)
    mesh = sequence[0].geometry          # TriangleMesh: positions, triangles
```

## Representations

What a decoded frame *is* is deliberately separate from the codec that produced
it: a triangle mesh is a mesh whether it arrived as OBJ, as a Draco payload, or
out of a V-DMC bitstream. `open4d.Representation` is the axis to gate on, and
`MESH`, `POINTS`, and `GAUSSIANS` have concrete types — `TriangleMesh`,
`PointCloud`, and `GaussianCloud`. Already-rendered pixels are named in the
taxonomy but have no concrete type yet; they land with the camera model they
need in order to be comparable at a known pose.

The containers and codecs described below are the mesh path, and the one most
completely covered. Check a specific codec before assuming it round-trips
points or Gaussians.

## Writing sequences

`write_sequence(sequence, "frames/", format="ply")` writes a versioned
`open4d.sequence.json` beside the frame files, so reopening the directory keeps
source frame indices, timestamps, frame/sequence metadata, and topology
declarations. Empty sequences are rejected before the destination is changed.

Single mesh-file exports require `allow_lossy=True` because that storage cannot
preserve sequence timing, metadata, or topology declarations. Trimesh-backed
OFF/GLB/glTF color export also requires that opt-in because OFF drops vertex
color and GLB/glTF quantize canonical float colors to eight bits.
STL also requires `allow_lossy=True`: it discards unused vertices and vertex
identity, so exported manifests clear correspondence and topology guarantees.
STL/GLB/glTF reject geometry without triangles. Gaussian clouds cannot be
exported through these mesh writers, and USD writing currently accepts only
triangle meshes.

OpenUSD is the public interchange container. `--pack-usd out.usdc` packs any
source into one compressed `.usdc` file carrying the frame rate, the key-frame
index, and per-frame streams alongside the geometry — see the
[visualization guide](../examples/visualization/README.md#the-openusd-container).

## Streaming

`open4d.stream` exports a file as a bundle and serves it to a browser:

```python
import open4d

open4d.stream("capture.usdc")
open4d.stream("capture.usdc", rungs=["draco", "draco@11"], out_dir="bundle/")
```

For a loaded sequence, pass browser options such as `name="capture"` and
`out_dir="bundle/"`. `open4d.send(sequence, host, port)` explicitly selects
decoded-mesh TCP transport and pairs with `open4d.receive`. Existing
`open4d.stream(sequence, host, port)` calls, and frame iterables without browser
options, retain that TCP behavior without requiring `open4d-streamer`.
The TCP default is `127.0.0.1:47004`; pass port 7000 explicitly when talking to
an older Open4D receiver.

`rungs` is the quality ladder. The first is the rendition a client plays by
default and the rest are what it can switch to mid-playback, so a one-entry
list is a fixed-quality stream and says so. A spec is a frame format —
`ply` for interchange, `draco` for delivery — optionally with a position
quantisation, as in `draco@11`. Sizes are measured off disk rather than
predicted; quality is left unscored until something scores it.

[`examples/streaming_demo.py`](../examples/streaming_demo.py) runs the whole
of it on the ten basketball frames the TVMC codec vendors: three rungs
built and scored, served over HTTP with the counters read back, then thirty
seconds simulated over a link that collapses mid-run.

The implementation is the separate `open4d-streamer` package, imported on the
call rather than at load: it depends on `open4d`, so `open4d` must not depend
on it. Without it installed the call raises `open4d.StreamerDependencyError`
saying how to install it. For several clips in one bundle, a constrained link,
or the delivered-quality metrics, use that package directly —
`streamer.Bundle`, `streamer.Link`, `streamer.serve`.

Separately, [`open4d/webclients`](../open4d/webclients) is the vendored
browser-client research tree that compares five delivery systems against each
other. It is not this API and shares no code with it.

## Codecs

TVMC and TSMC can encode directly to a single `.vmesh` file:

```python
open4d.encode("capture.usdc", "capture.vmesh", codec="tvmc")
with open4d.load("capture.vmesh") as sequence:
    open4d.save(sequence, "decoded.usdc")
```

The embedded codec is detected on load. The experimental O4D V3C application
format preserves native compressed payloads and timing; existing `.tvmc` and
`.tsmc` directories remain supported. See the [native `.vmesh` notebook](../examples/vmesh/01_container_and_usdc.ipynb)
for inspection, packing, extraction, and an exact compressed-state USDC round trip.
The [mesh example](../examples/vmesh/02_mesh_codecs.ipynb) demonstrates encoding
and saving decoded geometry as USDC.

Vega, QUEEN, 3DGStream and ReRF also support native `.vmesh` output. Import a
research run with `open4d.import_native(run, codec="queen", config=...)`, or
use `open4d.encode(run, "capture.vmesh", codec="queen", config=...)`.
Loading returns a `NativeSequence`; `native.decode(runtime=..., python=...)`
evaluates the actual temporal models. Saving it to USDC preserves the exact
compressed representation using O4D's native USD schema. This route does not
train a neural scene from an arbitrary mesh USD. See the linked guide for
required model/configuration files and representation-specific outputs.

N4MC also accepts `.vmesh` output from mesh sequences or mesh USDC using
`open4d.encode(..., codec="n4mc")`. Loading reconstructs a mesh `Sequence`.
`open4d.codec.pack_vmesh("old.n4d", "new.vmesh")` migrates existing O4D N4MC
artifacts without retraining. Its profile explicitly identifies independent
quantized TSDF latents sharing one model; it does not claim temporal prediction.
The guide also documents exact compressed-state preservation in native USDC.

`available_codecs()` lists the public research adapters: `klt`, `n4mc`, `qndf`,
`qndf-int8`, `vdmc`, `faster_vdmc`, `tvmc`, `tsmc`, `vega`, `queen`,
`3dgstream`, and `rerf`. The NumPy
reference codecs and temporal experiments are internal benchmark baselines;
`npz` and `.o4d` are not supported public save defaults. Use USD for interchange
or select a registered codec and its suffix.

The lightweight wheel includes the adapters but excludes their research
implementations. KLT, N4MC and QNDF need a source checkout and their optional
dependencies; set `OPEN4D_RESEARCH_ROOT` to that checkout when using an installed
wheel. Open4D's separate `temporal-delta` and `temporal-pca` experiments are
not the repository's TVMC or TSMC pipelines. The V-DMC adapters do not execute
shell scripts, but they do invoke configured native encoder and decoder
processes once per sequence. Callers can also register another
`open4d.codec.Codec`.
Native mesh processes default to a one-hour timeout; set
`OPEN4D_NATIVE_TIMEOUT` to a positive number of seconds for longer jobs.

For an all-registered-codec attempt using `4d_files/Rafa_Approves_hd_4k`, open
[`examples/open4d_sequence_codec.ipynb`](../examples/open4d_sequence_codec.ipynb).
Set `OPEN4D_NOTEBOOK_REQUIRE_ALL=1` in a fully provisioned environment to make
any codec failure stop the notebook instead of appearing only in its result
table.

### Device selection

The N4MC and QNDF adapters accept `device="auto"` (CUDA, then Apple Metal/MPS,
then CPU), or an explicit `"cuda"`, `"mps"`, or `"cpu"`. QNDF-int8 can train on
CUDA or Metal, but its quantized decoder remains CPU-only. Override the notebook
selection with `OPEN4D_NOTEBOOK_DEVICE=mps` when needed.
KLT decoding also accepts `device="auto"`.

### Test coverage

Normal CI runs dependency-complete CPU encode/fresh-decode contracts for KLT,
N4MC, QNDF, and QNDF-int8. The larger two-format Rafa quality/export matrix is
an additional CUDA acceptance test gated by `OPEN4D_TEST_RESEARCH_CODECS=1` and
`OPEN4D_RAFA_DATASET`; it is not presented as part of ordinary CI coverage.
