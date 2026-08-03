# Shared temporal geometry design

## Purpose

`Sequence` is the central Open4D data abstraction: it gives loaders, processors,
codecs, metrics, viewers, and integrations one lazy random-access interface
without requiring them to agree on storage or decoding. The intended long-term
flow is:

```text
Loader -> Sequence -> Processor -> Sequence -> Codec -> Sequence -> Viewer
```

This first version deliberately covers triangle meshes only. It establishes the
value and provider boundaries before codec APIs are changed.

## Existing representations

The repository currently has no shared frame value. The main paths use these
representations:

| Path | Geometry and attributes | Time and sequence state | Topology |
|---|---|---|---|
| TVMC | OBJ files loaded as Open3D `TriangleMesh`; a Trimesh bridge preserves vertex order and converts vertices, faces, vertex normals, RGB colors, and per-corner UVs. Later stages store `(N, 3)` NumPy displacement text/PLY files against a reference mesh. | `firstIndex`, `lastIndex`, and filename numbering; JSON pipeline configuration carries dataset metadata. No frame timestamps. | Input meshes may differ. Fitted/displacement frames require fixed reference vertex order and connectivity. Draco reordering is corrected explicitly with a nearest-neighbor map. |
| TSMC | Mostly duplicated Open3D mesh and Trimesh conversion code, plus separate static and dynamic meshes and NumPy displacement arrays. Normals are frequently recomputed; OBJ writes often omit colors, normals, and UVs. | Integer filename ranges and command-line frame counts; no shared metadata or timestamps. | Scene extraction can change topology. The displacement representation is fixed to a subdivided reference mesh with vertex correspondence. |
| N4MC | A local Torch `Mesh(vertices, faces)` wrapper with face normals, point-cloud-utils vertex/face arrays, TSDF tensors, Trimesh evaluation meshes, and Open3D visualization meshes. Marching cubes returns independent vertex, face, and normal arrays. | Dataset samples are dictionaries with string `frame_id`, tensor `index`, paths, and TSDF statistics. Sequence utilities return parallel lists. No common timestamps. | TSDF marching cubes is changing topology. Legacy displacement/KLT experiments assume a fixed vertex count and correspondence. |
| Former MeshReduce / 4D reconstruction | Native code uses Open3D tensor `TriangleMesh` plus a separate OpenCV texture in `MeshFrame`. Python fusion returns legacy Open3D meshes inside `MeshResult`, with colors and normals held by Open3D. | Transport structures carry device timestamps, pair numbers, and synchronization metadata separately from mesh values. `MeshResult` carries `source_pair` and build metrics. | TSDF extraction and independent mesh merging produce changing topology and vertex counts. |
| Players | Mesh player consumes `(vertices, faces, timestamp)` tuples and keeps a separate sorted frame-ID list. Point-cloud players use `(points, colors, timestamp)` tuples. | Playback FPS is supplied by the caller rather than derived from timestamps. | Not declared. |
| `.o4d` I/O | Mesh reader returns `(float32 vertices, uint32 faces, timestamp)`; point readers return `(float32 points, optional uint8 RGB, timestamp)`. Index entries carry stored frame IDs and timestamps; HEAD JSON carries metadata. | Indexed on-demand decoding, but each reader has its own tuple shape. | The v1 container does not declare topology. |

The largest incompatibilities are `faces` versus `triangles`, Torch versus
NumPy versus Open3D storage, RGB float versus byte colors, per-vertex versus
per-corner texture coordinates, integer/string frame IDs, timestamps stored
outside geometry, and implicit topology assumptions. TVMC and TSMC also carry
near-duplicate mesh conversion and displacement code.

## Layer boundaries

- **Geometry** is a spatial value. `TriangleMesh` owns no time or codec state.
- **Frame** associates one geometry with a stored frame index, timestamp, and
  lightweight metadata.
- **Provider** owns storage and decoding. Its required contract is only frame
  count plus random access. Metadata, timestamps, topology declarations, and
  cleanup are optional capabilities.
- **Sequence** normalizes indexing, iteration, slicing, timing properties, and
  topology declarations while remaining lazy.
- **Codec** should consume or produce sequences through providers. Encoded
  bitstreams and codec configuration do not belong in `TriangleMesh`.
- **Operation** transforms frames or sequences and may return a lazy provider
  that performs work on access.

