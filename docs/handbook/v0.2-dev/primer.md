# 3D and 4D geometry primer

This chapter gives you enough vocabulary to read the Open4D code and papers. It
deliberately starts from data, not rendering mathematics.

## Coordinates and coordinate systems

A 3D position is normally written `(x, y, z)`. The three numbers are meaningful
only with a coordinate system:

- **origin**: where `(0, 0, 0)` is;
- **axes**: which directions are positive x, y, and z;
- **handedness**: the orientation relationship among the axes;
- **up axis**: often Y in graphics tools and Z in robotics/data capture;
- **units**: metres, millimetres, or an arbitrary normalized cube;
- **transform**: a rotation, translation, and sometimes scale mapping one
  coordinate system to another.

Two meshes can look identical but measure as wildly different if one is in
metres and one in millimetres, or if their origins differ. The current shared
`TriangleMesh` stores scene-local coordinates but has no first-class transform,
units, or coordinate-frame field. Producers must therefore record these in
frame/sequence metadata and evaluators must not silently align meshes.

## Triangle meshes

A triangle mesh uses two main arrays:

```text
positions[N, 3]   one xyz point for each vertex
triangles[M, 3]   three integer vertex indices for each triangular face
```

For example, triangle `[0, 3, 2]` connects positions 0, 3, and 2. The order is
the **winding** and normally determines which side is the front.

Common optional data includes:

- **normals**: directions used for lighting or point-to-plane distance;
- **colors**: RGB or RGBA values, usually per vertex;
- **UV coordinates**: 2D coordinates that map mesh corners or vertices into an
  image texture;
- **materials/textures**: rules and images describing surface appearance;
- **attributes**: application-specific arrays aligned with vertices, faces, or
  face corners.

Open4D's current core represents triangle meshes, colors, normals, UVs, and
named numeric/boolean attributes. It does not yet model materials.

## Topology and correspondence

**Topology** means connectivity: which vertices make each face. Two frames have
fixed topology when the triangle-index array is unchanged. This is different
from vertex count: frames may have the same number of vertices but connect them
differently.

**Vertex correspondence** means vertex `i` represents the same physical or
semantic surface location over time. A temporal codec can store one reference
mesh plus per-vertex motion only when this correspondence exists. Independently
reconstructing a surface with marching cubes usually produces a new set and
ordering of vertices each frame, even if the object is the same.

Open4D declares, rather than guesses, these properties:

- `TopologyMode.FIXED`: connectivity is invariant;
- `TopologyMode.CHANGING`: connectivity changes;
- `TopologyMode.UNKNOWN`: the provider cannot promise either without scanning.

## Point clouds

A point cloud is a set of 3D samples, often with colors and normals, but no
faces connecting them. RGB-D cameras naturally produce point clouds: each valid
depth pixel becomes a point after applying camera calibration.

The v0.2-dev core has no `PointCloud` type. Example loaders temporarily represent
points as a `TriangleMesh` with zero triangles. That is a compatibility bridge,
not the intended long-term model.

## Voxels, signed distance fields, and TSDFs

A **voxel** is a 3D grid cell, the volumetric analogue of a pixel. A signed
distance field stores at each location the distance to the nearest surface:
positive on one side, negative on the other, and zero at the surface.

A **truncated signed distance field (TSDF)** stores this value only within a
limited distance of the surface. Truncation makes fusion and storage practical.
Multiple depth images can be integrated into one TSDF; a triangle mesh is then
extracted from the zero crossing, commonly with marching cubes.

Consequences that matter in this repository:

- RGB-D fusion and N4MC/KLT use volumetric representations internally.
- Marching cubes generally changes topology and vertex ordering each frame.
- A TSDF tensor should eventually get its own volume type; it should not be
  hidden inside `TriangleMesh`.

## Gaussian splats

A 3D Gaussian splat is a small, soft, oriented blob rather than a triangle.
Typical parameters include position, scale/covariance, orientation, opacity,
and view-dependent color coefficients. Thousands or millions are projected and
blended to render a view. Dynamic Gaussian methods update those parameters or
add/remove Gaussians over time.

Gaussian splats are excellent for photorealistic novel views but are not meshes:
they have no triangle connectivity, use a specialized differentiable renderer,
and their quality is normally evaluated in rendered images. QUEEN and
3DGStream therefore live with reconstruction. Ryan owns their algorithms,
training, quantization, CUDA rasterizers, and representation contract.

## Frames and sequences

A **frame** is geometry plus temporal identity:

```text
Frame
  frame_index   stored source identifier (nonnegative integer)
  timestamp     time in seconds on a declared clock/timeline
  geometry      currently TriangleMesh
  metadata      lightweight context
```

A **sequence** is an ordered, finite collection of frames. Open4D's `Sequence`
is lazy and random-access: construction reads provider declarations; requesting
frame 20 asks the provider to decode frame 20. A slice is a view, not a copied
list of decoded meshes.

Frame index and ordinal position are different. `sequence[3]` asks for the
fourth stored element; that frame's `frame_index` might be 120 if the source was
subsampled. Metrics and manifests need to preserve both.

## Time and clocks

A filename number is not automatically a timestamp. For files with no timing,
the example loader synthesizes timestamps from a supplied FPS. Live systems may
have several clocks:

- camera device time;
- sender monotonic or wall-clock time;
- receiver time;
- presentation time used by playback.

Clock domain, units, and synchronization error must travel with measurements.
Subtracting timestamps from unrelated clocks does not produce latency.

## Compression concepts

Compression removes redundancy. The main ideas used here are:

- **quantization**: store a value with fewer discrete levels;
- **transform coding**: rotate data into a basis where few coefficients matter
  (KLT/PCA is the classical example);
- **prediction/reference coding**: store changes from a previous/reference
  shape instead of every full shape;
- **neural representation**: train a compact network/latent code that generates
  geometry or a field;
- **entropy coding**: turn likely symbols into fewer bits without further loss;
- **keyframe and delta**: a self-contained base followed by dependent updates.

An encoded artifact is self-contained only when a fresh decoder needs no
original source, in-memory encoder values, or undeclared files. Model weights,
bases, means, normalization transforms, topology, and entropy side information
all count toward the artifact when decoding requires them.

## Containers, codecs, and transports

- A **codec** defines how data is encoded and decoded.
- A **container** packages one or more streams and metadata on disk. OpenUSD is
  the planned primary offline interchange container.
- A **transport/protocol** frames data across a network and defines ordering,
  bounds, errors, and acknowledgements.

Draco can compress a mesh payload inside a container or network message. It
does not itself specify a time sequence, clock synchronization, reconnect
behavior, or playback buffering.

## Evaluation basics

Quality and systems performance are separate dimensions:

- geometric RMS/maximum error and PSNR;
- image PSNR/SSIM/LPIPS for rendered Gaussian views;
- codec payload bytes, container bytes, and wire bytes;
- encode/decode time and peak memory;
- capture-to-decode and capture-to-present latency;
- throughput, goodput, startup time, and drops at each queue.

Current shared example metrics compare vertex sets with nearest-neighbor
point-to-point or point-to-plane distances in both directions. They are useful
cross-codec baselines, but they are not continuous, area-weighted surface
metrics and they do not register or align inputs.
