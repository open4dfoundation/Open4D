# Codec guide

A codec should turn a declared input into a self-contained encoded artifact and
turn that artifact back into a declared output. In Open4D, a complete adapter
will consume a finite `Sequence`, decode lazily through a provider, use shared
metrics, and emit a run manifest. None of the current codec trees crosses that
whole boundary at the audited revision.

## Comparison at a glance

| Codec | Representation it compresses | Time model | Training | Natural strength | Main Open4D gap |
| --- | --- | --- | --- | --- | --- |
| Draco | triangle mesh attributes/connectivity | each frame independently in current scripts | none | simple, mature CPU baseline | no sequence artifact/provider/manifest vertical slice |
| KLT | blocks of a TSDF volume | basis learned from selected frames | linear PCA/SVD | interpretable classical baseline | basis/mean and full decoder state are not one artifact |
| N4MC | TSDF volume latent plus neural decoder | dataset/sequence training, then per-volume reconstruction | neural | compact learned volumetric representation | incomplete entropy/artifact accounting and no sequence adapter |
| QNDF | coarse mesh plus implicit displacement field | current workflow trains each mesh/frame | neural | preserves detailed surface from coarse geometry | decode bundle omits required coarse/normalization/model context |
| QNDF-INT8 | QNDF decoder linear layers | isolated single-model comparison | post-training dynamic quantization | answers an FP32-versus-INT8 question | experiment, not a general temporal codec |
| TVMC | tracked reference mesh plus per-vertex temporal displacement | group/subsequence with reference | classical tracking/linear compression | explicit temporal correspondence | decoder still depends on encoder-side information; no provider |
| TSMC | static scene plus tracked dynamic mesh | groups of scene frames | segmentation plus tracking/compression | avoids repeatedly coding static scene | side information/artifact and shared adapter incomplete |
| MPEG V-DMC | dynamic mesh encoded through the MPEG test model | standard-defined temporal coding | none in the neural sense | standards reference and conformance experiments | submodule uninitialized; no Open4D wrapper/verification |

“Bitrate” comparisons are valid only if they count everything required to
decode: model weights, basis/mean, reference/coarse mesh, normalization,
topology, entropy tables, and container overhead when appropriate.

## Draco

Draco is Google's general 3D geometry codec. It quantizes and predicts geometry
attributes/connectivity and entropy-codes them. Open4D uses it as the least
complex reference path and as a building block inside other pipelines.

Current standalone flow:

```text
folder of OBJ frames
  -> draco_encoder for each frame/quantization setting
  -> .drc files
  -> draco_decoder
  -> decoded OBJ files
  -> codec-local bitrate, D1/D2 PSNR, depth/color SSIM evaluation
```

The source is a pinned but audit-time-uninitialized submodule. Setup scripts
build command-line binaries with CMake. The Python wrapper is useful but is not
a sequence codec: it has no common artifact schema, stored frame timing, lazy
decoded provider, run manifest, or fresh-process golden test through Open4D.

Draco is the first vertical slice because it needs no GPU training and already
has a real encode/decode binary boundary. The complete app must encode a tiny
licensed sequence, store per-frame identity/time/configuration/hash/size, hide
the source and intermediates, decode in a fresh process, expose a lazy
`Sequence`, compare using the shared metric, and emit a manifest. Codec payload
bytes must be separate from filesystem/container/wire bytes.

## KLT

The Karhunen–Loève Transform is PCA applied as a compression transform. Similar
TSDF blocks are treated as vectors. SVD learns a mean `mu` and basis `P`; target
blocks are projected onto the most useful basis vectors, coefficients are
quantized and packed, then inverse-projected. Marching cubes converts the
reconstructed TSDF back to a mesh.

```text
TSDF .npz frames
  -> overlapping training blocks -> SVD basis + mean
  -> non-overlapping target blocks -> coefficients
  -> quantization + zstd/zip size estimate
  -> inverse transform -> TSDF -> marching-cubes mesh
```

It is a valuable non-neural baseline and documents operating points, but its
current run is an experiment script rather than an independently decodable
artifact. The basis, mean, volume/block metadata, coefficient quantization, and
packed symbols must be serialized together. A real decoder should take only
that bundle and expose reconstructed mesh frames. GPU memory can still be a
constraint because the training-block extractor materializes overlapping
blocks.

## N4MC

N4MC is the repository's neural TSDF mesh-compression line. A surface is
converted to a TSDF, an encoder produces a compact latent tensor, quantization
reduces precision, and a neural decoder reconstructs the field. Marching cubes
extracts a mesh. The tree contains both legacy experiments and a newer modular
`data/models/losses/training/evaluation` path with YAML configuration,
validation, reconstruction, latent packs, and a managed basketball runner.

```text
mesh -> normalized TSDF (+ optional offsets)
     -> neural encoder -> quantized latent -> neural decoder
     -> reconstructed TSDF -> marching cubes -> restored mesh
```

Useful current features include narrow-band/sign-aware losses, saved latent
packs, checkpointed training, split-aware evaluation, and restoration to source
coordinates. However, a checkpoint, latent pack, normalization, model
configuration, and reconstruction parameters are not yet specified as one
self-contained codec artifact. The rate path is still an entropy proxy rather
than a complete interoperable bitstream, and decoder model bytes must count
toward rate. The SSIM objective and its test/implementation contract also need
reconciliation.

