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

## Comparing methods: Explore and Compare

`gs-tools view` is a comparison tool, not a file browser. Clips are grouped by
**scene** and **method**, several methods share one viewport, and there are two
modes because there are two honest ways to put reconstructions side by side:

**Explore** — a free camera, shared by every pane. Only methods that ship
Gaussians can appear, because there is nothing else to aim a free camera at. Any
difference you see between panes is the reconstruction, not the viewpoint.

**Compare** — the scene's own **capture rig** as the camera path, with camera and
time scrubbed independently. Gaussian methods are rendered at the selected rig
pose; ReRF is rendered there too (`--rig-views`); and the captured photograph is
shown as a third method. This is the mode a volumetric representation and a
photograph can both join, and the one a PSNR/SSIM number could be attached to.
`A|B` wipes between two panes with a draggable handle.

The camera path is not invented. `gs_tools/cameras.py` reads it out of the ORBIT
corpus's own `transforms.json` — the eight cameras that captured the scene —
which is what makes ground truth available at every station and means nothing
has to be registered: Vega's Gaussians are already in ORBIT world coordinates,
and ReRF is rendered at its own training views, which *are* those cameras.

    # everything, in one page: 9 Vega objects, ReRF at 8 rig cameras, the photographs
    gs-tools view \
      -i results/vega-gaussian/prepared-final \
         /media/frozzzen/DataDrive/ORBIT_datasets_gaussian \
         ~/nevo_runs/g_basketball ~/nevo_runs/g_dancer ... \
      --bitstream rerf --rig-views 0 1 2 3 4 5 6 7 --render

    # just Vega, free camera
    gs-tools view -i results/vega-gaussian/prepared-final --objects basketball

The whole view state lives in the URL fragment, so a particular comparison is a
link:

    #scene=basketball&mode=compare&camera=0&methods=vega,rerf,captured&wipe=1

### What the comparison does and does not license

- **No held-out view.** All eight cameras were training views for both Vega and
  ReRF (`nevo_corpus.json` records no holdout, and Vega refines against all
  eight). This measures reconstruction, not generalisation. A real held-out view
  means re-preparing the corpus with a holdout and retraining.
- **Vega colour is baked** — see below. Its *geometry* at a rig pose is exact.
- **ReRF's framing differs from the captured pane.** The pose matches; the crop
  does not, because ReRF renders at its training images' size and intrinsics
  (1920x1080) while the corpus captured 4:3. Compare content, not pixel
  positions. Aligning the intrinsics is the obvious next step and is not done.
- **The rig is eight coplanar cameras.** Neither silhouette carving nor a
  photometric fit recovers what no camera saw, so a path far off that plane
  makes every method look broken for reasons belonging to the capture.
  `cameras.ring_path` stays on the ring.

## Viewing output: Vega and ReRF

`SIBR_gaussianViewer_app` below opens a 3DGS run directory on a Linux box with a
display. Two of the things in `open4d/reconstruction` cannot be opened that way
at all, for different reasons, and `gs-tools export` / `gs-tools view` are what
make them viewable:

- **Vega** stores per-object `frame_XXXX.pt` chunks holding geometry only, with
  colour in a hierarchical hash grid queried per Gaussian per view direction at
  render time. Nothing but Vega can read it. The exporter drives Vega's own
  `StreamingPlayer` and colour model and writes **one 3DGS PLY per frame**, so
  the result opens in the bundled viewer, in SuperSplat, or in SIBR.
- **ReRF** (what the NeVo baseline vendors and streams) stores a DCT-coded,
  arithmetic-coded feature voxel grid -- not Gaussians, so there is no PLY to
  write. Its only decoder is its own, and that only runs under Python 3.8. The
  adapter runs `rerf_render.py` and bundles the **images** it produces.

Both land in the same shape -- a directory of frames plus a `view.json` -- and
`gs-tools view` serves it with a self-contained WebGL2 splat viewer, which is
the point: the GPU box usually has no display, and SIBR needs one (X11
forwarding does not help, see below).

    # what is this directory?
    gs-tools inspect -i ~/nevo_runs/g_basketball

    # Vega: decode one object's 30 frames to PLY and serve them
    gs-tools view -i results/vega-gaussian/prepared-final --objects basketball

    # every object in the catalog, as separate clips in one bundle
    gs-tools view -i results/vega-gaussian/prepared-final

    # ReRF: bundle the renders already in the run (no GPU time)
    gs-tools view -i ~/nevo_runs/g_basketball

    # ReRF: render a specific bitstream first (minutes of GPU time)
    gs-tools view -i ~/nevo_runs/g_basketball --bitstream rerf --render

    # build a bundle without serving it, e.g. to copy elsewhere
    gs-tools export -i ~/nevo_runs/g_basketball -o /tmp/bundle

`view` binds loopback. Add `--host 0.0.0.0` to open it from another machine, and
note that the server has no authentication of any kind. Without `-o` the bundle
goes to a cache directory keyed by the source path, and a second `view` of the
same source reuses it; `--force` rebuilds.

### What the exports do and do not preserve

- **Vega colour is baked.** A PLY's `f_dc` is one colour per Gaussian; Vega's is
  view-dependent. Colour is evaluated once from `--bake-azimuth` (default 0°)
  and frozen, so orbiting in the viewer does not change appearance the way a
  real Vega client would. Re-export at another azimuth to see it from
  elsewhere. The export is `sh_degree 0` and says so in the viewer.
- **Vega geometry is exact.** Position, scale, rotation and opacity round-trip
  bit-for-bit; verified against `diff_gaussian_rasterization` on the decoded
  Gaussians.
- **ReRF gets no free camera.** The clip is whatever camera it was rendered at
  -- upstream's orbit, or the rig cameras with `--rig-views`. Turning occupied
  voxels into one Gaussian each would give a free camera, but its appearance
  would not be what ReRF reconstructs, so it is not offered.
- **`--render_360 N` is not a full orbit.** Upstream computes
  `angle = 2*pi*i/360`, so 30 frames sweep 29 degrees, and it advances time with
  the camera -- the two cannot be separated. `--rig-views` renders at the capture
  cameras instead, and advances the decode once per *timestep* rather than once
  per image, so every view of one instant comes from the same decoded volume.
- **ReRF's codec settings are inferred, not remembered.** Upstream requires
  `--pca`/`--pca_chs`/`--group_size` to match between compress and render and
  nothing enforces it, so `gs_tools.methods.rerf.bitstream_info` reads them back
  off the per-frame headers: entry count and channel split give the PCA
  configuration, single-entry frames give the key frames. Override with
  `--pca-chs`, `--group-size`, `--no-pca` if the inference is ever wrong.
- **`render_360_rerf_<n>` does not name a bitstream.** Two bitstreams in one run
  render to the same directory and the second overwrites the first. A bundle
  keeps them apart; the run directory does not. This is why bundling existing
  renders is the default and `--bitstream` is required to render one of several.

ReRF runs in its own Python 3.8 environment (`conda activate nevo`; see
`../nevo/README.md`). `gs-tools` finds that interpreter as a sibling conda
environment of the current one -- override with `$OPEN4D_RERF_PYTHON` or
`--rerf-python`.

## The SIBR viewer

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


