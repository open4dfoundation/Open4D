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
additionally needs SciPy for nearest-neighbour search; `[player]` installs it.

Open3D ships no 3.13 wheels, capping `.[open3d]` and the codecs at 3.12.

## Installation

Clone the core without downloading research backends:

```bash
git clone https://github.com/open4dfoundation/Open4D.git
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
python -m pip install -e ".[player]"   # the viewer and GIF export (PyQt6 + pyqtgraph)
python -m pip install -e ".[usd]"      # OpenUSD containers
python -m pip install -e ".[tools]"    # trimesh, for extra mesh formats
python -m pip install -e ".[open3d]"   # Open3D adapter; Python 3.12 or older
python -m pip install -e ".[qndf]"     # Python dependencies, not the QNDF plugin/runtime
python -m pip install -e ".[all]"
```

### Downloadable adapter packages

The core ships usable NumPy array codecs. Research codec registrations are
separately installable packages; build a wheel with `python -m build
plugins/vdmc --outdir /tmp/open4d-plugins` or install directly:

```bash
python -m pip install ./plugins/vdmc        # one adapter
python -m pip install ./plugins/queen       # codec and reconstruction registration
# Alternatively install the aggregate instead of the individual packages:
python -m pip install ./plugins/research
python -c "import open4d as o4d; print(o4d.available_codecs()); print(o4d.available_methods())"
```

These packages are not yet published to PyPI. They contain MIT registration
metadata, a license and a dependency on the core. They contain no research
implementations, native binaries, datasets or weights. Avoid installing an
aggregate and an individual provider for the same name; duplicate entry points
are reported and ignored instead of choosing an arbitrary provider.

Acquire and build a backend separately under its own terms. Initialize only
the submodules needed by that method, not the full set. KLT, N4MC and QNDF can
use `OPEN4D_KLT_ROOT`, `OPEN4D_N4MC_ROOT` and `OPEN4D_QNDF_ROOT`, respectively;
`OPEN4D_RESEARCH_ROOT` still supports a whole source checkout. V-DMC supports
`OPEN4D_VDMC_ROOT` or configured encoder/decoder executables. QUEEN, 3DGStream,
Vega and ReRF retain their separate environments and runtime-root variables.
Backend-specific code may require the project's recorded patches; downloading
an unmodified upstream is not a claim of runtime compatibility.

An adapter package does not resolve missing licenses, the TVMC redistribution
prohibition, QUEEN's non-commercial restrictions, or ReRF's conflicting terms.
Their source and model weights remain excluded. The existing release ledger
and maintainer approval still gate publication of the core and plugins.

### Documentation site

The wiki builds from the existing guides and generates its API reference from
public signatures and docstrings. No separate hand-maintained API copy is
needed. Preview it outside the source tree:

```bash
python -m pip install -e '.[docs]'
python -m mkdocs build --strict --site-dir /tmp/open4d-site
python -m http.server 8000 --directory /tmp/open4d-site
```

The documentation workflow uploads a preview artifact for every PR. Reviewed
main-branch sources can be deployed to GitHub Pages through its manual deploy
option once the release approval gate passes. Unreviewed media in `docs/assets`
is excluded from the generated site.

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
Native projects use the .NET 10 SDK. Python pins live in
[`requirements-codecs.txt`](../requirements-codecs.txt), which `environment.yml`
installs; it lists direct dependencies only, so inside an existing Python 3.12
environment `pip install -r requirements-codecs.txt` is equivalent.

Codec-local setup scripts may create a convenience virtual environment, but
they must use these same Python and package pins rather than defining a second
dependency baseline. Native tools and GPU extensions remain separate.

### .NET SDK selection

Projects target `net10.0`. If an older `/usr/lib/dotnet` precedes `~/.dotnet` on
`PATH`, builds fail with `NETSDK1045`. Check `dotnet --list-sdks`, then put the
.NET 10 installation first on `PATH`.

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
| `reconstruction/gs_tools`, `queen`, `3dgstream`, `vega` | The separate `open4d-gs` conda environment and five CUDA extensions built with `--no-build-isolation`, per [`gs_tools`](../codecs/gs_tools/README.md). Build on ext4; on an ntfs3 mount ninja deadlocks in `ntfs_file_write_iter` |
| `reconstruction/rerf` | Python 3.8, because `ac_dc/ncvv_ac_dc.cpython-38-*.so` ships without sources and cannot be rebuilt for a newer interpreter. Plus torch with CUDA, mmcv, bitarray, Pillow, NumPy — a separate environment from every other module here |
| `integrations/unity` | Unity, plus a C++ toolchain to rebuild the backend for anything other than the prebuilt macOS and Android/Quest 3 plugins |

### Building V-DMC on macOS

The V-DMC CMake files assume x86-64. On macOS, including Apple Silicon, use the
build script. It needs the Xcode Command Line Tools, CMake 3.17 or newer, and
network access on the first run, when CMake downloads the dependencies.

```bash
git submodule update --init codecs/vdmc   # or codecs/faster_vdmc
./scripts/build_vdmc_macos.sh vdmc               # or faster_vdmc
```

The script patches the x86-only compiler flags and a few macOS build errors,
keeping a `.open4d-orig` copy of each edited file (`--revert` restores them).
It builds `encode` and `decode` into `<submodule>/build/Release/bin`, where a
source checkout finds them without configuration, and prints the
`OPEN4D_VDMC_ENCODER`/`OPEN4D_VDMC_DECODER` (or `OPEN4D_FASTER_VDMC_*`) lines to
export. An installed wheel can instead set `OPEN4D_VDMC_ROOT` to the source tree. `--source` and `--build-dir` select other locations.

## RGB-D capture on Windows

The RGB-D capture host is Windows and only encodes and forwards frames, so it
needs no NVIDIA GPU: just the camera vendor SDK (tested: Orbbec K4A Wrapper
1.10.5, SDK 1.10.28, two Femto Bolts), both cameras on separate USB 3 ports with
a sync hub, and an OpenSSH client. Close Orbbec Viewer first or the sender fails
with `Hardware MFT failed to start`. Start with the sender's default 5 FPS.

Calibration layout and the step-by-step session walkthrough are in
[`open4d/reconstruction/rgbd/README.md`](../open4d/reconstruction/rgbd/README.md),
which covers how to run the pipeline and leaves requirements to this page.
