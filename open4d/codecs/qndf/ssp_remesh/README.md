This module for **Remeshing using Successive Self-Parameterization** is a minimal
and slightly modified version of the implementation of
[Surface Multigrid via Intrinsic Prolongation](https://github.com/HTDerekLiu/surface_multigrid_code/tree/main/08_subdiv_remesh).

It builds `ssp_remesh_bin`, which `../build_dataset.py` invokes to produce the
coarse/subdivided training pairs QNDF compresses.

## Prerequisites

libigl is a submodule of the Open4D repository, pinned at **v2.3.0**. Fetch it
before configuring:

```bash
git submodule update --init --recursive open4d/codecs/qndf/ssp_remesh/libigl
```

The pin is deliberate and not a lagging one. `cmake/FindLIBIGL.cmake` ends with
`include(libigl)`, and v2.4.0 rewrote libigl's CMake so that including
`cmake/libigl.cmake` directly is a `FATAL_ERROR`. v2.3.0 is the last release this
build script speaks to.

Install Eigen from your package manager:

```bash
sudo apt install libeigen3-dev   # Ubuntu
brew install eigen               # macOS
```

Eigen is the one external libigl still needs here, and a system copy is strongly
preferred over letting libigl fetch its own. libigl's downloader generates a
helper project pinned at `cmake_minimum_required(VERSION 3.2)` and configures it
in a nested `cmake` call that forwards no policy flags, so on CMake 4 — which
removed compatibility with anything below 3.5 — the fetch fails and no flag on
the outer command can rescue it.

If you would rather vendor Eigen than install it, place it where libigl expects
and skip the downloader entirely:

```bash
git clone --depth 1 --branch 3.3.7 https://gitlab.com/libeigen/eigen.git \
  libigl/external/eigen
# then add -DLIBIGL_SKIP_DOWNLOAD=ON to the cmake command below
```

## Building

From this folder:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The executable lands at `build/ssp_remesh_bin`, which is where
`build_dataset.py` looks for it. Run it directly with:

```bash
./build/ssp_remesh_bin [mesh_path] [target_faces] [number_subdivision] [random_seed]
```

It writes `input_f<target_faces>_s<i>.obj` and `output_f<target_faces>_s<i>.obj`
for each subdivision level `i` into the *current working directory*;
`build_dataset.py` moves those into `experiments/<mesh_name>/`.

This is a headless command-line tool: it requests neither OpenGL nor GLFW, and
links only `igl::core`, so it builds and runs on a machine with no display.
