# Open4D — Python API Demo

This walkthrough showcases what the new package API makes possible: importing
Open4D as a normal Python module, a flat lazily-loaded namespace, native
container I/O, the `MeshSequence` 4D abstraction, and a uniform
compression-codec interface (N4MC / TSMC / TVMC) with per-stage timing.

Every snippet below was run against the repo; representative outputs are shown.

> **Environment:** the neural/GPU pieces use the `tsmc` conda env
> (`torch 2.7 + CUDA`, `open3d`, `point_cloud_utils`, `trimesh`). From a source
> checkout, run from the repo root or `pip install -e .`.

---

## 1. Open4D imports as a module

Before, `open4d/__init__.py` was empty. Now:

```python
import open4d as o4d

o4d.__version__          # '0.1.0'
```

The import is **cheap and lazy** — it touches no optional or heavy dependency.
You can `import open4d` on a box with no GPU, no PyQt, no Draco, and it still
works; the heavy bits load only when you reach for them (PEP 562 `__getattr__`).

```python
import sys, open4d as o4d
assert "torch"   not in sys.modules   # not imported by `import open4d`
assert "PyQt6"   not in sys.modules
assert "DracoPy" not in sys.modules
```

---

## 2. A flat public namespace

Reach everything through short attributes instead of the internal folder layout:

```python
import open4d as o4d

o4d.io          # native container readers/writers   (numpy-only, always there)
o4d.core        # 4D data structures (MeshSequence)
o4d.modules     # compression pipelines / codecs
o4d.metrics     # quality metrics
o4d.player      # interactive viewers (optional GUI extras)
o4d.tools       # CLI asset authoring helpers
```

Common classes are **hoisted to the top level**, and resolve to the same object
as their submodule:

```python
o4d.O4DMeshReader is o4d.io.O4DMeshReader   # True
o4d.MeshSequence is o4d.core.MeshSequence   # True
```

Optional subpackages fail *gracefully* with an actionable message rather than a
raw `ModuleNotFoundError`:

```python
import open4d.player
# ImportError: open4d.player requires the GUI extras (PyQt6, pyqtgraph).
#              Install them with:  pip install PyQt6 pyqtgraph
```

---

## 3. Native container I/O (`o4d.io`)

Read/write the Open4D `.o4d` container for time-varying geometry:

```python
import numpy as np, open4d as o4d

v = np.random.rand(20, 3).astype(np.float32)
f = np.array([[0, 1, 2], [2, 3, 4]], dtype=np.uint32)

# write a 2-frame mesh clip
w = o4d.O4DMeshWriter("clip.o4d"); w.open()
w.write_keyframe(v,       f, timestamp=0.0)
w.write_keyframe(v * 2.0, f, timestamp=0.033)
w.close()

# read it back
r = o4d.io.O4DMeshReader("clip.o4d"); r.open()
print(list(r.iter_frames()))          # [(0, 0.0), (1, 0.033)]
verts, faces, t = r.get_frame(0)
r.close()
```

Point-cloud and Draco-compressed point-cloud codecs are exposed the same way
(`O4DPointCloud*`, `O4DDracoPointCloud*`). The Draco pair loads lazily, so
`import open4d.io` works even without the optional `DracoPy` package installed.

---

## 4. The `MeshSequence` 4D abstraction (`o4d.core`)

The core data structure promised by the design: an ordered, timestamped list of
triangle-mesh frames. It behaves like a container and defines the standard 4D
contract algorithms build on.

```python
import numpy as np
from open4d.core import MeshSequence

# build in one shot (faces shared across frames = fixed topology)
seq = MeshSequence.from_frames(
    vertices=[verts_t0, verts_t1, verts_t2],
    faces=faces,
    timestamps=[0.0, 0.5, 1.0],
    name="prism",
)

# ...or incrementally (chainable)
seq = (MeshSequence(name="prism")
       .append(verts_t0, faces, timestamp=0.0)
       .append(verts_t1, faces))          # timestamp defaults to frame index
```

