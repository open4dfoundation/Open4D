# Vega (research prototype)

A from-scratch implementation of the core techniques from:

> Gunjoong Kim, Seonghoon Park, Jeho Lee, Chanyoung Jung, Hyungchol Jun, Hojung Cha.
> **"Vega: Fully Immersive Mobile Volumetric Video Streaming with 3D Gaussian Splatting."**
> ACM MobiCom 2025.

## What this is

Vega (the paper) is a full mobile streaming *system*: a server-side 3DGS video
encoder plus an Android player app with custom OpenGL ES compute shaders and
NPU delegation. This repository implements the **algorithmic core** of the
paper end-to-end and runnable on a CUDA workstation:

- **Mobile-friendly 3DGS video encoding** (paper §5)
  - Group-of-Volumes (GOV) structure: key frames + residual frames (§5.1)
  - Hierarchical color encoding: big-hash key frames / tiny-hash residual
    frames on top of Instant-NGP-style hash grids + a shared MLP, implemented
    with `tinycudann` (§5.2)
  - Dynamicity-based object filtering using per-object gradient magnitude
    (§5.3, Eq. 1-4)
  - Greedy GOV rate-distortion optimization (§5.4, Eq. 5-7)
- **View-adaptive rendering pipeline** (paper §6)
  - Object-level early culling against the view frustum (§6.2)
  - Priority-based task scheduling across simulated CPU/GPU/NPU processors
    under a per-frame deadline (§6.3, Eq. 8-9)

## What this is *not*

There is no Android device or Android SDK available in this environment, so
this repo does **not** include:

- The Java/C++ Android player app
- OpenGL ES compute shaders / QNN NPU delegation
- On-device FPS measurements on a real phone (Galaxy S24/S25 in the paper)

Instead, the rendering pipeline (§6) is implemented as a real, runnable
simulation: the hash-lookup / MLP / sort / render task costs are *actually
measured* on this workstation's GPU (see `vega/profiling.py`), and the
priority-based scheduler (§6.3) makes real accept/skip/processor-assignment
decisions against a target per-frame deadline using those measured costs.
This validates the *algorithm* (object selection under a time budget), even
though the absolute latencies come from a desktop GPU rather than a mobile
SoC. Swap in on-device profiled numbers later if/when a phone is available.

## No dataset yet

The encoder operates on a dataset-agnostic in-memory representation
(`vega.gaussians.GaussianSet`, a per-frame set of Gaussians with an
`object_id` per Gaussian). Until a real multi-view capture is available,
`vega/synthetic.py` generates a small synthetic multi-object dynamic scene
(static background + a few moving/rotating foreground objects, rendered with
the real `diff_gaussian_rasterization` CUDA rasterizer) so the full pipeline
can be exercised and validated end-to-end.

To plug in a real dataset later:

1. Reconstruct per-frame (or key + tracked) 3DGS scenes the way the existing
   `run_queen` / `run_3dgstream` pipelines in this environment already do.
2. Produce a per-Gaussian `object_id` (e.g. via Gaussian Grouping, or the
   fallback spatial-temporal clustering in `vega/segmentation.py`).
3. Wrap the result as a sequence of `vega.gaussians.GaussianSet` +
   `vega.cameras.Camera` and feed it to `vega.encoder.VegaEncoder`.

## Layout

```
vega/            core library (encoding + rendering-pipeline simulation)
scripts/         runnable demos / ablations, reproduce paper-style plots
tests/           unit tests for the GOV optimizer, culling, scheduler, etc.
configs/         default hyperparameters (mirrors the paper's §7 values)
```

## Quickstart

```bash
conda activate open4d-gs   # has torch+cuda, tinycudann, diff_gaussian_rasterization
cd ~/vega
pip install -e . --no-deps
pytest tests -q
python scripts/train_synthetic_demo.py --outdir runs/synthetic_demo
python scripts/ablation_color_encoding.py --outdir runs/ablation
python scripts/eval_rendering_pipeline.py --outdir runs/rendering
```
