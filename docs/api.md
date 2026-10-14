# Python API

The public API loads, saves, encodes, decodes, reconstructs, streams and
visualizes finite geometry sequences. The NumPy-only core is separate from
installed research plugins and their independently acquired backends.

This example needs no downloaded dataset or native codec:

```python
import open4d as o4d

with o4d.data.SyntheticMeshSequence().load() as source:
    o4d.encode(source, "sample.o4d", codec="npz")
with o4d.decode("sample.o4d") as restored:
    print(restored.frame(0).geometry.positions.shape)
```

OpenUSD export needs the optional `usd-core` package:

```python
import open4d

with open4d.load("capture.usdc") as sequence:
    open4d.save(sequence, "capture-copy.usdc")
    open4d.visualize(sequence)

# A path can go straight to the lazy viewer; it is closed when the window exits.
open4d.visualize("capture-copy.usdc")
```

`.usd`, `.usda`, `.usdc`, and `.usdz` are OpenUSD interchange containers.
All registered compression methods use `.o4d`; its descriptor identifies the
codec. Mesh profiles expose `Sequence`, while native Gaussian and field profiles
expose `NativeSequence`. `open4d.unload(sequence)` is an
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

`open4d.Representation` identifies the decoded frame type independently of its
codec: `MESH`, `POINTS`, and `GAUSSIANS` correspond to `TriangleMesh`,
`PointCloud`, and `GaussianCloud`. Rendered pixels have no concrete type yet;
comparison requires their camera model.

The NumPy profiles accept meshes, point clouds and GaussianCloud sequences.
Research mesh codecs accept triangle meshes. Native Gaussian and field methods require
their own trained states or calibrated reconstruction inputs; a mesh file alone
does not provide those inputs.

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
exported through these mesh writers. Ordinary USD geometry writing accepts
triangle meshes; compressed O4D USD interchange is described below.

### OpenUSD sequence layout

OpenUSD is the public interchange container. `open4d.save(sequence,
"capture.usdc")` writes one `UsdGeom.Mesh` prim at `/Open4D/Sequence`; any
ordinary USD reader sees an animated mesh. Frame `n` (zero-based) is stored at
time code `n`, the stage's time codes and frames per second are the sequence
frame rate (30 when it has none), and interpolation is held. `up_axis="y"` or
`"z"` sets the stage up axis. The prim carries:

| Attribute | Contents |
| --- | --- |
| `points`, `extent` | Positions and bounds per frame |
| `faceVertexCounts`, `faceVertexIndices` | Triangles, written only when the topology changes |
| `primvars:displayColor`, `primvars:displayOpacity` | Vertex RGB and alpha, when present |
| `normals` | Vertex normals, when present |
| `open4d:vertexUV`, `open4d:cornerUV` | Vertex or per-corner texture coordinates |
| `open4d:frameIndex`, `open4d:timestamp` | The source frame index and timestamp in seconds |
| `open4d:frameDescriptor` | JSON frame metadata and the names of custom attributes |
| `open4d:attributeNNNN` | Custom per-vertex attributes as float, int or bool arrays |

