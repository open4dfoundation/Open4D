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

## Example I/O at the audited revision

`examples/visualization/frame_sources.py` currently provides the one-call loader:

```python
from frame_sources import open_sequence

with open_sequence("frames/", fps=30.0) as sequence:
    frame = sequence[0]
```

Supported sources are:

| Source | Base/extra | Current behavior |
| --- | --- | --- |
| folder of `.obj`/`.ply` | base NumPy | lazy listing/provider; last filename number controls order |
| one `.obj`/`.ply` | base NumPy | single fixed frame |
| folder or one `.usd/.usda/.usdc/.usdz` | `.[usd]` | lazy USD provider |
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

The planned P1 public interface was:

```python
open4d.io.open_sequence(path, fps=None) -> Sequence
open4d.io.write_usd_container(path, frames, ...) -> pathlib.Path
```

`open4d.io.open_sequence` and the mesh-file writers now exist. USD remains
example-local. Examples should become thin clients of public functions;
promotion must keep
construction lazy, add cleanup and malformed-source tests, and use actionable
optional-dependency errors.

## OpenUSD container

OpenUSD is the selected primary **offline interchange container**, not a
compression algorithm. The example writer already creates time-sampled
`UsdGeom.Mesh` or `UsdGeom.Points` data with:

- positions and bounds per frame;
- triangle connectivity once for fixed topology or at key frames;
- per-vertex display color when present;
- custom frame index, timestamp, keyframe, vertex-count, and triangle-count
  streams;
- stage FPS/time range/up axis and `customLayerData["open4d"]` metadata.

The example format labels its container metadata version `1`, but it is not yet
the ratified Open4D schema v1. Known fidelity and validation gaps are:

- normals, UVs, named attributes, materials, and general frame metadata are not
  round-tripped;
- the reader places stored frame-index streams into metadata but constructs
  frames with ordinal indices;
- the first frame chooses Mesh versus Points, so a later mixed sequence is not
  rejected explicitly and may lose connectivity;
- an empty position array fails when the writer calculates min/max extent;
- any `up_axis` other than `"z"` becomes Y; X is not explicitly rejected;
- transform, units, and coordinate-frame semantics are not first-class.

Schema v1 must preserve positions, triangles, colors, normals, UVs, named
attributes, stored timestamps/frame indices, sequence metadata, and topology
declarations. It must support Y-up and Z-up, reject X-up for now, omit extents
safely for empty geometry, and reject mixed Mesh/Points sequences rather than
silently dropping data. Unsupported values must fail explicitly.

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

The PyQt viewers decode and measure the selected display frames up front, then
provide orbit, zoom, scrub, stepping, playback, GIF output, fixed sequence-wide
error colors, and synchronized comparison cameras. Preserve existing comparison
behavior while moving only stable loader/metric/container code into the package.

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
