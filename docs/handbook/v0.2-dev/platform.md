# Core, I/O, OpenUSD, and metrics

This chapter records the supported core, example code, and planned public APIs
at commit `96b8c7b`. It is historical. See
[current implementation status](implementation-status.md) and the root README
for APIs promoted after the audit.

## Public core model

The root `open4d` package exports:

- `TriangleMesh`
- `Frame`
- the `FrameProvider` protocol and `MemoryFrameProvider`
- `Sequence`, `SequenceView`, and `TopologyMode`
- canonical dtype constants and the `dtypes` module

The base dependency is NumPy. Heavy readers, renderers, ML frameworks, and
codec dependencies should remain optional.

### TriangleMesh

`TriangleMesh` validates array shape, numeric/boolean type, finite values,
triangle bounds, color range, and attribute alignment. Its canonical storage is:

| Field | Shape | Stored dtype/range |
| --- | --- | --- |
| `positions` | `(N, 3)` | `float32` |
| `triangles` | `(M, 3)` | `uint32` |
| `colors` | `(N, 3)` or `(N, 4)` | `float32`, `[0, 1]` |
| `normals` | `(N, 3)` | `float32` |
| `texture_coordinates` | `(N, 2)` or `(M, 3, 2)` | `float32` |
| float attributes | first dimension vertex/face/corner aligned | `float32` |
| integer attributes | first dimension vertex/face/corner aligned | `int32` |
| boolean attributes | first dimension vertex/face/corner aligned | `bool` |

The object is structurally immutable, but its NumPy buffers are not forced
read-only. Canonical arrays may be shared zero-copy with the caller. Preserve
this policy unless a measured need and migration plan justify changing it.

At the audited revision, generic integer attributes lacked an explicit `int32`
range check. That check and its boundary tests have since been implemented.

### Frame

`Frame(frame_index, timestamp, geometry, metadata)` validates a nonnegative
integer source index, finite real timestamp, `TriangleMesh`, and mapping
metadata. Metadata is a shallow read-only snapshot. It is not a place to hide a
codec's encoded state.

### FrameProvider and Sequence

A provider must expose `frame_count` and `get_frame(index)`. It may declare
metadata, a timestamp table, topology, constant vertex count, correspondence,
and `close()`.

`Sequence` provides:

- finite length and ordinal indexing, including negative indices;
- iteration and lazy slice views;
- provider metadata and topology declarations;
- validated nondecreasing timestamps, duration, and average FPS;
- context-manager cleanup.

Construction reads declarations only. Integer access asks the provider for one
frame. Requesting timestamps can decode every frame when the provider has no
timestamp table. A `SequenceView` is lazy, although its timestamp property reads
parent frames through the view provider.

Topology flags are promises, not results of an automatic scan. `FIXED` implies
constant topology, vertex count, and correspondence unless the provider makes a
more specific declaration; `UNKNOWN` correctly returns `None` when evidence is
insufficient.

## Public sequence I/O

Whole-sequence files use the top-level API:

```python
import open4d

with open4d.load("capture.usdc") as sequence:
    open4d.save(sequence, "capture.o4d")

open4d.visualize("capture.usdc")
```

Supported sources are:

| Source | Base/extra | Current behavior |
| --- | --- | --- |
| `.o4d` and registered codec artifacts | base or codec-specific | lazy whole-sequence decode |
| one `.usd/.usda/.usdc/.usdz` | `.[usd]` | lazy OpenUSD sequence provider |
| folder of `.obj`/`.ply` | base NumPy | lazy listing/provider; last filename number controls order |
| one `.obj`/`.ply` | base NumPy | single fixed frame |
| `.off/.stl/.glb/.gltf` | `.[tools]` | trimesh fallback |

Important limitations:

- OBJ reads positions/faces but ignores normals, UVs, groups, materials, and
  colors; polygons are fan-triangulated.
- built-in PLY reads positions, triangles, and RGB; unusual PLY variants fall
  back to trimesh.
- mixed-format folders choose the most common suffix and skip the others.
- ordering uses the **last** integer in the filename, so `frame_003_qp9.obj`
  sorts on 9 and can silently misalign comparisons.
- zero-face geometry stands in for point clouds because core has no point type.

The public whole-sequence interface is:

```python
open4d.load(path, fps=None) -> Sequence
open4d.save(sequence, "capture.usdc") -> pathlib.Path
open4d.visualize(path_or_sequence)
open4d.unload(sequence)
```