The first adapter should leave TSDF training datasets alone and expose the
existing reconstructed marching-cubes meshes as a changing-topology provider.
A first-class volume type should come later, based on demonstrated needs.

## QNDF

Quantized Neural Displacement Fields starts with SSP preprocessing: simplify a
mesh to a coarse surface, subdivide it, and project the subdivided vertices to
the original. A small implicit neural representation learns displacement from
the coarse/subdivided surface to detailed geometry. Network values can then be
quantized and entropy-coded.

```text
OBJ -> normalize -> SSP coarse mesh -> subdivide/project training pair
    -> train implicit displacement decoder
    -> quantized model + reconstruction -> restore source coordinates
```

The repository includes a disconnected-component preprocessing path and a
resumable basketball runner. Its pinned libigl dependency was uninitialized in
the audit. QNDF is GPL-3.0 licensed, which is one reason package/release scope
must be resolved before distribution.

A complete artifact must include the coarse/reference geometry, subdivision
and sampling contract, normalization transform, architecture/configuration,
trained/quantized parameters, and any entropy metadata. Today those pieces are
spread across working directories. The first adapter should report one-frame
and sequence-run outputs without pretending independently trained frames form a
temporal codec.

## QNDF-INT8

This sibling is intentionally narrower. It takes an existing SSP mesh pair,
trains the original QNDF architecture, applies PyTorch dynamic INT8
quantization to each `Linear` layer, and saves reloadable FP32/INT8 TorchScript
models, reconstructions, artifact sizes, and quality metrics.

That is a completed experiment question—whether the chosen dynamic INT8
container changes size/quality and reloads—not a complete QNDF artifact or
sequence codec. Preserve the isolation and its result manifest. Do not merge it
into the main QNDF path until an explicit general format requires it.

## TVMC

Time-Varying Mesh Compression is a multi-stage temporal pipeline. It tracks
volume centers through a sequence, deforms frames into a reference pose,
extracts a self-contact-free reference mesh, deforms the reference forward,
computes fine displacements, and compresses trajectories. Draco participates in
reference geometry handling. Python orchestrates NumPy/Open3D work; .NET
projects perform tracking/editing.

```text
numbered OBJ sequence
  -> volume-center tracking
  -> reference centers + center transforms
  -> frames deformed to reference -> reference extraction
  -> reference deformed to each frame
  -> vertex displacements -> temporal coefficient/trajectory compression
  -> reconstructed OBJ sequence + metrics
```

The setup, sample basketball input, dry-run/resumable stage runner, cached
intermediates, checked subprocess execution, and optimized CPU evaluation paths
are valuable and should be preserved. TVMC itself is covered by a Northeastern
non-commercial, non-transferable, non-sublicensable, no-redistribution license;
release and contribution rules need explicit review.

TVMC is the first temporal adapter after Draco. It should consume the public
directory `Sequence`, preserve existing CLI/files/resume behavior, serialize
the information required to restore displacement-to-vertex correspondence,
decode without original encoder-side displacement values, and expose a finite
lazy provider. It must declare changing topology before fitting and fixed
reference topology/correspondence at the correct later boundary.

## TSMC

Time-varying 4D Scene Mesh Compression extends the reference/deformation idea to
scenes. SAM3-based segmentation separates static and dynamic geometry so the
unchanged environment need not be recoded every frame. The dynamic portion is
volume-tracked, represented with a reference mesh and motion/displacement data,
compressed, and recombined for evaluation.

```text
scene mesh sequence + rendered segmentation views
  -> static/dynamic separation
  -> dynamic volume-center tracking and reference extraction
  -> deformation + displacement compression
  -> reconstructed dynamic + static scene -> evaluation
```

The imported project contains paper assets, tracked sample data, .NET/C++/Python
stages, Blender-only conversion, SAM3 integration, and its own Draco copy. It is
useful standalone research code but not a supported Open4D codec. Required
entropy-model/side information must be serialized, and decoding must start from
the declared encoded artifact instead of predecoded arrays. After TVMC's
directory/reference contracts are proven, TSMC should reuse them rather than
maintain duplicate mesh/displacement conversion.

The advertised VR decoder is not integrated; the current Unity path is TVMC
specific.

## MPEG V-DMC

V-DMC is the MPEG video-based dynamic mesh coding reference test model. A test
model is primarily for standards experiments and conformance, not a polished
library API. It brings its own encoder, decoder, metrics, formats, build system,
and tests.

At the audited revision the `open4d/codecs/vdmc` submodule was pinned at
`ecffe4212e5e956761c4fa14a17c453ae916b0b1` but uninitialized, so no build or
runtime claim is verified here. Initialize and verify upstream first, including
the known macOS build work tracked by the project, then add an Open4D wrapper
that records exact test-model revision/configuration and exposes decoded
geometry through the shared boundary.

## Cross-codec acceptance contract

Every adapter is done only when it passes the same shape of test:

```text
licensed tiny fixture
  -> encode
  -> retain only declared artifact
  -> start fresh decoder process
  -> decoded finite Sequence
  -> shared versioned comparison
  -> validated run manifest and exact byte accounting
```

Codec-local scientific metrics remain useful and should be recorded alongside,
not substituted for, the cross-codec baseline.
