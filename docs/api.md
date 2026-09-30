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
All registered compression methods use `.vmesh`; its descriptor identifies the
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

What a decoded frame *is* is deliberately separate from the codec that produced
it: a triangle mesh is a mesh whether it arrived as OBJ, as a Draco payload, or
out of a V-DMC bitstream. `open4d.Representation` is the axis to gate on, and
`MESH`, `POINTS`, and `GAUSSIANS` have concrete types — `TriangleMesh`,
`PointCloud`, and `GaussianCloud`. Already-rendered pixels are named in the
taxonomy but have no concrete type yet; they land with the camera model they
need in order to be comparable at a known pose.

The mesh codecs accept triangle meshes. Native Gaussian and field methods require
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
triangle meshes; compressed VMESH USD interchange is described below.

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

Every public codec encodes to a single `.vmesh` file:

```python
open4d.encode("capture.usdc", "capture.vmesh", codec="tvmc")
with open4d.load("capture.vmesh") as sequence:
    open4d.save(sequence, "decoded.usdc")
```

The embedded codec is detected on load. The standalone [VMESH format](#vmesh-format)
preserves native compressed payloads and timing. See the [native `.vmesh` notebook](../examples/vmesh/01_container_and_usdc.ipynb)
for inspection, packing, extraction, and an exact compressed-state USDC round trip.
The [mesh example](../examples/vmesh/02_mesh_codecs.ipynb) demonstrates encoding
and saving decoded geometry as USDC.

Vega, QUEEN, 3DGStream and ReRF carry native compressed models. Import a
research run with `open4d.import_native(run, codec="queen", config=...)`, or
use `open4d.encode(run, "capture.vmesh", codec="queen", config=...)`.
Loading returns a `NativeSequence`; `native.decode(runtime=..., python=...)`
evaluates the actual temporal models. Saving it to USDC preserves the exact
compressed representation using the custom `VMESH` USD prim. This route does not
train a neural scene from an arbitrary mesh USD. See the linked guide for
required model/configuration files and representation-specific outputs.

KLT, N4MC, QNDF and QNDF-int8 accept mesh sequences or mesh USDC, for example
`open4d.encode(..., codec="n4mc")`. Loading reconstructs a mesh `Sequence`.
N4MC's profile identifies independent quantized TSDF latents sharing one model;
it does not claim temporal prediction. KLT carries a shared transform basis and
compressed frame coefficients. QNDF carries coarse meshes and independent
displacement models; QNDF-int8 uses quantized model weights.
The guide also documents exact compressed-state preservation in native USDC.

`available_codecs()` lists the public research adapters: `klt`, `n4mc`, `qndf`,
`qndf-int8`, `vdmc`, `faster_vdmc`, `tvmc`, `tsmc`, `vega`, `queen`,
`3dgstream`, and `rerf`. Use `.vmesh` for every registered compression method
and USD for interchange. NumPy archives, model checkpoints and Draco files are
native payload encodings within a profile, not alternate public sequence types.

The lightweight wheel includes the adapters but excludes their research
implementations. KLT, N4MC and QNDF need a source checkout and their optional
dependencies; set `OPEN4D_RESEARCH_ROOT` to that checkout when using an installed
wheel. Internal benchmark experiments are not public codec choices. The V-DMC adapters do not execute
shell scripts, but they do invoke configured native encoder and decoder
processes once per sequence. Callers can also register another
`open4d.codec.Codec`.
Native mesh processes run without a time limit by default; set
`OPEN4D_NATIVE_TIMEOUT` to a positive number of seconds to bound them.

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
N4MC, QNDF, and QNDF-int8. The larger Rafa quality/export matrix is
an additional CUDA acceptance test gated by `OPEN4D_TEST_RESEARCH_CODECS=1` and
`OPEN4D_RAFA_DATASET`; it is not presented as part of ordinary CI coverage.

## VMESH format

VMESH is a standalone custom data type for a compressed sequence. Its header,
metadata, codec profiles and records belong to VMESH. It does not manufacture a
V3C stream, insert private SEI messages, or serialize Open4D geometry objects.
It is not an interoperable MPEG V-DMC interchange format. A V-DMC profile can
carry a real encoder-produced V3C bitstream as its native payload.

The Python API uses `Sequence` or `NativeSequence` to expose a file. Those are
in-memory interfaces, not data types embedded in the file.

### Binary layout

All integers in the container are unsigned and big endian. The file starts with
eight bytes: `56 4d 45 53 48 00 01 00` (`VMESH`, zero, version 1, reserved zero).
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

The UTF-8 JSON manifest has `schema: "vmesh/1"`, `codec`, `native_version: 1`,
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
defines exact names and order; this table is not a user-supplied extraction map.
The separate `frames` delivery profile carries standard per-frame PLY, Draco,
splat or image payloads and is not another research compression method.

Native files are copied verbatim. A directory's `metadata.json` is parsed into
the VMESH manifest; it is **not a carried file**. Unpacking reconstructs a
`metadata.json` only as an input for the existing codec adapters. JSON whitespace
in that generated adapter file is not preserved. Native payload bytes are.

Use `open4d.codec.inspect_vmesh`, `pack_vmesh`, and `unpack_vmesh` to inspect,
create and extract files without loading native models. Inspection verifies
all hashes. Extraction is staged and published only after complete validation.

### USD interchange

Compressed interchange uses a custom prim of type `VMESH` at `/VMESH`, with
`vmesh:schema = "vmesh.usd/1"`, timeline samples in `vmesh:frameIndex`, and
static `vmesh:payload:chunkNNNNNN` byte arrays. `vmesh:payloadManifest` describes
the exact carried VMESH file and its hash. This prim preserves compressed state;
it does not claim to be a renderable `UsdGeomMesh` or Gaussian schema.

### Compatibility

Normal load, save, encode and decode accept standalone VMESH for compressed
sequences. Older artifacts require explicit `open4d.migrate_legacy` conversion;
renaming their extension is insufficient. Use `open4d.import_native` for a
supported trained native run and `pack_vmesh` for a validated profile directory.
Neither operation trains a model or turns arbitrary native files into a sequence.
Native research encodings remain inside the VMESH profile. Previously exported
USD compressed state must use the custom `VMESH` prim described above.
Raw native MPEG V-DMC bitstreams remain supported as read/import inputs using
external frame timing; new compressed output is always standalone VMESH.

```python
open4d.migrate_legacy("older_artifact", "converted.vmesh", fps=30.0)
```

Migration covers earlier research-codec exports and packed browser clips. Older
private benchmark artifacts must be re-encoded from their original geometry.
Migration is an explicit compatibility operation. `overwrite=False` protects an
existing destination, and `fps` supplies timing only when the old input did not
carry it. The result is validated VMESH; normal loading does not silently migrate.