It is sized, indexable, sliceable, and iterable:

```python
len(seq)                 # 3
seq[0]                    # MeshFrame(index=0, t=0, V=6, F=8)
seq[-1].timestamp         # 1.0
sub = seq[1:]             # a NEW MeshSequence with 2 frames
for frame in seq:
    frame.vertices        # (N,3) float32, C-contiguous
    frame.faces           # (M,3) uint32, C-contiguous
```

Sequence-level properties:

```python
seq.num_frames            # 3
seq.timestamps            # array([0. , 0.5, 1. ])
seq.duration              # 1.0
seq.is_topology_constant()# True
seq.faces                 # shared connectivity (valid when topology is constant)
repr(seq)                 # MeshSequence('prism', frames=3, duration=1, fixed-topology)
```

**Layout contract.** Vertices are always coerced to `float32 (N,3)` and faces to
`uint32 (M,3)`, C-contiguous — the exact memory layout the eventual native (C++)
buffer will expose. Bad shapes are rejected at ingest:

```python
seq.append(np.zeros((5, 2)), faces)   # ValueError: vertices must have shape (N, 3)
```

**Round-trips through `.o4d`:**

```python
seq.to_o4d("prism.o4d")
loaded = MeshSequence.from_o4d("prism.o4d")   # geometry + timestamps preserved
```

### Swappable storage backend (ready for the native buffer)

Storage sits behind a tiny `FrameStore` interface. The default is numpy-backed;
a future native store implements the same four methods and returns numpy views
onto a C++ buffer — **nothing above it changes**. You can drop in your own today:

```python
from open4d.core import MeshSequence, FrameStore

class DictStore(FrameStore):
    def __init__(self):        self._d = {}; self._n = 0
    def __len__(self):         return self._n
    def append(self, v, f, t): self._d[self._n] = (v, f, t); self._n += 1
    def vertices(self, i):     return self._d[i][0]
    def faces(self, i):        return self._d[i][1]
    def timestamp(self, i):    return self._d[i][2]

seq = MeshSequence(store=DictStore())         # same public API, different backend
seq.append(verts, faces, 0.0)
seq[0].vertices.dtype                          # float32 — validation still applied
```

---

## 5. Compression codecs (`o4d.modules`)

All three Open4D algorithms are now exposed through **one uniform interface**.
Discover and instantiate by name:

```python
from open4d.modules import list_codecs, get_codec

list_codecs()                 # ['n4mc', 'tsmc', 'tvmc']
codec = get_codec("n4mc")     # -> N4MCCodec
```

### Probe the environment first

Every codec reports what it can (and cannot) run here — no guessing:

```python
print(get_codec("n4mc").available())
# Capability(available)
#   note:    CUDA available (2 GPU)
#   note:    offset field is zero-init (optimize_tsdf_offset needs nvdiffrast)
```

### Run N4MC end-to-end — from a `MeshSequence`

This is the headline: feed the neural auto-decoder an in-memory `MeshSequence`
and get back compressed weights, latent codes, and reconstructed meshes. The
adapter voxelizes each frame to the TSDF grid, trains the real
`QuantGeneratorV2`, and collects the outputs.

```python
import open4d as o4d
from open4d.core import MeshSequence

seq = MeshSequence.from_frames(vertices_list, faces, timestamps, name="prism-demo")

result = o4d.modules.get_codec("n4mc").compress(
    seq,
    workdir="/tmp/n4mc_run",
    n_epoch=100,           # >=100 to also export latent codes + reconstructed meshes
    voxel_grid_res=127,
)
```

Every run returns a structured `CompressionResult` with **per-stage wall-clock
timing**:

```python
print(result.stage_table())
# [ok ] voxelize (mesh -> TSDF)         21.90s
# [ok ] train (QuantGeneratorV2)       145.67s
# [ok ] collect artifacts                0.00s
# ----------------------------------------------------
# TOTAL                                167.57s

result.metrics       # {'final_loss': 0.946, 'num_frames': 3.0}
result.artifacts     # {'encoder_compressed.pt': '...1.0MB',
                     #  'decoder_compressed.pt': '...3.3MB',
                     #  'code_embed_feature_0000.npy': ...,   # latent code (1,8,8,8,16)
                     #  'rec_mesh_rec_mesh_0000.obj': ...}    # reconstructed geometry
```

