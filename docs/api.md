# Python API

The public API loads, saves, unloads, and visualizes whole finite triangle-mesh
sequences independently of their storage format:

```python
import open4d

with open4d.load("capture.usdc") as sequence:
    open4d.save(sequence, "capture.o4d")
    open4d.visualize(sequence)

# A path can go straight to the lazy viewer; it is closed when the window exits.
open4d.visualize("capture.o4d")
```

`.usd`, `.usda`, `.usdc`, and `.usdz` are OpenUSD interchange containers.
`.o4d` and the registered codec suffixes are codec artifacts. Both carry whole
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

OpenUSD is the public interchange container. `--pack-usd out.usdc` packs any
source into one compressed `.usdc` file carrying the frame rate, the key-frame
index, and per-frame streams alongside the geometry — see the
[visualization guide](../examples/visualization/README.md#the-openusd-container).

## Codecs

Five lossless, in-process reference codecs are included: `raw`, `deflate`,
`bzip2`, `lzma`, and byte-level `rle` (`npz` remains the default DEFLATE alias).
They share a safe NumPy-array container so they compare storage strategies, not
research geometry models.

Source checkouts register in-process adapters for `klt`, `n4mc`, `qndf`, and
`qndf-int8`; the lightweight wheel omits them until their provenance review is
complete. Open4D's separate `temporal-delta` and `temporal-pca` experiments are
not the repository's TVMC or TSMC pipelines. The V-DMC adapters do not execute
shell scripts, but they do invoke configured native encoder and decoder
processes once per sequence. Callers can also register another
`open4d.codec.Codec`.

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

### Test coverage

Normal CI runs dependency-complete CPU encode/fresh-decode contracts for KLT,
N4MC, QNDF, and QNDF-int8. The larger two-format Rafa quality/export matrix is
an additional CUDA acceptance test gated by `OPEN4D_TEST_RESEARCH_CODECS=1` and
`OPEN4D_RAFA_DATASET`; it is not presented as part of ordinary CI coverage.
