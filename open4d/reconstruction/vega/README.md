# Vega (ORBIT adaptation)

A lean baseline implementing the core techniques of:

> Gunjoong Kim, Seonghoon Park, Jeho Lee, Chanyoung Jung, Hyungchol Jun, Hojung Cha.
> **"Vega: Fully Immersive Mobile Volumetric Video Streaming with 3D Gaussian Splatting."**
> ACM MobiCom 2025.

Vega is a 3D Gaussian Splatting (3DGS) volumetric video system, structurally
different from every other baseline in this repo (which stream Draco-encoded
point clouds or HEVC-encoded RGBD video). This directory only implements the
paper itself, driven by the real ORBIT corpus:

- **Mobile-friendly 3DGS video encoding** (paper §5): Group-of-Volumes (GOV)
  key/residual structure, hierarchical color encoding (Instant-NGP-style big
  hash for key frames / tiny hash for residual frames + a shared MLP),
  dynamicity-based object filtering (Eq. 1-4), and a greedy GOV
  rate-distortion optimizer (Eq. 5-7).
- **View-adaptive rendering pipeline** (paper §6): object-level early culling
  against the view frustum, and priority-based task scheduling across
  simulated CPU/GPU/NPU processors under a per-frame deadline (Eq. 8-9),
  using per-task latencies **actually measured** on this workstation's GPU/CPU
  (see `vega/profiling.py`) rather than a real mobile SoC.

## What this deliberately does *not* do

Unlike DeltaStream/ViVo/LiVo/NAVA, this baseline does **not**:
- implement the paper's Android/Java/C++/OpenGL ES/QNN mobile player app —
  there's no Android device or SDK in this environment;
- speak this repo's V4DS wire protocol, plug into `system/Server` (Node), or
  appear in `scripts/user_study.py`'s Quest-headset conditions.

Those would require either physical Android/Quest hardware or reproducing a
large amount of the harness's live-streaming machinery for a representation
(3D Gaussians) it wasn't designed to carry. Kept out on purpose, so this stays
a lean, direct implementation of the paper rather than a strained fit into
the mesh/point-cloud ladder harness.

What it *does* give you: a real encoder driven by the real ORBIT dataset, and
a live demo — encode + decode + render pipeline running on this GPU box,
streamed as MJPEG to a browser on any other machine — so the whole thing is
watchable end-to-end, not just unit-tested.

## Layout

```
vega/            vendored engine (see vega/ENGINE_README.md for full detail)
  datasets/orbit_gaussian.py   loader for ORBIT_datasets_gaussian (default)
  datasets/orbit.py            loader for ORBIT_datasets_rgbd
vega_tests/       the engine's own unit/integration tests
orbitvega/
  prepare.py       offline step: encode ORBIT objects into a Vega bitstream
  live_demo.py      live demo: encode -> serve -> render -> MJPEG stream
citation.txt
```

## Usage

From the repo root, in an environment with torch+CUDA, tinycudann,
`diff_gaussian_rasterization`, and `simple_knn` (this project used the
`open4d-gs` conda env already present on the GPU machine):

```bash
pip install -e .   # registers baselines.Vega.vega as an importable package

# Offline: encode one or more ORBIT objects into a Vega bitstream
python -m baselines.Vega.orbitvega.prepare \
  --output-dir results/vega-run/prepared \
  --objects basketball

# Live: encode + stream to a browser on another machine
python -m baselines.Vega.orbitvega.live_demo \
  --scene basketball --n-frames 30 --mjpeg-port 8767
# then open http://<this-machine-ip>:8767/ in a browser
```

## Input corpus

Both entry points take `--dataset-format`, defaulting to `gaussian`:

| format | root (default on this machine) | geometry |
| --- | --- | --- |
| `gaussian` | `/media/frozzzen/DataDrive/ORBIT_datasets_gaussian` | 8 calibrated RGB views per frame, no depth — geometry from silhouette carving + a short photometric fit |
| `rgbd` | `/media/frozzzen/DataDrive/ORBIT_datasets_rgbd/level_1` | 4 RGBD cameras per frame — geometry unprojected from depth |

`gaussian` is the default because `ORBIT_datasets_gaussian` is the corpus this
project built *for* Gaussian training: per object, 30 frames x 8 views of
4096x3072 RGB on a black background, with OpenCV intrinsics/extrinsics in
nerfstudio-style `transforms.json` files, and no depth or point clouds at all
(`contains_depth: false`, `contains_pointclouds: false`).

Since there is no depth to unproject, `vega/datasets/orbit_gaussian.py`
recovers each frame's geometry from the 8 silhouettes:

1. threshold the black background into per-view foreground masks;
2. carve a visual hull on a voxel grid inside the object's known bounding box
   (`--grid-res`, default 224 voxels along the longest axis, ~8 mm/voxel for a
   standing person);
3. keep the hull's *surface* voxels only — interior Gaussians are invisible
   from every camera but would still cost bitrate;
4. colour each point with the mean of the views it is visible from, z-buffered
   against the hull so the back of the subject isn't painted with its front;
5. initialize Gaussians from that colored point cloud (the same
   point-to-Gaussian initialization the RGBD path uses), then optionally run
   `--refine-iters` (default 200) iterations of photometric 3DGS fitting
   against the 8 real views using the paper's own loss (Eq. 2).

An 8-view coplanar ring cannot carve concavities that are only visible from
above or below (under a chin, an arm held against a torso); the photometric
refinement mops up some of that, and a full 3DGS training run with
densification would do better — deliberately out of scope, since the point
here is Vega's encoder and rendering pipeline, not a reconstruction
contribution.

Everything Vega itself contributes — segmentation, GOV key/residual
structure, hierarchical color encoding, dynamicity filtering, view-adaptive
rendering — runs downstream of the loader and is identical for both corpora.

## Scene objects

Both corpora carry the same object names (`dancer`, `basketball`, `mitch`,
`thomas`, `UMA0`-`UMA4`), matching `vstream.config.OBJECTS` exactly. Vega's own
internal "object-level selective computation" (paper §4.1 — segmenting a
scene into semantically meaningful Gaussian clusters, e.g. a basketball
player's limbs vs. the ball vs. the court) operates *within* each of these
scene objects and isn't exposed at that granularity here; each configured
scene object gets its own independently-encoded GOV sequence.