And you can close the loop — load the reconstruction straight back into a
`MeshSequence`:

```python
recon = result.to_mesh_sequence()
print(recon)         # MeshSequence('n4mc:reconstruction', frames=3, ...)
```

A ready-to-run version of exactly this lives at
[`scripts/run_n4mc_adapter.py`](scripts/run_n4mc_adapter.py).

### TSMC / TVMC — same interface, `dry_run` to inspect the plan

TSMC and TVMC drive multi-stage `.NET` + Draco pipelines. The same
`compress(...)` entry point orchestrates their real stages with timing; a
`dry_run=True` returns the ordered stage plan without executing:

```python
res = get_codec("tsmc").compress("answering", dry_run=True)
for s in res.stages:
    print(s.name, "->", s.detail)
# 1_reference_center       -> [plan] (tsmc) python ./get_reference_center.py --dataset answering ...
# 2_transformation         -> [plan] (tsmc) python ./get_transformation.py ...
# 3_tvmeditor_deform       -> [plan] (tvm-editing) TVMEditor.Test ... answering 1 0 9 ...
# 4_extract_reference_mesh -> ...
# 5_tvmeditor_deformback   -> ...
# 6_displacements          -> ...
# 7_compress_displacements -> ...
# 8_evaluation             -> ...   (8 stages)

get_codec("tvmc").compress("dancer", first_index=5, last_index=14, dry_run=True)
# 9 stages, incl. ARAP build + track up front and Draco encode at the end
```

Dropping `dry_run` runs the real pipeline (requires the built `.NET`/Draco
toolchains — see each codec's `available()`).

---

## 6. Tests

Two standalone-runnable suites (also pytest-compatible) ship with the repo:

```bash
python tests/test_core_mesh_sequence.py     # 15 passed  (facade + MeshSequence)
python tests/test_modules_adapter.py        #  9 passed  (registry, capability, dry-run plans)
```

---

## 7. Honest scope

- **N4MC** runs for real end-to-end. The `optimize_tsdf_offset` sub-voxel
  *offset* refinement is **not** run (it needs `nvdiffrast` + a CUDA compiler and
  a `render` module absent from the repo); the offset field is zero-init and the
  SDF field is the network's real signed-distance init. This is surfaced as a
  `Capability` note, not hidden.
- **TSMC / TVMC** adapters are wired and orchestrate the real pipeline stages;
  the snippets above were validated via `dry_run`. A full run requires the built
  `.NET` (`tvm-editing`, ARAP `Client`) and Draco toolchains.
- N4MC's `train_quant.py` does top-level `import kaolin` / `import py7zr` on code
  paths this adapter never exercises; small shipped stubs
  (`open4d/modules/pipelines/_n4mc_stubs/`) satisfy those imports.

---

## Quick reference

| Task | API |
|---|---|
| Import the library | `import open4d as o4d` |
| Read/write `.o4d` | `o4d.O4DMeshReader` / `o4d.O4DMeshWriter` |
| Build a 4D sequence | `o4d.core.MeshSequence.from_frames(...)` / `.append(...)` |
| Load/save a sequence | `MeshSequence.from_o4d(path)` / `seq.to_o4d(path)` |
| Custom storage backend | subclass `o4d.core.FrameStore` |
| List codecs | `o4d.modules.list_codecs()` |
| Get a codec | `o4d.modules.get_codec("n4mc" | "tsmc" | "tvmc")` |
| Check what can run | `codec.available()` |
| Compress | `codec.compress(source, ...)` → `CompressionResult` |
| Inspect stage plan | `codec.compress(source, dry_run=True)` |
| Reconstruction as sequence | `result.to_mesh_sequence()` |
