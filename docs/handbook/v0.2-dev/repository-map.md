# Repository and feature map

This map describes what is physically present at the audited revision and how
each area should align with the platform.

## Top-level map

```text
Open4D/
├── open4d/                 shared package plus research implementations
│   ├── core/               NumPy geometry/frame/provider/sequence model
│   ├── torch_ops/          small Torch mesh/I/O replacements
│   ├── codecs/             eight codec or reference-codec areas
│   └── reconstruction/     RGB-D, QUEEN, 3DGStream, gs_tools placeholder
├── examples/visualization/ working loaders, USD, metrics, renderers, viewers
├── integrations/           Open3D conversion and Unity TVMC playback
├── apps/                   end-to-end application scaffold (README only)
├── scripts/                setup, artifact, and data helper scripts
├── docs/                   design, policy, media, and this handbook
├── pyproject.toml          lightweight package metadata and extras
├── environment.yml         shared Python 3.12 codec environment
├── requirements-*.txt      shared codec and machine-specific GPU dependencies
└── README.md               project overview and setup
```

Local `build/`, `open4d.egg-info/`, `.pytest_cache/`, and `.context/` directories
are generated or workspace state, not product architecture. Do not document or
import from them.

## Feature alignment matrix

| Area | What it does | Inputs | Outputs | Main dependencies | Who uses it | Open4D alignment |
| --- | --- | --- | --- | --- | --- | --- |
| `open4d/core` | validates meshes and represents finite time sequences lazily | NumPy-compatible arrays, provider objects | `TriangleMesh`, `Frame`, `Sequence` | NumPy | all future loaders/codecs/metrics | canonical shared boundary; preserve |
| `open4d/torch_ops` | replaces a handful of PyTorch3D mesh and OBJ functions | Torch tensors, OBJ | tensors / local `Mesh` | PyTorch | N4MC, QNDF | dependency-reduction utility, not the public data model |
| `examples/visualization` | opens sequences, writes USD, compares and displays frames | mesh folders, mesh files, USD | `Sequence`, CSV, USD, GIF/window | optional SciPy, PyQt6, OpenGL, OpenUSD, trimesh | contributors and codec evaluation | working prototype to promote into public APIs |
| `integrations/open3d` | converts frame-like values to Open3D mesh/point cloud | Open4D geometry or compatible arrays | Open3D object | Open3D 0.19 | visualization/processing clients | external adapter; time stays in Open4D |
| `integrations/unity` | decodes and plays TVMC-style subsequences in Unity/XR | zipped reference/basis/trajectory files | Unity meshes over time | C++, Eigen, C#, Unity | TVMC XR playback | isolated legacy/specialized consumer; needs disposition |
| `apps` | intended home for complete vertical slices | shared APIs plus codec | documented application artifacts | component-dependent | end users/reviewers | scaffold until Draco app exists |
| `scripts` | initializes Draco copies, fetches checked artifacts, reserves data setup | URLs/checksums/repo state | local dependencies/artifacts | shell, CMake, Git | setup and reproducibility | keep checksum and manifest policy; add validation |
| `docs` | architecture, policies, onboarding | repository evidence | versioned guidance | none | all contributors | source of truth only when tied to revision/evidence |

## Codec map

| Codec | Basic idea | Native input | Native output | Where it aligns next |
| --- | --- | --- | --- | --- |
| Draco | quantize/predict one triangle mesh and entropy-code it | per-frame OBJ | `.drc`, decoded OBJ, evaluation files | first strict encode/decode `Sequence` vertical slice |
| KLT | learn a PCA/KLT basis for TSDF blocks and quantize coefficients | TSDF `.npz` volumes | compressed coefficient intermediates and reconstructed mesh | package basis/mean and expose decoded meshes |
| N4MC | neural latent representation of TSDF volumes, then marching cubes | meshes converted to TSDF or TSDF datasets | checkpoint/latent packs/reconstructed meshes | define complete artifact; adapt reconstructed mesh provider |
| QNDF | coarse SSP mesh plus neural implicit displacement decoder | OBJ mesh | model/coarse mesh/reconstruction/metrics | package all decode dependencies; adapt result frames |
| QNDF-INT8 | dynamic INT8 quantization experiment on QNDF linear layers | existing SSP pair | FP32/INT8 TorchScript and meshes | retain as bounded experiment, not general codec API |
| TVMC | track a reference shape and compress temporal displacements | numbered OBJ sequence | reference/deformation/displacement artifacts and decoded OBJ | first temporal `Sequence` adapter; independent decode |
| TSMC | split static/dynamic scene, track dynamic reference, compress motion | scene mesh sequence and SAM-derived masks | static/dynamic/reference/displacement results | reuse TVMC boundaries after shared side information exists |
| V-DMC | MPEG reference test model for video-based dynamic mesh coding | test-model formats | standard bitstreams/decoded assets | initialize/build upstream, then add wrapper |

