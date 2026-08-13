# gs-tools

- `environment.yml` — the one environment both are built and run in.
- `simple-knn/` — one copy, QUEEN's, whose added `<float.h>`/`<cfloat>` includes
  are what let it compile under GCC 13. Both trees import it as `simple_knn._C`,
  by module name, so moving it here changed nothing in either.
- `glm/` — one copy. All three rasterizers vendored byte-identical trees, and it
  is header-only, so sharing it is only an `-I` path.
- `SIBR_viewers/` — one copy of the interactive viewer, 3DGStream's, which is a
  strict superset of QUEEN's: the 463 files they shared were byte-identical and
  the 63 extra are documentation images. Not needed to train or evaluate.
  `src/projects/gaussianviewer` is the one that renders Gaussian splats; it is
  force-added, because SIBR's own `.gitignore` excludes `src/projects/*`.

## Setup

    conda env create -f environment.yml
    conda activate open4d-gs

Then build the five CUDA extensions from `open4d/reconstruction/`, all with
`--no-build-isolation` (an isolated build has no torch to compile against):

    export TCNN_CUDA_ARCHITECTURES=89      # sm_89 = RTX 4090; set to your card
    pip install --no-build-isolation \
      git+https://github.com/NVlabs/tiny-cuda-nn/#subdirectory=bindings/torch
    pip install --no-build-isolation gs_tools/simple-knn
    pip install --no-build-isolation gs_tools/rasterizers/diff-gaussian-rasterization
    pip install --no-build-isolation gs_tools/rasterizers/gaussian-rasterization-grad
    pip install --no-build-isolation gs_tools/rasterizers/gstream-rasterization

Build on ext4. On an ntfs3 mount ninja deadlocks in `ntfs_file_write_iter`.

## The viewer

Linux only, and it needs a display with OpenGL 4.5. There is no macOS build, and
X11 forwarding does not help: XQuartz offers indirect GLX at roughly OpenGL 2.1.

    cd gs_tools/SIBR_viewers
    cmake -Bbuild . -DCMAKE_BUILD_TYPE=Release
    # cmake downloads extlibs/CudaRasterizer, which needs one include added,
    # and does so again on every reconfigure:
    sed -i 's|#include <cuda_runtime_api.h>|#include <cuda_runtime_api.h>\n#include <cstdint>|' \
      extlibs/CudaRasterizer/CudaRasterizer/cuda_rasterizer/rasterizer_impl.h
    cmake --build build -j16 --target install

That produces `install/bin/SIBR_gaussianViewer_app`, plus `SIBR_remoteGaussian_app`
for attaching to a training run. Point it at a 3DGS-format model directory.

Upstream SIBR last shipped 2024-01-30 and does not build on a current
distribution, so this copy carries fixes. Four are in-tree:

- `core/video/FFmpegVideoEncoder.cpp` — FFmpeg 5 removed `av_register_all`,
  `AVStream::codec` and `avcodec_encode_video2`. Ported to
  `avcodec_send_frame`/`avcodec_receive_packet` with a separately allocated
  context copied into `codecpar`.
- `core/video/VideoUtils.hpp` — a structured binding over `std::vector<uint>`,
  copy-pasted from the `std::map` template. Also returned an uninitialised value
  when every bin was empty.
- `core/raycaster/Raycaster.{hpp,cpp}` — Embree 4 renamed `RTCIntersectContext`
  and moved it behind an arguments struct. Selected by `__has_include`, so
  Embree 3 and 4 both work.
- `core/raycaster/CMakeLists.txt` — linked `-lembree`, which no distribution
  ships; now `find_library` over `embree4 embree3 embree`.

The fifth, the `<cstdint>` above, cannot be committed: `extlibs/` is line 1 of
SIBR's own `.gitignore` and is re-fetched by cmake.

Verified 2026-08-13: builds clean on Ubuntu 24.04 / GCC 13.3 / Embree 4.3,
binary links with no unresolved libraries and starts. Rendering was not
exercised — that needs a display, and the box had none free.