This separation lets a directory loader, `.o4d` decoder, neural decoder, and
live capture expose the same API without copying their complete output into
memory.

## Geometry and mutability

`TriangleMesh` validates floating-point `(N, 3)` positions, integer `(M, 3)`
triangles, finite values, and triangle bounds. Optional colors are RGB/RGBA
bytes in `[0, 255]` or floating point in `[0, 1]`; normals are floating-point
`(N, 3)` arrays. Texture coordinates may be per-vertex `(N, 2)` or per-corner
`(M, 3, 2)`. Named numeric or boolean attributes must align with vertices,
triangles, or triangle corners.

The object is structurally immutable, but its NumPy buffers may be shared and
remain writable. Construction uses `numpy.asarray` and does not copy merely to
change dtype or ownership. A caller that needs a value snapshot must pass
copies. The attributes and metadata mappings are shallow read-only snapshots.

## Lazy access and views

Creating a `Sequence` reads only provider declarations. `len`, metadata, and
topology do not request a frame. Integer indexing requests exactly one provider
frame. A `SequenceView` stores a Python `range` mapping and does not copy or
decode frames; nested slices remain lightweight.

`timestamps`, `duration`, and average `fps` are explicit properties. They use a
provider timestamp table when available. Otherwise requesting them may decode
frames to obtain timestamps. This cost is never paid automatically during
construction. Timestamps must be finite and nondecreasing.

## Topology declarations

`TopologyMode` has three values:

- `FIXED`: triangle connectivity is invariant. Constant vertex count and vertex
  correspondence are therefore true unless a provider makes a more specific
  declaration.
- `CHANGING`: connectivity changes. Vertex count and correspondence are not
  inferred because either can still be constant independently.
- `UNKNOWN`: the provider cannot make a reliable declaration without scanning
  or decoding the sequence.

These values are declarations, not results of an automatic sequence-wide
comparison. `has_constant_topology`, `has_constant_vertex_count`, and
`has_vertex_correspondence` return `None` when the available declaration is
insufficient. Expensive verification should be a future explicit operation.

## First integration

`open_o4d_mesh_sequence(path)` wraps the existing `O4DMeshReader`. Opening the
sequence reads the HEAD and frame index but does not decode geometry. Ordinal
sequence access maps to the stored frame ID and converts only that reader tuple
to a core `Frame` and `TriangleMesh`. Existing reader and writer APIs remain
unchanged. The sequence should be used as a context manager so its file closes
deterministically.

Open3D remains an external frame operation. An adapter converts
`frame.geometry` to an Open3D mesh for visualization or processing; Open3D does
not own sequence timing, lazy decoding, or topology declarations.

## Staged migration

1. **TVMC:** add a lazy numbered-OBJ provider for inputs and decoded outputs.
   Represent fitted reference meshes as `FIXED` sequences, store displacement
   arrays as named attributes or provider state, and assert vertex
   correspondence at the boundary. Keep current CLI and files intact.
2. **TSMC:** reuse the TVMC directory provider, then expose static and dynamic
   outputs as separate sequences. Mark pre-reference extraction as `CHANGING`
   and reference-displacement reconstruction as `FIXED`. Remove duplicated
   Open3D/Trimesh conversion only after parity tests exist.
3. **N4MC:** first adapt reconstructed Trimesh/marching-cubes outputs through a
   provider while leaving TSDF training datasets unchanged. Later define a
   volume geometry type rather than forcing TSDF tensors into `TriangleMesh`.
   Declare marching-cubes output `CHANGING`.
4. **Former MeshReduce / 4D reconstruction:** adapt completed `MeshResult`
   values at the Python boundary, preserving pair number and synchronization
   data in frame metadata. A future live/ring-buffer provider can represent an
   unknown-length stream; the current finite `Sequence` should not pretend to
   solve streaming.

TVMC is the next recommended migration because it already has ordered mesh
directories, a configuration-level frame range, and a clear transition from
changing input topology to fixed-reference displacements. Its directory
provider can then be reused by TSMC and viewers.

## Non-goals

This version does not define point clouds, volumes, transforms, materials,
live unknown-length streams, mutation/replacement APIs, processing graphs,
codec interfaces, caching, prefetching, automatic topology scans, or direct
Open3D methods. It does not rewrite TVMC, TSMC, N4MC, or former MeshReduce.
Those capabilities should be added after real providers demonstrate their
requirements.