See the [codec guide](codecs.md) for the beginner-level pipeline and limitations
of each method.

## Reconstruction and network map

| Area | What it does | Native input | Native output | Where it aligns next |
| --- | --- | --- | --- | --- |
| `reconstruction/rgbd` Python | receives two synchronized RGB-D cameras, aligns point clouds, fuses CUDA TSDF meshes, serves Open3D WebRTC | OBP1 RGB JPEG + compressed depth, calibration | latest PLY point cloud/mesh and JSON report | finalized capture replay as finite changing-topology `Sequence` |
| `reconstruction/rgbd` C++ | original camera/playback reconstruction and mesh transport | K4A-compatible camera or recording | PLY/OBJ/texture/Draco, MRD messages, stage metrics | keep as hardware/native lane; add protocol golden tests |
| OBP1 | bounded, CRC-protected paired RGB-D input transport with ACK | compressed color/depth payloads and timing metadata | validated paired capture frames | preserve framing; test reconnect/gaps/duplicates and recording |
| MRD1/MRD2 | one-shot raw or Draco mesh/texture transfer | reconstructed mesh and texture | one validated message | retain as fixtures/reference formats |
| MRD3 | continuous Draco/JPEG mesh frames | successive reconstruction results | live frame messages and receive statistics | label experimental; supersede only after live contract |
| QUEEN | trains and quantizes dynamic 3D Gaussians for free-viewpoint video | calibrated multi-view images/COLMAP | initial/delta Gaussian artifacts and rendered views | Ryan-owned handoff contract only |
| 3DGStream | initial 3DGS plus per-frame neural transformation cache/new Gaussians | calibrated multi-view images and initial 3DGS | per-frame NTC/new Gaussian data and renders | Ryan-owned handoff contract only |
| `gs_tools` | currently a one-heading placeholder | none | none | correct stale descriptions; do not resurrect deleted design |

See [reconstruction and streaming](reconstruction-streaming.md) for data and
protocol details.

## Important root configuration

- `pyproject.toml` advertises a NumPy-only base and optional `player`, `usd`,
  `tools`, and `open3d` extras. Its current namespace auto-discovery is too broad
  for a safe lightweight wheel and is a P0 item.
- `environment.yml` plus `requirements-codecs.txt` define one Python 3.12 codec
  environment; `.NET 10`, CMake, CUDA extensions, camera SDKs, and Unity remain
  machine/toolchain concerns.
- `.gitmodules` pins three Draco copies, QNDF's libigl, TSMC's SAM3, and MPEG
  V-DMC. All were uninitialized in the audited checkout.
- `.gitignore` now names expected local data/output locations, but large
  historical artifacts are already tracked and need a provenance-preserving
  migration.
- `docs/artifacts.md` defines checksum and manifest expectations; it should be
  treated as policy even before automated enforcement exists.

## Where a new contribution belongs

| Contribution | Preferred home |
| --- | --- |
| finite geometry/sequence invariant | `open4d/core` |
| general supported file/container loader | future `open4d/io` |
| general versioned metric | future `open4d/metrics` |
| method-specific training/encoding code | its codec/reconstruction directory |
| conversion to an external framework | `integrations/<framework>` |
| complete reproducible user workflow | `apps/<workflow>` |
| teaching, inspection, visualization | `examples/` |
| durable architecture/policy | `docs/` plus tests where enforceable |

Avoid adding another copy of mesh parsing, metric code, or directory ordering to
a codec. First promote and test the shared behavior, then have the codec call it.
