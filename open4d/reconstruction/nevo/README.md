# NeVo (ORBIT adaptation)

An offline, trace-driven simulator for evaluating the streaming quality of

> Nan Wu, Bo Chen, Ruizhi Cheng, Klara Nahrstedt, Bo Han.
> **"NeVo: Advancing Volumetric Video Streaming with Neural Content
> Representation."** ACM MobiCom 2025.

NeVo itself has no released code. What it streams does:
[ReRF](https://github.com/aoliao12138/ReRF) (CVPR 2023), the streamable-NeRF
representation the paper builds on and benchmarks against, is vendored
unmodified under `rerf/` and is what every stage here loads, renders and
measures. `rerf/PATCHES.md` records exactly what was and was not touched.

NeVo streams NeRF content rather than point clouds or meshes, so unlike every
other baseline in this repo it needs a *neural* volumetric video to stream.
ReRF's own dataset is licence-gated, so we train ReRF ourselves on
`ORBIT_datasets_gaussian` -- see `rerf/DATA.md`, and note the NeVo paper does
the same substitution for two of its six datasets.

There are no sockets and no WebRTC anywhere in this baseline. It models byte
arrival: a bandwidth trace gives queueing delay, a loss trace gives drops, and
a deadline decides what counts as lost. That makes a run reproducible from a
seed and lets the ablations be exact rather than approximately re-measured.

## Status

Built and verified so far -- **steps 1 and 2 of the pipeline, plus the
importance CDF**, which is the evidence the rest of the design rests on:

1. **Load** a ReRF feature voxel grid and its motion vectors (`nevo/sequence.py`).
2. **Score** every feature voxel's neural visibility by instrumenting ray
   marching to emit `T_i * alpha_i` per sample and scatter-maxing it into a
   per-voxel buffer (`nevo/importance.py`), then take the CDF (`nevo/cdf.py`).

Plus a viewer (`orbitnevo/render_frames.py`, `orbitnevo/live_demo.py`,
`orbitnevo/report.py`) that plays the trained sequence back with the filtering
switchable, and two things needed to know whether step 2 is worth building
on:
`nevo/render.py` renders a reloaded frame against its training image (does
step 1 rebuild the checkpoint correctly?), and `nevo/filtering.py` +
`orbitnevo/filter_sweep.py` drop the sub-threshold voxels and score the result
(does the metric actually identify what is safe to discard?).

Not built yet, deliberately, pending the verification below:

3. Packetize surviving voxels (contiguous-block vs. interleaved mapping).
4. Simulate arrival: bandwidth trace -> queueing delay, loss trace -> drops,
   plus RTT/2, with a 33 ms deadline.
5. Recover missing voxels (VRM: 3D CNN over 3x3x3 neighbours x 9 history
   frames -> LSTM, with an availability-mask channel).
6. Render at the trace viewport; SSIM and LPIPS against the unfiltered grid.

## What the verification says

Full write-up in `RESULTS.md`; look at it before building on the paper's
numbers. The short version:

- The instrumentation is faithful: our marched weights match ReRF's own
  forward pass exactly, and reloaded frames render within a dB of what the
  trainer logged.
- The long tail is real and the mechanism works, at roughly the scale claimed.
  At the SSIM >= 0.98 bar the paper uses, filtering by neural visibility drops
  **49-56%** of the non-empty feature voxels at ReRF's own 8^3 codec block
  (two objects), **64%** at a 4^3 unit, and **69%** on a better-reconstructed
  version of the same subject.
- **Caveat that matters for step 3:** the SSIM >= 0.98 bar those figures use
  passes renders with plainly visible 8^3 block artefacts. SSIM forgives
  spatially coherent error, which is exactly what dropping a block produces.
  Fit the threshold against LPIPS, not SSIM alone.
- The specific figure "~60% of voxels below 0.025" is *not* an invariant. It
  slides from 42% to 77% purely with the granularity at which a "feature
  voxel" is defined, which the paper does not pin down -- and the paper itself
  fits its threshold to an SSIM target rather than fixing it at 0.025 (on this
  content the fitted value is 0.2, eight times the quoted one). Treat the
  quality bar as the claim and the threshold as an output.

## Layout

```
rerf/                  vendored ReRF, unmodified (see rerf/PATCHES.md)
  configs/nevo/        generated training configs
nevo/                  the simulator, no ORBIT or harness dependencies
  rerf_env.py          make `import lib.dvgo` work outside upstream's wrapper
  cameras.py           rig geometry and the world -> normalised transform
  sequence.py          step 1: feature voxel grids + motion vectors
  blocks.py            ReRF's 8^3 codec block geometry and occupancy
  importance.py        step 2: instrumented ray marching -> per-voxel weights
  viewports.py         synthetic viewports and 6DoF trace decoding
  cdf.py               streaming CDF accumulation and the Figure 7 plot
  render.py            render a loaded frame; check it reloaded correctly
  filtering.py         drop the sub-threshold voxels and score what changed
orbitnevo/             ORBIT-corpus adapters and CLIs
  prepare.py           ORBIT -> ReRF-trainable NHR corpus
  train.py             drive ReRF training over a corpus
  importance_cdf.py    steps 1-2 end to end
  filter_sweep.py      what each threshold drops, and what it costs in SSIM
  render_frames.py     render the plain-ReRF and visibility-filtered conditions
  live_demo.py         stream those frames to a browser as MJPEG
  rerf_cli.py          run ReRF's own compress.py / rerf_render.py
  rd_sweep.py          bytes (real encoder) against quality, per threshold
  report.py            static page: the viewer, plus every other output
nevo_tests/            unit tests; the model-dependent ones skip without a run
```

## Environments

This baseline needs **two**, and that is not incidental. ReRF's entropy coder
`ac_dc/` ships only as a CPython 3.8 binary with no sources, and importing
`lib.dvgo` pulls it in — so anything touching a ReRF model runs on 3.8, while
this repo itself requires 3.10+.

| Stage | Environment | Why |
| --- | --- | --- |
| `orbitnevo/prepare.py` | repo env (`conda activate pytorch`, 3.10) | reuses DeltaStream's nvdiffrast rasteriser and this repo's 3.10-only type syntax |
| everything else | `conda activate nevo` (3.8) | ReRF's `ac_dc` binary |

Creating the `nevo` environment:

```bash
conda create -y -n nevo python=3.8
conda activate nevo
pip install torch==2.4.1 torchvision==0.19.1 --index-url https://download.pytorch.org/whl/cu121
pip install torch_scatter -f https://data.pyg.org/whl/torch-2.4.0+cu121.html
pip install "numpy<2" "mmcv==1.7.2" imageio imageio-ffmpeg opencv-python-headless \
            tqdm ipdb lpips pytorch_msssim bitarray scipy matplotlib einops pandas pytest
```

Torch 2.4.1 is the newest release that still builds for Python 3.8 *and* has
`sm_89` kernels for this box's RTX 4090s; upstream's pinned torch 1.12.1+cu116
predates Ada and will not run here at all. `mmcv==1.7.2` is for `mmcv.Config`,
which `run.py` uses (mmcv 2.x moved it to mmengine).

## Usage

Build a ReRF-trainable corpus from `ORBIT_datasets_gaussian` (repo env). With
no `--objects` it prepares the scene configured in `vstream/config.py`, like
every other baseline here:

```bash
conda activate pytorch
python -m baselines.NeVo.orbitnevo.prepare --output-dir ~/nevo_data_g
```

30 frames x 8 calibrated views per object, cropped to the subject's silhouette
and resampled to 1280x960. The crop matters: the corpus frames the whole
60-degree stage, so a standing subject is only ~25% of the frame height and
~6% of the pixels, and ReRF trains at 960x720. Cropping to the silhouette
union (3550x2662 for `basketball`) lifts that to ~9% of pixels and ~72% of
frame height without touching the calibration -- the crop goes into the
intrinsics.

There is a second source, `--source mesh`, which rasterises ORBIT's textured
OBJ sequences on an arbitrary rig (48 views over four elevations by default)
using DeltaStream's nvdiffrast renderer. Better training data -- the prepared
corpus puts all 8 views on one horizontal ring, which leaves a NeRF free to
invent geometry above and below the subject -- but no longer the same pixels
the other baselines see. `RESULTS.md` reports both.

Train the ReRF sequence (nevo env), one object at a time:

```bash
conda activate nevo
python -m baselines.NeVo.orbitnevo.train \
    --corpus ~/nevo_data_g/basketball --expname basketball --frames 24
```

Score the voxels and build the CDF:

```bash
python -m baselines.NeVo.orbitnevo.importance_cdf \
    --config baselines/NeVo/rerf/configs/nevo/basketball.py \
    --out ~/nevo_results/basketball --viewports 300 --block-size 8 --verify
```

`--verify` checks that this module's transcription of ReRF's ray marching
returns exactly the weights the vendored model does; `--block-size` chooses
the granularity a "feature voxel" means (8 is ReRF's codec block, 1 is a
single grid entry).

Sweep the filtering threshold against the quality bar:

```bash
python -m baselines.NeVo.orbitnevo.filter_sweep \
    --config baselines/NeVo/rerf/configs/nevo/basketball.py \
    --out ~/nevo_results/basketball
```

### ReRF's own codec and renderer

The baseline NeVo is measured against is plain ReRF, and upstream can produce it
end to end: `codec/compress.py` writes the compressed bitstream and
`rerf_render.py` decodes it and renders a 360-degree orbit. `orbitnevo/rerf_cli.py`
runs either one with the environment already set up, so no `LD_LIBRARY_PATH`
incantation is needed:

```bash
python -m baselines.NeVo.orbitnevo.rerf_cli codec/compress.py \
    --model_path ~/nevo_runs/g_basketball --expr_name rerf \
    --quality 99 --pca --pca_chs 7,13 --frame_num 30
python -m baselines.NeVo.orbitnevo.rerf_cli rerf_render.py \
    --config configs/nevo/g_basketball.py \
    --compression_path ~/nevo_runs/g_basketball/rerf \
    --render_360 30 --pca --pca_chs 7,13
```

Three things worth knowing:

- **`--pca` must match at both ends**, per upstream's README. It is ~13% smaller
  on this content and is part of ReRF's published method, so it is worth passing.
- **`--render_360` must not exceed the number of compressed frames.** Frame ids
  wrap on `cfg.frame_num`, but the decode stream is pulled sequentially, so
  asking for more exhausts the iterator.
- **The bitstream is the whole deliverable.** A 30-frame object is ~25 MB of
  `.rerf` files plus a 90 kB colour MLP, against ~19 GB of training checkpoints
  -- roughly 800x. It decodes and renders without them, so the checkpoints can be
  deleted once an object is compressed. Only NeVo's own measurements
  (`importance_cdf`, `filter_sweep`, `rd_sweep`) still need them.

Two upstream calls fail against modern dependencies and are patched at runtime by
`nevo/rerf_env.py:patch_dependencies` rather than by editing `rerf/`, which stays
byte-identical: `np.bool` (removed in numpy 1.24) in `codec.compress_utils.decode_pca`,
which every decode goes through, and `imageio.imwrite` on the single-channel depth
maps `rerf_render.py` writes. Without the first, `compress.py` truncates the
bitstream to one frame; without the second, rendering dies after the first frame.

### Watch it

Build and serve the page:

```bash
python -m baselines.NeVo.orbitnevo.render_frames \
    --config baselines/NeVo/rerf/configs/nevo/g_basketball.py
python -m baselines.NeVo.orbitnevo.report --out ~/nevo_report --serve 8752
```

`render_frames.py` writes the conditions the page compares -- plain ReRF and each
visibility-filtered threshold, plus the captured camera -- and `live_demo.py`
streams them as MJPEG (`--nevo-only` for just NeVo's output).

Nothing renders NeRF in a browser and this simulator has no live path by
design, so the page indexes into precomputed renders along the two axes a viewer
moves along, time and viewpoint.

Below the player the same page carries the diagnostics: the prepared views and
their mattes, each trained frame rendered from its reloaded checkpoint beside
its training image, a novel viewpoint the rig never saw, the filtering
difference amplified, and the CDF plots and threshold tables with the numbers
behind them. Pass `--skip-render` to build only the parts that need no GPU;
`--clean` rebuilds the thumbnails and leaves the playback grid alone.

Tests:

```bash
conda activate nevo
python -m pytest baselines/NeVo/nevo_tests -q
```

The model-dependent cases skip unless a trained sequence is present; point
`NEVO_TEST_CONFIG` at one to override which.

## What this deliberately does not do

- **No live streaming.** No V4DS wire protocol, no `system/Server`, no Quest
  conditions in `scripts/user_study.py`. The point of this baseline is a
  reproducible offline measurement of neural-content streaming quality; the
  paper's own headset numbers come from a HoloLens 2 client that does not
  exist here.
- **No 3DGS.** The paper explicitly scopes it out (section 2.2) on the grounds
  that 3DGS needs more bandwidth than NeRF for comparable quality; this repo's
  `baselines/Vega` covers 3DGS streaming.
