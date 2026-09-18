# Requirements and installation

## Baseline

| | |
|---|---|
| Python | 3.10–3.13 |
| Operating system | macOS, Linux, or Windows |
| CPU | Any x86-64 or arm64; no particular core count |
| GPU | Not required. The viewers open a real OpenGL window, so a graphical session is needed even for `--save` |
| Memory | Roughly 1 MB of RAM per frame of playback |
| Disk | About 1.5 GB for a clone with submodules initialized |

`pip install -e .` needs only NumPy, and reads `.obj` and `.ply` with no further
dependencies. Extras add optional readers and viewers. The comparison program
additionally needs SciPy, which the `[player]` extra installs, for its
nearest-neighbour search — the same `cKDTree` query TVMC's own evaluation uses.

Open3D ships no 3.13 wheels, capping `.[open3d]` and the codecs at 3.12.

## Installation

Clone with submodules to obtain the pinned Draco, libigl, SAM3, and MPEG V-DMC
source:

```bash
git clone --recurse-submodules https://github.com/open4dfoundation/Open4D.git
cd Open4D
```

For the lightweight core package:

```bash
python -m venv .venv
source .venv/bin/activate            # Windows: .venv\Scripts\activate
python -m pip install --upgrade pip
python -m pip install -e .
```

Optional local tooling is available through extras:

```bash
python -m pip install -e ".[player]"   # the example viewer (PyQt6 + pyqtgraph)
python -m pip install -e ".[usd]"      # OpenUSD containers
python -m pip install -e ".[tools]"    # trimesh, for extra mesh formats
python -m pip install -e ".[open3d]"   # Open3D adapter; Python 3.12 or older
python -m pip install -e ".[qndf]"     # QNDF/QNDF-INT8 in-process adapters
python -m pip install -e ".[temporal]" # experimental temporal-delta/PCA codecs
python -m pip install -e ".[all]"
```

These extras do not install the heavyweight codec environments. Use the setup
instructions inside the selected codec before running it. Research codec
implementations remain source-checkout-only and are excluded from the
lightweight wheel until their provenance review is complete.

If an existing clone is missing Draco, initialize and build all three copies —
the Draco baseline codec's own, plus TSMC's and TVMC's — with:

```bash
./scripts/setup_draco.sh
```

## One Python dependency set for the codecs

The supported baseline for codec Python stages is described by
[`environment.yml`](../environment.yml) at the repository root:

```bash
conda env create -f environment.yml
conda activate open4d
pip install -e .
```

The Python set is Python 3.12, NumPy 1.26.4, Open3D 0.19, and PyTorch 2.7.0.
Native projects use one external .NET 10 SDK. This replaces three Python
versions, two Open3D versions, two PyTorch versions, and three .NET targets. The
Python pins themselves live in
[`requirements-codecs.txt`](../requirements-codecs.txt), which `environment.yml`
installs; it lists direct dependencies only, so inside an existing Python 3.12
environment `pip install -r requirements-codecs.txt` is equivalent.

Codec-local setup scripts may create a convenience virtual environment, but
they must use these same Python and package pins rather than defining a second
dependency baseline. Native tools and GPU extensions remain separate.

### The .NET SDK trap

One trap worth naming, because its error message points the wrong way. The .NET
projects target `net10.0`, and a distribution's own `dotnet` under
`/usr/lib/dotnet` will shadow a newer SDK in `~/.dotnet` on `PATH`. The build
then fails with `NETSDK1045: The current .NET SDK does not support targeting
.NET 10.0`, which reads as a missing SDK when the SDK is usually installed and
merely second in line. Check with `dotnet --list-sdks` before installing
anything. Downgrading the projects to `net9.0` is not the fix: .NET 9 left
support in May 2026, and moving off end-of-life targets is why they are on
`net10.0`.

## Compiled GPU extensions

Some codecs additionally need compiled extensions that pip cannot resolve from a
version number alone, because each is built against one exact PyTorch and CUDA
build. Those are optional and separate, with install commands in
[`requirements-gpu.txt`](../requirements-gpu.txt):

| Extra | Needed by |
|---|---|
| `cupy-cuda12x` | `n4mc`, `tsmc` |
| `torch-scatter` | `n4mc` |
| `nvdiffrast` | `n4mc` |
| `kaolin` | `n4mc`, `klt` |

## What each module adds

| Module | Adds |
|---|---|
| `codecs/tvmc` | .NET 10 SDK, CMake; Homebrew macOS or Ubuntu |
| `codecs/tsmc` | .NET 10 SDK, SAM3, `cupy`; Ubuntu 24.04, tested against Meta Quest 3. `convert_to_std_obj.py` runs inside Blender, which supplies `bpy` |
| `codecs/n4mc` | All four GPU extras and an NVIDIA GPU — 24 GB holds only about two training frames at resolution 256 |
| `codecs/qndf`, `codecs/qndf_int8` | An NVIDIA GPU for training. Evaluation (`mesh_errors.py`) runs on CPU. Building the `ssp_remesh` preprocessor needs CMake and Eigen (`libeigen3-dev`/`brew install eigen`), plus the pinned libigl submodule |
| `codecs/klt` | `kaolin` and an NVIDIA GPU; 24 GB is the same ceiling at resolution 128–256 |
| `codecs/draco` | A CMake build of the vendored Draco submodule. Open3D, pymeshlab, and OpenCV are for evaluation only |
| `codecs/vdmc`, `codecs/faster_vdmc` | The MPEG reference and optimized test models' own build requirements |
| `reconstruction/rgbd` | Two hardware-synchronized RGB-D cameras, a Windows capture host, and an Ubuntu host with Python 3.10+, an NVIDIA GPU, and CUDA-enabled Open3D. Its legacy C++ pipeline additionally wants CUDA 12.x, Open3D 0.18, OpenCV, Eigen, jsoncpp, Draco, CMake, Ninja, and either the Azure Kinect SDK or the Orbbec K4A wrapper |
| `reconstruction/gs_tools`, `queen`, `3dgstream`, `vega` | The separate `open4d-gs` conda environment and five CUDA extensions built with `--no-build-isolation`, per [`gs_tools`](../open4d/reconstruction/gs_tools/README.md). Build on ext4; on an ntfs3 mount ninja deadlocks in `ntfs_file_write_iter` |
| `reconstruction/rerf` | Python 3.8, because `ac_dc/ncvv_ac_dc.cpython-38-*.so` ships without sources and cannot be rebuilt for a newer interpreter. Plus torch with CUDA, mmcv, bitarray, Pillow, NumPy — a separate environment from every other module here |
| `integrations/unity` | Unity, plus a C++ toolchain to rebuild the backend for anything other than the prebuilt macOS and Android/Quest 3 plugins |

## RGB-D capture on Windows

The RGB-D capture host is Windows and only encodes and forwards frames, so it
needs no NVIDIA GPU: just the camera vendor SDK (tested: Orbbec K4A Wrapper
1.10.5, SDK 1.10.28, two Femto Bolts), both cameras on separate USB 3 ports with
a sync hub, and an OpenSSH client. Close Orbbec Viewer first or the sender fails
with `Hardware MFT failed to start`. 5 synchronized pairs/s held over Wi-Fi and
VPN; 15 did not.

Calibration layout and the step-by-step session walkthrough are in
[`open4d/reconstruction/rgbd/README.md`](../open4d/reconstruction/rgbd/README.md),
which covers how to run the pipeline and leaves requirements to this page.
