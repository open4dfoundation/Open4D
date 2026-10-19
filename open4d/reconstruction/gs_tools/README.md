# gs-tools

Shared CUDA environment, rasterizers, exporters and comparison viewer for QUEEN,
3DGStream, Vega and ReRF.

- `environment.yml`: training environment for QUEEN and 3DGStream.
- `simple-knn/`: QUEEN's copy, with GCC 13 include fixes.
- `glm/`: shared header-only dependency for the three rasterizers.
- `SIBR_viewers/`: optional Linux viewer; not needed for training or evaluation.
  `src/projects/gaussianviewer` is tracked despite SIBR's ignore rule.

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

Run `gs-tools doctor` to check the environment and test each rasterizer at a
distance of one unit. Older builds cull splats within four units, leaving
object-scale scenes black.

For MiDaS (`timm==0.6.13`), create a separate environment using
`requirements-midas.txt`, run QUEEN's `scripts/patch_timm.py` there, and download
weights with `scripts/setup.sh --midas-weights`. Cache one depth map per camera
in `depth_priors/`, then enable the cache during training:

    gs-tools depth-prior -s scene --python /path/to/midas-env/bin/python
    gs-tools train --method queen -s scene -m run --depth-priors

## Comparing methods: Explore and Compare

`gs-tools view` groups clips by scene and method in a shared viewport:

- **Explore:** shared free camera for Gaussian outputs.
- **Compare:** captured camera poses, with independent camera and time controls.
  Renders Gaussians and ReRF (`--rig-views`) beside the captured photographs.
  `A|B` enables a draggable wipe between panes.

`gs_tools/cameras.py` reads the eight ORBIT camera poses from `transforms.json`.
Vega uses ORBIT world coordinates; ReRF renders at its training views.

    gs-tools view \
      -i results/vega-gaussian/prepared-final \
         /media/frozzzen/DataDrive/ORBIT_datasets_gaussian \
         ~/nevo_runs/g_basketball ~/nevo_runs/g_dancer ... \
      --bitstream rerf --rig-views 0 1 2 3 4 5 6 7 --render

    # just Vega, free camera
    gs-tools view -i results/vega-gaussian/prepared-final --objects basketball

The URL fragment stores the comparison state:

    #scene=basketball&mode=compare&camera=0&methods=vega,rerf,captured&wipe=1

### Comparison limits

- All eight rig cameras were training views for Vega and ReRF. These comparisons
  measure reconstruction; held-out evaluation requires retraining with a holdout.
- Vega exports bake colour from one direction (see below).
- ReRF uses its training intrinsics at 1920x1080; captured images are 4:3. Poses
  match, but crops and pixel positions differ.
- The eight cameras are coplanar. Reconstruction outside the observed plane is
  limited; `cameras.ring_path` stays on the ring.

## Viewing output: Vega and ReRF

Exports are directories of frames plus `view.json`, played by the browser client
in `streamer.client`:

| Method | Export |
| --- | --- |
| Vega | `StreamingPlayer` decodes geometry and evaluates colour into one 3DGS PLY per frame |
| ReRF | `rerf_render.py` produces images using its Python 3.8 runtime |
| QUEEN / 3DGStream | copies saved 3DGS PLYs and writes a manifest |

`gs_tools.outputs.gaussian_frames` resolves QUEEN's `frames/NNNN/`, 3DGStream's
`frameNNNNNN/point_cloud/iteration_N/`, and single-frame
`point_cloud/iteration_N/` layouts. It selects the highest iteration; standalone
`added/` files contain only the new Gaussians and are not full frames.

    gs-tools view -i ~/runs/coffee_martini --scene-name coffee_martini --method-name queen

    gs-tools export -i ~/runs/coffee_martini --frame-format splat -o /tmp/bundle

`--frame-format splat` uses 32 bytes per Gaussian. Positions and scales remain
exact; colour, opacity and rotation are quantized to 8 bits. SH bands above
degree 0 are dropped, removing view-dependent appearance. Keep PLY for archival
use.

Set `--scene-name` to compare runs of the same subject. Otherwise the run's
directory supplies its scene name. `--scene-name` and `--method-name` apply to
all sources in one invocation; export different subjects separately.

    # Inspect a reconstruction run
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

### Export limits

- **Vega:** colour is evaluated once at `--bake-azimuth` (default 0°), producing
  `sh_degree 0` PLYs. Positions, scales, rotations and opacity remain exact.
- **ReRF images:** viewpoints are fixed by the renders. `--rig-views` renders
  each timestep at the capture cameras from the same decoded volume.
- **`--render_360 N`:** upstream uses `angle = 2*pi*i/360`, so 30 frames cover
  29 degrees and advance time with the camera.
- **ReRF configuration:** `gs_tools.methods.rerf.bitstream_info` infers PCA
  channels and group keys from per-frame headers. Override with `--pca-chs`,
  `--group-size` or `--no-pca` when needed.
- **ReRF output directories:** different bitstreams can overwrite the same
  `render_360_rerf_<n>` directory. Bundles separate them; `--bitstream` is required
  when rendering one of several bitstreams.

ReRF needs a separate Python 3.8 environment. See
[`../rerf/README.md`](../rerf/README.md) for its dependencies.

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

This copy includes FFmpeg 5 API fixes, an empty-bin fix in `VideoUtils.hpp`, and
Embree 3/4 compatibility and library discovery in `core/raycaster/`. The
`<cstdint>` fix above must be reapplied after CMake fetches `extlibs/`.