The existing `open4d.io` and `open4d.codec` entry points remain available and
delegate to the same providers and writers. USD construction is lazy, cleanup
is explicit, and a missing optional dependency reports the exact install extra.

## OpenUSD container

OpenUSD is a public **offline interchange container**, not a compression
algorithm. Schema `open4d.usd-sequence/v1` stores:

- positions and bounds per frame;
- triangle connectivity once for fixed topology or at key frames;
- RGB/RGBA, normals, vertex and face-varying UVs;
- typed custom float, integer, and boolean arrays with exact shapes;
- exact frame indices, timestamps, frame/sequence metadata, and topology
  declarations;
- stage FPS/time range and Y/Z up axis.

Writes are atomic, empty geometry omits its extent safely, and unsupported
metadata fails before replacing an existing destination. `.usdc` is the
recommended compact interchange extension; `.o4d` remains the default lossless
Open4D codec artifact.

## Working example metrics

The current metric implementation lives in
`examples/visualization/mesh_metrics.py` and `compare_frames.py`.

For each frame it computes nearest-vertex distances in both directions:

```text
decoded -> reference   catches displaced/extra decoded vertices
reference -> decoded   catches geometry the decoder deleted
```

`point` uses Euclidean nearest-vertex distance. `plane` projects the offset
onto the nearest reference vertex normal and falls back to point distance when
a usable normal is unavailable. Symmetric RMS and maximum use the worse
direction; symmetric PSNR uses the lower direction. One reference bounding-box
diagonal is used as the peak for all compared frames.

Scientific limits must be stated with every result:

- distances are between **vertex sets**, not continuous triangle surfaces;
- “Hausdorff” is therefore the worse sampled vertex maximum, not exact surface
  Hausdorff distance;
- results are not face-area weighted;
- inputs are not ICP-aligned, registered, rescaled, or unit-converted;
- point-to-plane depends on computed vertex normals and winding/degeneracy;
- identical meshes produce infinite PSNR; a degenerate peak produces NaN.

Current sequence behavior also needs hardening:

- unequal sequences silently compare through the shorter sequence and record a
  truncation note;
- sequence RMS is an RMS of per-frame symmetric RMS values, not a pooled
  per-vertex error;
- sequence PSNR is an arithmetic mean of per-frame PSNR;
- `worst_frame` is an index into comparison results, not the stored source frame
  index.

The P1 public interface is:

```python
open4d.metrics.compare_meshes(...)
open4d.metrics.compare_sequences(..., allow_truncate=False)
```

Lengths must match by default. Explicit truncation may retain the current
behavior. Sequence RMS/PSNR must pool error with one documented peak, and reports
must retain original frame indices. Publish an algorithm identifier such as
`open4d.vertex-nearest/v1` so old paper results remain interpretable after a
metric improves.

## Viewers and adapters

The single-sequence PyQt viewer decodes strided frames on demand through a
three-frame LRU and schedules one-frame lookahead on the Qt event loop. It
frames the first displayed geometry instead of scanning the complete sequence.
The comparison viewer still measures selected frames up front so it can provide
fixed sequence-wide error colors and synchronized cameras.

The Open3D integration converts Open4D geometry or compatible arrays into an
Open3D `TriangleMesh` or `PointCloud`, preserving vertex colors/normals. It does
not carry texture coordinates/materials and must remain an external frame
operation; Open3D should not own timing or laziness.

`open4d.torch_ops` replaces the small subset of PyTorch3D previously used by
research codecs: geometry-only OBJ I/O, normal computation, surface sampling,
Chamfer distance, and point-to-face distance. Keep its tested numerical
semantics, but do not make Torch the base data model.

## Run manifest v1

Every complete vertical slice should emit `open4d.run-manifest/v1` containing:

- Open4D commit and dirty-state indication;
- source URI/name, license, content hash, frame range, frame indices/timestamps;
- codec identifier/version and complete normalized configuration;
- every required artifact path, SHA-256, role, and actual byte count;
- environment: OS, Python/tool versions, CPU/GPU, relevant driver/CUDA;
- encode/decode/stage timings and measurement method;
- metric identifier, peak convention, configuration, and results;
- payload, filesystem/container, and (when applicable) wire byte counts as
  separate values.

The manifest must be sufficient to decide what a fresh decoder needs and to
distinguish a paper claim from a reproduced run.