The root layer's `customLayerData["open4d"]` records the
`open4d.usd-sequence/v1` schema and a JSON manifest with the frame rate, up
axis, key-frame indices, sequence metadata and topology declarations, so
`open4d.load` restores exact timestamps and frame indices. Other USD files load
one time-sampled geometry prim; select it with
`options={"prim_path": "/World/Mesh"}`.
Compressed `.o4d` artifacts use a separate custom prim, described under
[USD interchange](#usd-interchange).

## Reconstruction

Prepared RGB-D data can be reconstructed and encoded in one call:

```python
import open4d as o4d

capture = o4d.RGBDCapture.from_folder("capture/", "calibration/")
o4d.reconstruct(capture, "reconstructed.o4d", method="tsdf", codec="npz")
with o4d.decode("reconstructed.o4d") as meshes:
    o4d.save(meshes, "reconstructed.usdc")  # needs open4d[usd]
```

`rgbd` is the canonical built-in method; `tsdf` is its alias. Without an output
path, it returns a lazy `Sequence`. Reconstruction needs Open3D 0.19.x and
already aligned, undistorted, calibrated and synchronized depth/RGB/poses.
Capture loading and hardware acquisition remain separate from reconstruction.
The destination and codec are validated before expensive RGB-D processing.

`MultiviewCapture.from_colmap(model, images, scale_to_meters=...)` reads text
COLMAP `PINHOLE` or `SIMPLE_PINHOLE` calibrations. It requires an explicit scale
because COLMAP does not determine metres, and rejects distorted camera models.
The container is for plugins that explicitly support calibrated multiview
inputs; native QUEEN expects its own prepared scene directory.

`available_methods()` lists the built-in `rgbd` and installed reconstruction
providers. After separately installing the QUEEN adapter and compatible runtime:

```python
run = o4d.reconstruct("prepared-scene/", "run/", method="queen")
o4d.encode(run, "scene.o4d", codec="queen")
# Or train into an owned temporary directory and package the native result:
o4d.reconstruct("prepared-scene/", "scene.o4d", method="queen", codec="queen")
```

This carries the actual backend model state, not a mesh automatically converted
to Gaussians. Neural runtime, configuration and initial-model dependencies keep
their own requirements. GPU reconstruction is not validated by the core CPU
tests or this example.

## Streaming

`open4d.stream` exports a file as a bundle and serves it to a browser:

```python
import open4d

open4d.stream("capture.usdc")
open4d.stream("capture.usdc", rungs=["draco", "draco@11", "klt/draco"],
              score=True, out_dir="bundle/")
open4d.stream(gaussian_run)          # QUEEN / 3DGStream run, Gaussian .o4d, or splats
```

Accepted inputs are mesh or point-cloud `Sequence` values, `GaussianRun`,
Gaussian `NativeSequence`, decoded Vega frames, lists of `GaussianSplats`,
and paths supported by `open4d.load`. Single frames and ReRF fields raise
`TypeError` before writing output.

For a loaded sequence, pass browser options such as `name="capture"` and
`out_dir="bundle/"`. `open4d.send(sequence, host, port)` explicitly selects
decoded-mesh TCP transport and pairs with `open4d.receive`. Existing
`open4d.stream(sequence, host, port)` calls, and frame iterables without browser
options, retain that TCP behavior without requiring `open4d-streamer`.
The TCP default is `127.0.0.1:47004`; pass port 7000 explicitly when talking to
an older Open4D receiver.

### Recording, stopping and measuring a TCP stream

A receiver accepts one sender. `receiver.record(max_frames=None,
duration=None)` collects frames into an in-memory `Sequence` until the sender
ends the stream, `max_frames` frames arrive, or a frame's timestamp is
`duration` seconds or more after the first recorded frame (stream time, not
wall time). A frame that ends the duration window is kept for the next `next()`
or `record()` call, and stopping at a limit leaves the receiver open. Save the
result with `open4d.save(recording, "capture.usdc")` or encode it to `.o4d`.

`receiver.close()` may be called from another thread: a waiting `next()` stops
with `StopIteration` and a waiting `record()` returns the frames it has. If the
receiver closes first, `send` raises `ConnectionError`. A sender that
disconnects without ending the stream raises `EOFError` on the receiver, and
`timeout` bounds every wait.

`receiver.stats` returns an `open4d.transport.StreamStats` snapshot: `frames`,
`payload_bytes` (array bytes), `wire_bytes` (headers and arrays), `elapsed`
(seconds from the first frame's arrival to the latest frame), `fps` and
`bits_per_second`. The rates are `None` until two frames have arrived.

`rungs` defines the quality ladder; the first rung is the default, and a
single rung gives fixed-quality playback. Each spec selects a frame format
(`ply` or `draco`), optionally with a position
quantisation, as in `draco@11`. It can also start with one of Open4D's mesh
codecs: `klt` or `tsmc/draco@11` encodes with that codec, decodes on the server
and serves the decoded frames. Gaussian
rungs are `ply` (the default for Gaussians) or `splat`, optionally keeping only
the most significant share of each frame's Gaussians, as in `splat@25%`.
Without `rungs`, meshes and points get `draco`.

Sizes are measured from the output files. With `score=True`, every
mesh or point-cloud rung is scored against the source with
`open4d.compare_sequences` and saved as `point_psnr`, which
`streamer.policy` and `streamer.playback` can rank by
(`metric="point_psnr"`). `link=streamer.Link(...)` shapes what the server
sends and `monitor=streamer.Monitor()` counts it.

[`examples/streaming_demo.py`](../examples/streaming_demo.py) builds and scores
three rungs from the generated wave sequence, serves them over HTTP, and
simulates playback over a changing link.

Browser streaming requires the separate `open4d-streamer` package, imported
when called. If it is missing, `open4d.StreamerDependencyError` gives installation
instructions. For several clips in one bundle, budgets and
simulated playback, use that package directly — `streamer.Bundle`,
`streamer.policy`, `streamer.playback`.

Separately, [`open4d/streamer/study`](../open4d/streamer/study) is the vendored
browser-client research tree for comparing delivery systems.

## Codecs

Every public codec encodes to a single `.o4d` file:

```python
open4d.encode("capture.usdc", "capture.o4d", codec="tvmc")
with open4d.load("capture.o4d") as sequence:
    open4d.save(sequence, "decoded.usdc")
```

The embedded codec is detected on load. The standalone [O4D format](#o4d-format)
preserves native compressed payloads and timing. See the [native `.o4d` notebook](../examples/o4d/01_container_and_usdc.ipynb)
for inspection, packing, extraction, and an exact compressed-state USDC round trip.
The [mesh example](../examples/o4d/02_mesh_codecs.ipynb) demonstrates encoding
and saving decoded geometry as USDC.

Vega, QUEEN, 3DGStream and ReRF carry native compressed models. Import a
research run with `open4d.import_native(run, codec="queen", config=...)`, or
use `open4d.encode(run, "capture.o4d", codec="queen", config=...)`.
Loading returns a `NativeSequence`; `native.decode(runtime=..., python=...)`
evaluates the actual temporal models. Saving it to USDC preserves the exact
compressed representation using the custom `O4D` USD prim. See the linked guide
for required model/configuration files and representation-specific outputs.

KLT, N4MC, QNDF and QNDF-int8 accept mesh sequences or mesh USDC, for example
`open4d.encode(..., codec="n4mc")`. Loading reconstructs a mesh `Sequence`.
N4MC carries independent quantized TSDF latents sharing one model. KLT carries a
shared transform basis and compressed frame coefficients. QNDF carries coarse meshes and independent
displacement models; QNDF-int8 uses quantized model weights.

`available_codecs()` lists core NumPy profiles and installed plugins. Install a
research adapter from this checkout, for example `pip install ./plugins/klt`
or `pip install ./plugins/vdmc`; `pip install ./plugins/research` registers all
research adapters. These adapter packages contain MIT registration glue, not
research implementations, native binaries, datasets or weights, and are not
claimed to be published on PyPI. The backends retain their original terms.
No-license and no-redistribution components remain excluded from artifacts.

Third-party packages register codec objects or zero-argument factories through
the `open4d.codecs` entry point group. Reconstruction functions use
`open4d.reconstruction`. Discovery is lazy; inspecting availability does not
execute a plugin. Invalid, duplicate or unavailable plugins produce diagnostic
errors when selected rather than silently overriding core codecs.
Lazy providers report an unknown `plugin` representation until selected.
Use `open4d.codec.codec_info("klt", resolve=True)` to load only that provider
and inspect its actual representation and preserved attributes. The browser
streamer uses this selection step for codec rungs, including core NumPy profiles.
Entry points extend implementations of the reviewed O4D profiles in the core.
A new native wire layout also needs a versioned profile and validation in the
core; arbitrary plugin-defined extraction schemas are deliberately rejected.

The core `npz`, `raw`, `deflate`, `bzip2`, `lzma` and `rle` profiles preserve
TriangleMesh, PointCloud and GaussianCloud frame arrays. Research codecs still
require their declared representation. KLT, N4MC and QNDF need their external
source and optional dependencies; set `OPEN4D_KLT_ROOT`, `OPEN4D_N4MC_ROOT` or
`OPEN4D_QNDF_ROOT` to the corresponding source directory, or use
`OPEN4D_RESEARCH_ROOT` for compatibility with a full checkout. The V-DMC adapters do not execute
shell scripts, but they do invoke configured native encoder and decoder
processes once per sequence. Callers can also register another
`open4d.codec.Codec`.
Native mesh processes run without a time limit by default; set
`OPEN4D_NATIVE_TIMEOUT` to a positive number of seconds to bound them.

[`examples/open4d_sequence_codec.ipynb`](../examples/open4d_sequence_codec.ipynb)
walks through encoding, decoding, reconstruction and streaming one call at a
time.

### Backend locations

Adapters find their native programs and research sources through keyword
arguments first, then environment variables. In a source checkout the default
locations are the codec and reconstruction trees.

| Variable | Used by | Default |
| --- | --- | --- |
| `OPEN4D_RESEARCH_ROOT` | KLT, N4MC, QNDF, QNDF-int8 | This checkout, if present |
| `OPEN4D_VDMC_ENCODER`, `OPEN4D_VDMC_DECODER` | `vdmc` (`encoder=`, `decoder=`) | `build/Release/bin/encode` and `decode` under `OPEN4D_VDMC_ROOT` |
| `OPEN4D_FASTER_VDMC_ENCODER`, `OPEN4D_FASTER_VDMC_DECODER` | `faster_vdmc` | The same under `OPEN4D_FASTER_VDMC_ROOT` |
| `OPEN4D_VDMC_ROOT`, `OPEN4D_FASTER_VDMC_ROOT` | V-DMC built from source | `codecs/vdmc`, `codecs/faster_vdmc` |
| `OPEN4D_VDMC_DECODER_CONFIG`, `OPEN4D_FASTER_VDMC_DECODER_CONFIG` | V-DMC decoding (`decoder_config=`) | The carried `decoder.cfg` |
| `OPEN4D_TVMC_ROOT`, `OPEN4D_TSMC_ROOT` | TVMC, TSMC (`backend=`) | `codecs/tvmc`, `codecs/tsmc` |
| `OPEN4D_TVMC_PYTHON`, `OPEN4D_TSMC_PYTHON` | TVMC, TSMC (`python=`) | The backend's `.venv`, else this interpreter |
| `DRACO_ENCODER`, `DRACO_DECODER` | TVMC, TSMC | The backend's `draco/build` |
| `OPEN4D_GS_ROOT` | QUEEN and 3DGStream reconstruction (`runtime=`) | `codecs/gs_tools` |
| `OPEN4D_QUEEN_PYTHON`, `OPEN4D_3DGSTREAM_PYTHON`, `OPEN4D_VEGA_PYTHON` | Reconstruction and Vega encoding (`python=`) | This interpreter |
| `OPEN4D_VEGA_ROOT` | Vega encoding and decoding (`runtime=`) | `codecs/vega` |
| `OPEN4D_QUEEN_ROOT`, `OPEN4D_3DGSTREAM_ROOT`, `OPEN4D_RERF_ROOT` | `NativeSequence.decode` (`runtime=`); `OPEN4D_3DGSTREAM_ROOT` also holds 3DGStream's NTC checkpoint (`ntc=`) | `codecs/<codec>` |
| `OPEN4D_<CODEC>_PYTHON` | `NativeSequence.decode` (`python=`) | This interpreter |
| `OPEN4D_NATIVE_TIMEOUT` | Native mesh encoder and decoder processes | No limit |

An installed core or adapter wheel contains none of these trees, so every research codec needs
its variable or keyword argument there.

### Device selection

The N4MC and QNDF adapters accept `device="auto"` (CUDA, then Apple Metal/MPS,
then CPU), or an explicit `"cuda"`, `"mps"`, or `"cpu"`. QNDF-int8 can train on
CUDA or Metal, but its quantized decoder remains CPU-only.
KLT decoding also accepts `device="auto"`.

### Test coverage

Normal CI runs dependency-complete CPU encode/fresh-decode contracts for KLT,
N4MC, QNDF, and QNDF-int8. The larger Rafa quality/export matrix is
an additional CUDA acceptance test gated by `OPEN4D_TEST_RESEARCH_CODECS=1` and
`OPEN4D_RAFA_DATASET`.

## Comparing sequences

For visual comparison, `open4d.visualize(decoded, baseline=source)` opens two
panes with linked cameras and frame playback. Explicit RGB(A) texture arrays
use the mesh's vertex or corner UVs; pass `texture=` and `baseline_texture=`.
The caller supplies textures and does not lose them through hidden file lookup.
OBJ loading preserves vertex or per-corner `vt` coordinates, including seams
and negative indices. Material files and texture images are not fetched;
load the image explicitly and pass its array to the viewer. Geometry-only OBJ
readers retain their existing three-array return value.

For an explicit Linux X11 EGL setup, start a fresh Python process with
`QT_QPA_PLATFORM=xcb`, `QT_XCB_GL_INTEGRATION=xcb_egl` and
`PYOPENGL_PLATFORM=egl`. Qt and PyOpenGL must use the same context backend;
select these settings before importing either library. A desktop GLX setup
uses `QT_XCB_GL_INTEGRATION=xcb_glx` and the normal PyOpenGL platform.
Metric comparisons remain separate from the viewer.

For incremental geometry or a network receiver:

```python
with open4d.Visualizer(title="Comparison") as viewer:
    viewer.add_geometry(reference_frame, baseline=True)
    viewer.add_geometry(decoded_frame)
    while viewer.poll_events():
        # When a new frame arrives:
        # viewer.update_geometry(next_frame)
        viewer.render()
```

`viewer.stream(receiver)` uses a bounded background reader while the Qt event
loop stays on the main thread. Close the caller-owned receiver to interrupt a
blocked network read. PointClouds render as points. GaussianClouds are colored,
opacity-weighted center previews, not a full Gaussian covariance renderer.

`open4d.compare_sequences` measures a decoded sequence against its source,
frame by frame. It needs SciPy (`open4d[metrics]`). Either argument can be a
mesh `Sequence` or a path that `open4d.load` opens as meshes:

```python
result = open4d.compare_sequences("input_frames/", "capture.o4d")
print(result.symmetric_psnr_db, result.hausdorff, result.worst_frame)
```

Sequences must have the same length and matching timestamps within
`timestamp_tolerance` seconds; nothing is aligned or resampled. Distances run
from each vertex to the nearest vertex of the other mesh, in both directions.
`metric="plane"` projects each offset onto the normals of the mesh it is
measured against. `peak` only affects PSNR; by default it is the largest
reference bounding-box diagonal, shared by every frame.

The result has `symmetric_rms` (each frame's worse-direction RMS, combined with
equal frame weights), `hausdorff` (largest error in any frame or direction),
`symmetric_psnr_db`, `worst_frame`, the per-frame `MeshComparison` objects in
`frames`, and `timestamps`, `peak` and `metric`. `compare_meshes` compares two
`TriangleMesh` objects the same way.

Paths are opened with default options and closed before returning; sequences
you pass stay open. Pass a `Sequence` when a source needs load options. Gaussian
and neural-field artifacts raise `TypeError`.

## Command line

```bash
open4d demo wave/                 # 60 PLY frames with a LICENSE and README
open4d inspect wave/
open4d inspect capture.o4d --json
open4d view capture.o4d         # requires open4d[player]
```

`inspect` on a `.o4d`, or a USD file carrying an O4D prim, reads only the
container header: codec, stored representation, dependency mode, frame count,
timing and payload sizes. It works for every profile without a codec backend;
payload hashes are verified but nothing is decoded. `--decode` also decodes a
mesh profile and reports its topology and first frame, which needs the codec
backend. With `--json`, container details are under `container`. `view` plays
mesh profiles and NumPy geometry sequences (points or Gaussian center previews).
Native trained Gaussian and field profiles still require their native renderer.

## O4D format

O4D is a custom container for compressed sequences, with a versioned header,
metadata, codec profiles and native payload records. It is not MPEG V-DMC
interchange; a V-DMC profile carries an encoder-produced V3C bitstream as its
native payload.

The Python API uses `Sequence` or `NativeSequence` to expose a file. Those are
in-memory interfaces, not data types embedded in the file.

### Binary layout

All integers in the container are unsigned and big endian. The file starts with
eight bytes: `56 4d 45 53 48 00 01 00` (`VMESH`, zero, version 1, reserved zero).
The signature and manifest schema retain their original wire identifiers;
renaming the public format to O4D does not change encoded bytes.
Version 1 of the envelope is fixed. New codec payload layouts require their
own native profile version; unsupported versions are rejected before native
execution. NumPy geometry profiles use native version 2 with an explicit
representation per frame and can still decode earlier version 1 mesh payloads.
Each following record is:

| Field | Bytes | Meaning |
| --- | --- | --- |
| Record length | 4 | Length of the record header plus its data |
| Record version | 1 | Must be 1 |
| Kind | 1 | 0 = manifest, 1 = native payload chunk, 2 = end |
| File ID | 4 | Zero-based native file index; zero for manifest/end |
| Offset | 8 | Offset within the native payload; zero for manifest/end |
| Data | variable | JSON manifest, native bytes, or end digest |

There is exactly one manifest, followed by all payload chunks in manifest file
order, followed by exactly one end record. The end data is the 32-byte SHA-256
of the exact manifest bytes. No trailing bytes are permitted.

The UTF-8 JSON manifest has `schema: "vmesh/1"`, `codec`, a profile-specific `native_version`,
`representation`, `dependency_mode`, `frame_count`, `sequence`, `files`, and
codec-specific `native` settings when required. `sequence` contains version 1,
the codec ID, frame indices, timestamps in seconds, JSON metadata, and optional
normalization/topology fields. It cannot carry an application `schema` field.
Each file record contains `id`, `name`, `role`, byte `size`, and hexadecimal
`sha256`. Filenames and roles must exactly match the selected codec profile;
input cannot supply arbitrary extraction paths.

The manifest is at most 16 MiB; payload chunks are at most 1 MiB. Frame and
profile file counts are bounded at 65,536 (including the generated adapter
metadata in the profile file limit). Some profiles have lower frame limits
because they require several files per frame. Duplicate JSON keys, non-finite
numbers, reordered chunks, offsets, unknown versions, hash failures and extra
records are rejected. These hashes detect corruption; they do not authenticate
a publisher or make an executable research payload safe to decode.

### Native profiles

| Codec | Native payloads | Dependencies |
| --- | --- | --- |
| KLT | `decoder_context.pt`, `<ordinal>_quantized_indices.zst`, `<ordinal>_quantized_metadata.npz` | Shared basis, independent coefficients |
| QNDF / QNDF-int8 | `frame_<ordinal>.pt`: subdivided base mesh, displacement context and model | Independent frames; int8 decode on CPU |
| N4MC | `checkpoint.pt`, `frame_<ordinal>.npz` | Shared model, independent TSDF latents |
| TVMC | `reference.drc`, `displacement_<ordinal>.drc` and `.npy` | Shared mesh reference; displacement and vertex order |
| TSMC | `reference.drc`, `B_matrix.txt`, `T_matrix.txt`, `delta_trajectories_encoded.npy`, `entropy_model.npz` | Whole temporal group |
| V-DMC / faster V-DMC | `sequence.vmesh`, optional `decoder.cfg` | Actual native encoder bitstream |
| Vega | `manifest.json`, `color_model.pt`, `frame_<ordinal>.pt` | Shared appearance and key/residual state |
| QUEEN | `initial.ply`, `frame_<ordinal>.pkl` for residual frames | Previous Gaussian frame |
| 3DGStream | `initial.ply`, `ntc_config.json`, `ntc_<ordinal>.pth`, optional `added_<ordinal>.ply` | Previous frame and transform architecture |
| ReRF | Field/model configuration, `rgb_net.tar`, headers, occupancy/channel entropy files, optional PCA/motion | Previous field and group keys |

Frame ordinals are zero-padded to six digits except Vega's four-digit native
index. ReRF uses its declared native frame IDs and channel suffixes. The profile
defines exact names and order.
The separate `frames` delivery profile carries standard per-frame PLY, Draco,
splat or image payloads and is not another research compression method.

Native files are copied verbatim. A directory's `metadata.json` is parsed into
the manifest and regenerated for the codec adapters on extraction; its JSON
whitespace is not preserved.

Use `open4d.codec.inspect_o4d`, `pack_o4d`, and `unpack_o4d` to inspect,
create and extract files without loading native models. Inspection verifies
all hashes. Extraction is staged and published only after complete validation.

### USD interchange

Compressed interchange uses a custom prim of type `O4D` at `/O4D`, with
`o4d:schema = "o4d.usd/1"`, timeline samples in `o4d:frameIndex`, and
static `o4d:payload:chunkNNNNNN` byte arrays. `o4d:payloadManifest` describes
the exact carried O4D file and its hash. Rendering requires native decoding.

### Compatibility

Normal load, save, encode and decode accept standalone O4D for compressed
sequences. Existing standalone `.vmesh` files can be renamed to `.o4d` without
conversion. The `VMESH` USD prim and its `vmesh:*` attributes remain readable;
new USD exports use the `O4D` prim. Native MPEG payload filenames, including
`sequence.vmesh`, are unchanged inside the container.

Convert retired ZIP and private V3C wrappers with `open4d.migrate_legacy`;
those older layouts require conversion. Use `open4d.import_native` for supported trained
native runs and `pack_o4d` for validated profile directories. Compressed USD
state uses the custom `O4D` prim described above.
Raw native MPEG V-DMC bitstreams remain supported as read/import inputs using
external frame timing; new compressed output is always standalone O4D.

```python
open4d.migrate_legacy("older_artifact", "converted.o4d", fps=30.0)
```

Migration covers earlier research-codec exports and packed browser clips. Older
private benchmark artifacts must be re-encoded from their original geometry.
`overwrite=False` protects an existing destination, and `fps` supplies timing
only when the old input did not carry it.

## Tutorials

Start with the NumPy core, then add the dependencies needed for a particular
workflow. The runnable examples below use synthetic MIT data unless an input
path or download URL is explicitly supplied. Research runtimes, captured data
and model weights retain their own licenses. None of these examples installs
packages, acquires hardware data, or starts GPU training automatically.

The tutorials target this revision's Open4D 0.3 API. For a checkout validation,
install its core into an isolated environment with `python -m pip install .`
before running the scripts. An older installed Open4D package may not expose
these APIs. This local installation is separate from publication approval in
the [release ledger](../THIRD_PARTY.md).

### 1. Round-trip mesh, point-cloud and Gaussian data

The [core round-trip example](../examples/tutorials/core_roundtrip.py) generates
eight mesh-wave frames, derives point clouds from their vertices, and generates
a Gaussian ring. Each becomes a separate `.o4d` file using `codec="npz"`.
The program reopens every artifact and checks exact positions, geometry fields
and timestamps. A new output directory protects existing work:

```bash
python examples/tutorials/core_roundtrip.py --output /tmp/o4d-core
open4d inspect /tmp/o4d-core/mesh.o4d --json
open4d inspect /tmp/o4d-core/points.o4d --json
open4d inspect /tmp/o4d-core/gaussians.o4d --json
```

This needs only `open4d` and NumPy. Add `--usd` after installing `open4d[usd]`
to export the mesh as `mesh.usdc`. Ordinary USD writing currently accepts
meshes; use the NumPy `.o4d` profile for PointCloud and GaussianCloud examples.
GaussianCloud stores activated scales/opacities, wxyz quaternions and RGB.
It does not imply a trained neural model or view-dependent appearance.

To build your own sequence, wrap a geometry value in `Frame(index, timestamp,
geometry)` and pass those frames to `Sequence(MemoryFrameProvider(frames))`.
Keep sequences in a `with` block to close file-backed providers reliably.

### 2. Cache examples and explicitly download licensed assets

The [data cache example](../examples/tutorials/data_cache.py) first shows the
synthetic mesh and Gaussian cache without using the network:

```bash
python examples/tutorials/data_cache.py --cache /tmp/o4d-cache
```

`sample.path` materializes the sample on first access; later accesses validate
the cache. `sample.load()` returns a Sequence. The default application cache is
`~/.open4d/data`; set `OPEN4D_DATA_DIR` or pass `cache_dir=` to choose another
location. For a real external asset, gather its HTTPS URL, exact SHA-256 digest,
data license and terms URL before constructing an immutable specification:

```python
from open4d.data import DatasetSpec, DownloadableDataset

spec = DatasetSpec(
    name="your-reviewed-mesh-data",
    url=reviewed_https_url,
    sha256=verified_sha256,
    license=asset_license,
    license_url=asset_license_terms_url,
    kind="mesh",
    archive="zip",
)
dataset = DownloadableDataset(spec)
print(dataset.path)  # Explicit first download; returns the extracted directory.
```

These variables must come from the asset's verified provenance; this snippet
does not invent a hosted dataset or grant redistribution permission. The CLI
accepts the same values through `--url`, `--sha256`, `--license`,
`--license-url` and `--archive`. Use a separate `kind="model"` specification for
weights with independently reviewed model/training-data rights. Corrupt caches
raise an error; ZIP/TAR paths, links and byte/file limits are checked before
publication of a completed download.

### 3. Reconstruct a saved, calibrated RGB-D capture

The [RGB-D capture example](../examples/tutorials/rgbd_capture.py) loads real
saved two-camera pairs and their measured calibration. Install `open4d[capture]`
in a supported environment, then provide both required folders:

```bash
python examples/tutorials/rgbd_capture.py \
  --capture /path/to/pairs --calibration /path/to/rig-calibration \
  --frames 1 2 --output /tmp/reconstructed.o4d
```

`--frames` names increasing saved pair numbers, not zero-based array offsets.
The pairs use the [capture layout](../open4d/reconstruction/rgbd/README.md);
the rig folder contains factory calibration and the measured depth-to-depth
transform. `RGBDCapture.from_folder(pairs, calibration)` rectifies depth and
aligns color. `reconstruct(capture, output, method="tsdf", codec="npz")` then
fuses each pair into a temporal mesh Sequence and encodes it. It does not
calibrate, synchronize or acquire cameras. Add `--usd /tmp/reconstructed.usdc`
with `open4d[usd]` to export animated meshes. Open3D 0.19 availability depends
on the interpreter/platform; capture loading and reconstruction are separate
dependency boundaries.

### 4. Train a caller-owned generic model

The [generic training example](../examples/tutorials/train_generic.py) learns
`y = 2x + 1` from synthetic tensors on the CPU. It requires `open4d[torch]` and
does not download a model or checkpoint:

```bash
python examples/tutorials/train_generic.py --epochs 40
```

The program prints initial/final losses, step count and learned parameters,
and verifies that its original evaluation mode was restored. Supply your own
model, optimizer, loss function and re-iterable `(inputs, targets)` batches to
`o4d.ml.train`. Tensors must already be on the selected model device. This is a
shared training utility; neural compression remains an installed codec/backend
workflow with separate runtime and weight provenance.

### 5. Record decoded meshes over TCP

The [TCP example](../examples/tutorials/tcp_stream.py) runs a sender and receiver
on an ephemeral loopback port, transfers four synthetic mesh frames, checks
positions/timestamps and optionally saves a received recording:

```bash
python examples/tutorials/tcp_stream.py --output /tmp/recording.o4d
```

It needs the NumPy core. `receive(port=0)` gives the selected endpoint in
`receiver.address`; `send(sequence, *receiver.address, realtime=False)` transmits
without pacing. `receiver.record(max_frames=4)` returns an owned Sequence.
Close the receiver and recording when finished. This protocol sends decoded
mesh arrays; compressed `.o4d` bytes and native Gaussian/field state use their
own delivery workflows.

### 6. Export a browser playback bundle

The [browser streaming example](../examples/tutorials/browser_stream.py) uses
the separately installed `open4d-streamer` distribution. From a checkout, a
local installation is `pip install ./open4d/streamer`; use its reviewed release
package when available. Start with PLY delivery to avoid a native Draco build:

```bash
python examples/tutorials/browser_stream.py --output /tmp/o4d-browser --seconds 3
python examples/tutorials/browser_stream.py \
  --source /tmp/o4d-core/mesh.o4d --output /tmp/o4d-browser-real
```

The first command performs a bounded loopback HTTP check and stops the server.
The second opens the browser and serves until Ctrl+C. Choose a fresh output
directory each time. `--rung ply --rung draco@11` defines a delivery ladder;
Draco needs its native encoder. Gaussian rungs use `ply` or `splat`; native
Gaussian inputs also require the corresponding explicitly configured decoder.
Bundle export prepares data before playback: serving it does not prove live
codec processing or adaptation beyond the selected ladder/policy.

### 7. Compare a baseline and its decoded result

The [comparison example](../examples/tutorials/compare_visualization.py) opens
two caller-provided sequences in shared-camera panes with `open4d[player]`:

```bash
python examples/tutorials/compare_visualization.py \
  --baseline /tmp/o4d-core/mesh.o4d --decoded /tmp/o4d-core/mesh.o4d --seconds 3
```

Pass `--metrics` with `open4d[metrics]` to also print nearest-vertex RMS and
Hausdorff distances. Real OBJ inputs retain per-vertex or seam-specific
per-corner UVs. `--texture /path/to/atlas.png` explicitly supplies an image for
both textured meshes; no material path is followed automatically. The selected
decoded codec must preserve UVs to use the same atlas. GaussianCloud panes are
center previews; actual Gaussian covariance and directional appearance require
a native splat renderer.

For sequence playback, the shorter equivalent is:

```python
o4d.visualize("decoded.o4d", baseline="reference.o4d")
```

A manually managed `Visualizer` is useful for live updates: create the window
on the main thread, add/update geometries, call `poll_events()` and `render()`,
and destroy it when finished. It does not take ownership of supplied Sequences
or stream receivers.
