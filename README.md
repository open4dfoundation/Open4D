# Open4D
![Python](https://img.shields.io/badge/python-3.10%20%7C%203.11%20%7C%203.12%20%7C%203.13-blue?logo=python&logoColor=white) ![Ubuntu](https://img.shields.io/badge/Ubuntu-24.04-E95420?logo=ubuntu&logoColor=white)

Open4D is a research repository for representing, compressing, evaluating, and
playing time-varying 3D geometry. It brings several mesh-compression systems,
a shared 4D data model, a sequence viewer, and per-codec evaluation scripts
into one workspace for XR, teleoperation, digital-twin, robotics, and graphics
research.

> **Project status:** Open4D is under active development. The individual
> codecs and domain components contain working pipelines, while the shared 4D
> data model and a common metrics API are still evolving.

<p align="center">
  <img src="docs/assets/open4d-ecosystem.png" width="90%" alt="Open4D ecosystem">
</p>

## Repository layout

```text
Open4D/
├── open4d/
│   ├── core/          shared temporal geometry and sequence abstractions
│   ├── codecs/
│   │   ├── draco/
│   │   ├── klt/
│   │   ├── n4mc/
│   │   ├── qndf/
│   │   ├── qndf_int8/
│   │   ├── tsmc/
│   │   ├── tvmc/
│   │   └── vdmc/
│   └── reconstruction/
│       └── rgbd/
├── integrations/
│   ├── open3d/
│   └── unity/
├── examples/
│   └── visualization/ runnable sequence loading, visualization, and
│                      reference-versus-decoded comparison
├── apps/              placeholder for end-to-end pipelines; a README only
├── scripts/           repository-level setup utilities
└── docs/              architecture and repository policies
```

### Codecs, reconstruction, and integrations

- **N4MC** — neural TSDF-based mesh compression, including a newer modular
  codec under its `data`, `models`, `losses`, `training`, and `evaluation`
  packages.
- **Quantized Neural Displacement Fields (QNDF)** — static mesh compression
  using an SSP coarse mesh and an implicit displacement decoder.
- **TVMC** — a Python, .NET, and Draco pipeline for tracked time-varying mesh
  compression. It includes setup and resumable pipeline scripts.
- **TSMC** — scene-mesh compression with optional SAM-based static/dynamic
  separation, ARAP volume tracking, deformation, displacement compression, and
  evaluation.
- **Unity integration** — a C++ decoder backend and C# Unity front end for
  playback on XR targets.
- **Draco** — Google Draco mesh-compression baseline. Wraps the vendored
  `draco_encoder`/`draco_decoder` binaries into a per-frame encode/decode/eval
  pipeline for benchmarking against the neural codecs.
- **KLT** — Karhunen–Loève Transform baseline that compresses TSDF voxel blocks
  with a learned linear basis and quantized coefficients, reconstructing meshes
  via marching cubes.
- **4D reconstruction** — synchronized multi-camera RGB-D ingestion, calibrated
  point-cloud fusion, CUDA TSDF mesh reconstruction, and live browser playback.
  It includes both the original native reconstruction code and the Python
  two-camera streaming pipeline.
- **MPEG V-DMC test model** — the pinned MPEG reference implementation for
  video-based dynamic mesh coding. The `open4d/codecs/vdmc` submodule provides
  the standard's reference encoder, decoder, metric tools, and unit tests; it is
  separate from Open4D's TVMC research pipeline.

Each component has its own README and environment. See
[Requirements](#requirements) for what each one adds on top of the baseline.

## Requirements

One baseline covers the repository itself — the shared data model and
`examples/visualization`:

| | |
|---|---|
| Python | 3.10–3.13 |
| Operating system | macOS, Linux, or Windows |
| CPU | Any x86-64 or arm64; no particular core count |
| GPU | Not required. The viewers open a real OpenGL window, so a graphical session is needed even for `--save` |
| Memory | Roughly 1 MB of RAM per frame of playback. |
| Disk | About 1.5 GB for a clone with submodules initialized|

`pip install -e .` needs only NumPy, and reads `.obj` and `.ply` with no further
dependencies. Extras add optional readers and viewers — see
[Installation](#installation). The comparison program additionally needs SciPy,
which the `[player]` extra installs, for its nearest-neighbour search — the same
`cKDTree` query TVMC's own evaluation uses.

### One environment for the codecs

Every codec runs in a single environment, described by
[`environment.yml`](environment.yml) at the repository root:

```bash
conda env create -f environment.yml
conda activate open4d
pip install -e .
```

That is Python 3.12, NumPy 1.26.4, Open3D 0.19, PyTorch 2.7.0, and .NET 10 — one
of each, where there used to be three Python versions, two Open3D versions, two
PyTorch versions, and three .NET SDKs. The pins themselves live in
[`requirements-codecs.txt`](requirements-codecs.txt), which `environment.yml`
installs; it lists direct dependencies only, so inside an existing Python 3.12
environment `pip install -r requirements-codecs.txt` is equivalent.

One trap worth naming, because its error message points the wrong way. The .NET
projects target `net10.0`, and a distribution's own `dotnet` under
`/usr/lib/dotnet` will shadow a newer SDK in `~/.dotnet` on `PATH`. The build
then fails with `NETSDK1045: The current .NET SDK does not support targeting
.NET 10.0`, which reads as a missing SDK when the SDK is usually installed and
merely second in line. Check with `dotnet --list-sdks` before installing
anything. Downgrading the projects to `net9.0` is not the fix: .NET 9 left
support in May 2026, and moving off end-of-life targets is why they are on
`net10.0`.

Some codecs additionally need compiled extensions that pip cannot resolve from a
version number alone, because each is built against one exact PyTorch and CUDA
build. Those are optional and separate, with install commands in
[`requirements-gpu.txt`](requirements-gpu.txt):

| Extra | Needed by |
|---|---|
| `cupy-cuda12x` | `n4mc`, `tsmc` |
| `torch-scatter` | `n4mc` |
| `nvdiffrast` | `n4mc` |
| `kaolin` | `n4mc`, `klt` |

What each module needs beyond that shared environment:

| Module | Adds |
|---|---|
| `codecs/tvmc` | .NET 10 SDK, CMake; Homebrew macOS or Ubuntu |
| `codecs/tsmc` | .NET 10 SDK, SAM3, `cupy`; Ubuntu 24.04, tested against Meta Quest 3. `convert_to_std_obj.py` runs inside Blender, which supplies `bpy` |
| `codecs/n4mc` | All four GPU extras and an NVIDIA GPU — 24 GB holds only about two training frames at resolution 256 |
| `codecs/qndf`, `codecs/qndf_int8` | An NVIDIA GPU for training. Evaluation (`mesh_errors.py`) runs on CPU |
| `codecs/klt` | `kaolin` and an NVIDIA GPU; 24 GB is the same ceiling at resolution 128–256 |
| `codecs/draco` | A CMake build of the vendored Draco submodule. Open3D, pymeshlab, and OpenCV are for evaluation only |
| `codecs/vdmc` | The MPEG reference test model's own build requirements |
| `reconstruction/rgbd` | Two hardware-synchronized RGB-D cameras, a Windows capture host, and an Ubuntu host with Python 3.10+, an NVIDIA GPU, and CUDA-enabled Open3D. Its legacy C++ pipeline additionally wants CUDA 12.x, Open3D 0.18, OpenCV, Eigen, jsoncpp, Draco, CMake, Ninja, and either the Azure Kinect SDK or the Orbbec K4A wrapper |
| `integrations/unity` | Unity, plus a C++ toolchain to rebuild the backend for anything other than the prebuilt macOS and Android/Quest 3 plugins |

Open3D ships no 3.13 wheels, capping .[open3d] and the codecs at 3.12.

### RGB-D capture on Windows

The RGB-D capture host is Windows and only encodes and forwards frames, so it needs no NVIDIA GPU: just the camera vendor SDK (tested: Orbbec K4A Wrapper 1.10.5, SDK 1.10.28, two Femto Bolts), both cameras on separate USB 3 ports with a sync hub, and an OpenSSH client. Close Orbbec Viewer first or the sender fails with Hardware MFT failed to start. 5 synchronized pairs/s held over Wi-Fi and VPN; 15 did not.

Calibration layout and the step-by-step session walkthrough are in
[`open4d/reconstruction/rgbd/README.md`](open4d/reconstruction/rgbd/README.md),
which covers how to run the pipeline and leaves requirements to this page.

## Installation

Clone with submodules to obtain the pinned Draco, SAM3, and MPEG V-DMC source:

```bash
git clone --recurse-submodules https://github.com/open4dfoundation/Open4D.git
cd Open4D
```

For the lightweight core package:

```bash
python -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e .
```

Optional local tooling is available through extras:

```bash
python -m pip install -e ".[player]" # the example viewer (PyQt6 + pyqtgraph)
python -m pip install -e ".[usd]"    # OpenUSD containers
python -m pip install -e ".[tools]"  # trimesh, for extra mesh formats
python -m pip install -e ".[open3d]" # Open3D adapter; Python 3.12 or older
python -m pip install -e ".[all]"
```

These extras do not install the heavyweight codec environments. Use the setup
instructions inside the selected codec before running it.

If an existing clone is missing Draco, initialize and build all three copies —
the Draco baseline codec's own, plus TSMC's and TVMC's — with:

```bash
./scripts/setup_draco.sh
```

## Getting started

`examples/visualization/visualize_sequence.py` loads a 4D sequence, reports what it contains,
and animates it. Point it at your own data:

```bash
python -m pip install -e '.[player]'
python examples/visualization/visualize_sequence.py my_capture/ --info
python examples/visualization/visualize_sequence.py my_capture/
```

<p align="center">
  <img src="docs/assets/viewer_demo.gif" width="70%" alt="The Open4D sequence viewer playing a 10-frame mesh sequence">
</p>

Playback is our own PyQt6 window: drag to orbit, scroll to zoom, drag the slider
to scrub, space to pause, left/right to step a frame. `--save out.gif` writes an
animated GIF through the same renderer.

A source is either a folder holding one mesh file per frame — `.obj` and `.ply`
need no extra dependencies to read — or a single time-sampled USD file. `--info`
reports frame count, duration, topology and bounds without decoding geometry,
which is the quickest way to check a dataset loads.

Loading is one call, and frames are decoded on access:

```python
from frame_sources import open_sequence

with open_sequence("path/to/frames", fps=30.0) as sequence:
    print(len(sequence), sequence.duration, sequence.fps)
    mesh = sequence[0].geometry          # TriangleMesh: positions, triangles
```

OpenUSD is the container the example writes. `--pack-usd out.usdc` packs any
source into one compressed `.usdc` file carrying the frame rate, the key-frame
index, and per-frame streams alongside the geometry:

```bash
python -m pip install -e '.[usd]'
python examples/visualization/visualize_sequence.py my_capture/ --pack-usd out.usdc --info
```

The TVMC codec vendors 10 frames of a basketball player, useful for checking the
program runs before pointing it at your own data. See
[`examples/visualization/README.md`](examples/visualization/README.md) for that
command, the full format list, and the container layout.

## Comparing a codec against its reference

`examples/visualization/compare_sequences.py` measures one sequence against
another and shows both at once — the reference as geometry, the decoded mesh
coloured by its distance from it, in synchronized panes under one camera:

```bash
python examples/visualization/compare_sequences.py reference/ decoded/ --info
python examples/visualization/compare_sequences.py reference/ decoded/
```

Error is a nearest-neighbour distance, since a decoded mesh has its own vertex
count and connectivity: point-to-point by default, `--metric plane` for the MPEG
point-to-plane definition. Both are one-sided, so RMS, Hausdorff and PSNR are
reported in each direction and the symmetric figure is the worse of the two.
`--info` prints the per-frame table without opening a window, and `--csv` writes
it for a paper or a regression run.

This is codec-independent: both sides are read through the same loader, so
anything the viewer opens can be compared. It does not replace a codec's own
evaluation — the figures are per-vertex rather than area-weighted over faces, so
they compare codecs against a shared reference rather than substituting for a
metric tool that integrates over the surface.

## Reproducibility and artifacts

Do not commit local datasets, virtual environments, benchmark jobs, training
runs, checkpoints, logs, or decoded outputs. The expected local directories,
publication-manifest requirements, and policy for existing historical fixtures
are documented in [`docs/artifacts.md`](docs/artifacts.md).

Per-codec evaluation still lives inside each codec rather than in a
repository-wide suite; `examples/visualization/compare_sequences.py` is the one
shared piece, covering geometric error between any two sequences the loader can
read. Results should identify the exact component revision, configuration,
dataset/frame range, encoded byte count, runtime environment, and metric
implementation.

## Contributing

Contributions are welcome, especially around shared data abstractions, common
metrics, codec adapters, documentation, and performance. Keep codec
dependencies isolated and document any new binary fixture or external artifact
alongside the code that consumes it.

Please contact the Open4D maintainers before adding a large dataset, checkpoint,
or third-party source tree.

## License

Open4D is distributed under the [MIT License](LICENSE) and is intended to be
useful in academic, educational, and commercial projects. You may use, adapt,
and redistribute the Open4D code subject to the attribution and license-notice
requirements in the license. Bundled third-party components and submodules
remain subject to their respective license terms.

If Open4D contributes to published research, please acknowledge the project
using the repository's [citation metadata](CITATION.cff), and cite the original
papers for any individual codecs, datasets, or algorithms used in your work.
We also welcome feedback through the project's issue tracker: sharing real-world
use cases, limitations, and improvement ideas helps guide future development.
