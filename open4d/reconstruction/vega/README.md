# Vega (ORBIT adaptation)

Adaptation of:

> Gunjoong Kim, Seonghoon Park, Jeho Lee, Chanyoung Jung, Hyungchol Jun, Hojung Cha.
> **"Vega: Fully Immersive Mobile Volumetric Video Streaming with 3D Gaussian Splatting."**
> ACM MobiCom 2025.

CUDA implementation of Vega's encoding and rendering algorithms for ORBIT:

- GOV key/residual frames, hierarchical hash-grid colour encoding, dynamicity
  filtering, and greedy rate-distortion optimisation (paper §5, Eq. 1–7).
- Frustum culling and deadline-based scheduling across simulated CPU/GPU/NPU
  processors (paper §6, Eq. 8–9).

Task latencies come from workstation profiling in `vega/profiling.py`, not a
mobile SoC. The Android player, OpenGL ES/QNN implementation, and on-device
measurements are not included. `orbitvega.live_demo` provides browser playback
through MJPEG.

## Layout

```
vega/            vendored engine
  datasets/orbit_gaussian.py   loader for ORBIT_datasets_gaussian (default)
  datasets/orbit.py            loader for ORBIT_datasets_rgbd
vega_tests/       the engine's own unit/integration tests
orbitvega/
  prepare.py       offline step: encode ORBIT objects into a Vega bitstream
  live_demo.py      live demo: encode -> serve -> render -> MJPEG stream
citation.txt
```

## Usage

From the repo root, use an environment with PyTorch/CUDA, `tinycudann`,
`diff_gaussian_rasterization` and `simple_knn`; see
[`../gs_tools/README.md`](../gs_tools/README.md) for setup.

```bash
pip install -e .
export PYTHONPATH="$PWD/open4d/reconstruction/vega${PYTHONPATH:+:$PYTHONPATH}"

# Rebuild after native source updates to apply the near-plane fix.
python -m pip install --no-build-isolation --no-deps --force-reinstall \
  open4d/reconstruction/gs_tools/rasterizers/diff-gaussian-rasterization

# Offline: encode one or more ORBIT objects into a Vega bitstream
python -m orbitvega.prepare \
  --dataset-root /path/to/ORBIT_datasets_gaussian \
  --output-dir ../vega-run/prepared \
  --objects basketball

# Live: encode + stream to a browser on another machine
python -m orbitvega.live_demo \
  --dataset-root /path/to/ORBIT_datasets_gaussian \
  --scene basketball --n-frames 30 --mjpeg-port 8767
# then open http://<this-machine-ip>:8767/ in a browser
```

For the corpus-dependent tests, set `OPEN4D_ORBIT_GAUSSIAN_ROOT` to the
accessible dataset root and run `pytest open4d/reconstruction/vega/vega_tests`.

## Input corpus

Both entry points take `--dataset-format`, defaulting to `gaussian`:

| format | root (default on this machine) | geometry |
| --- | --- | --- |
| `gaussian` | `/media/frozzzen/DataDrive/ORBIT_datasets_gaussian` | 8 calibrated RGB views per frame, no depth — geometry from silhouette carving + a short photometric fit |
| `rgbd` | `/media/frozzzen/DataDrive/ORBIT_datasets_rgbd/level_1` | 4 RGBD cameras per frame — geometry unprojected from depth |

The Gaussian corpus contains 30 frames per object, eight 4096x3072 RGB views
on black backgrounds, and OpenCV calibration in nerfstudio-style
`transforms.json`. It has no depth or point clouds.

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

Coplanar cameras cannot recover concavities visible only from above or below.
Photometric refinement does not include a full densification training pass.
Both loaders feed the same encoder and rendering pipeline.

## Scene objects

Both corpora carry the same object names (`dancer`, `basketball`, `mitch`,
`thomas`, `UMA0`-`UMA4`), matching `vstream.config.OBJECTS` exactly. Vega's own
internal "object-level selective computation" (paper §4.1 — segmenting a
scene into semantically meaningful Gaussian clusters, e.g. a basketball
player's limbs vs. the ball vs. the court) operates *within* each of these
scene objects and isn't exposed at that granularity here; each configured
scene object gets its own independently-encoded GOV sequence.
