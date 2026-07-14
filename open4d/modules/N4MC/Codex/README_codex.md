# README_codex.md

## What this is

This document tells Codex exactly how to restart the TSDF compression project cleanly.

The repository already contains an older autoencoder/decoder/training pipeline. Treat that code as historical reference only. The new implementation should start fresh and focus on a **geometry-aware TSDF compression baseline** that is clean, reproducible, and easy to extend.

---

## Primary objective

Build a **new neural compression system for TSDF tensors** generated from the mesh-to-TSDF preprocessing pipeline.

### First milestone

Implement **single-frame TSDF-only compression** with:

- explicit latent quantization,
- explicit rate term,
- surface-aware distortion,
- sign-aware distortion,
- mesh reconstruction evaluation.

### Later milestones

- offset/deformation compression,
- block sparsity improvements,
- hyperprior or stronger entropy model,
- temporal/inter-frame coding.

---

## Input data

The current preprocessing saves `.npz` files with fields like:

- `sdf`: TSDF tensor, typically `(R+1, R+1, R+1, 1)`, stores `/media/frozzzen/DataDrive/ChromeDownloads/Mesh_dataset/dancer_scaled/TSDF_128/data/TSDF`
- `offset`: optional deformation tensor, typically `(R+1, R+1, R+1, 3)`, stores `/media/frozzzen/DataDrive/ChromeDownloads/Mesh_dataset/dancer_scaled/TSDF_128/data`

For the new baseline:

- use only `sdf`,
- assume values are clamped or truncated to `[-1, 1]`,
- preserve tensor layout explicitly and document it.

---

## Recommended implementation plan

### Step 1 — Find and quarantine the old code

Locate the previous autoencoder/decoder/training scripts.

Keep only clearly reusable pieces such as:

- basic file loading,
- logging helpers,
- checkpoint utilities,
- environment setup.

Do not copy over:

- the old model,
- the old loss formulation,
- old normalization assumptions,
- old training structure.

---

### Step 2 — Create a clean module structure

A good target structure is:

```text
configs/
data/
models/
losses/
training/
evaluation/
utils/
scripts/
```

Recommended modules:

- `data/dataset.py`
- `data/block_sampling.py`
- `models/tsdf_autoencoder.py`
- `models/quantization.py`
- `models/entropy.py`
- `losses/tsdf_losses.py`
- `training/train.py`
- `training/validate.py`
- `evaluation/reconstruct.py`
- `evaluation/metrics.py`

---

### Step 3 — Implement the dataset first

The dataset layer should:

- load `.npz`,
- read `sdf`,
- validate shapes and value ranges,
- convert to PyTorch tensor,
- optionally extract 3D blocks,
- optionally sample blocks with near-surface preference.

The dataset must expose enough information to debug:

- original file path,
- shape,
- min/max,
- narrow-band voxel ratio.

---

### Step 4 — Implement the first model baseline

Implement a **new** 3D encoder-decoder compression model.

Requirements:

- input: TSDF tensor,
- bottleneck latent,
- explicit quantization,
- decoder output same TSDF layout,
- output includes reconstruction and rate-related values.

Minimum acceptable first model:

- 3D conv encoder,
- latent tensor,
- STE or additive-noise quantization in training,
- decoder,
- simple entropy proxy.

Preferred next step:

- factorized prior or hyperprior.

---

### Step 5 — Use geometry-aware losses

The new loss should not be only MSE.

Required components:

#### 1. Reconstruction loss

Use L1 or smooth L1 on TSDF values.

#### 2. Narrow-band weighted loss

Give much more weight to voxels near zero level set.

Examples:

- hard mask on `|tsdf| < tau`, or
- soft weighting such as `exp(-alpha * abs(tsdf))`.

#### 3. Sign consistency loss

Penalize sign errors that can change topology.

Suggested total loss:

```text
L = lambda_rate * rate
  + lambda_rec * rec_loss
  + lambda_band * band_loss
  + lambda_sign * sign_loss
```

Keep weights configurable.

---

### Step 6 — Train with real rate-distortion logic

This must be a real compression training loop.

That means:

- quantized latent,
- bitrate estimate,
- logged RD terms,
- support multiple lambda values.

Log at least:

- total loss,
- rate,
- rec loss,
- band loss,
- sign loss,
- validation metrics,
- best checkpoint.

---

### Step 7 — Evaluate in both tensor and mesh space

Tensor-space metrics:

- MAE,
- MSE,
- narrow-band MAE,
- sign accuracy,
- optional PSNR.

Mesh-space metrics:

- reconstruct mesh from predicted TSDF,
- compute Chamfer distance,
- optional Hausdorff,
- optional normal consistency.

Save reconstructed meshes for inspection.

---

### Step 8 — Add block-based mode

After the full-volume baseline works, add block-based coding.

Recommended behavior:

- split volume into `16^3` or `32^3` blocks,
- optionally skip trivial blocks,
- code important blocks preferentially,
- stitch reconstructed blocks back together.

This is likely the strongest practical path for high compression ratio.

---


---

## Acceptance criteria for the first complete baseline

The restart is successful when all of the following are true:

- a new dataset pipeline exists,
- a new model exists,
- the model uses quantized latent,
- the training includes a rate term,
- losses include surface-aware and sign-aware components,
- validation reports tensor metrics,
- evaluation reconstructs meshes,
- at least one mesh metric is reported,
- the result is reproducible from config + command.

---

## Suggested commands to support

These are examples. The exact CLI may differ.

### Train

```bash
python -m training.train --config configs/train_tsdf.yaml
```

### Validate

```bash
python -m training.validate --config configs/eval_tsdf.yaml --checkpoint path/to/best.pt
```

### Reconstruct meshes

```bash
python -m evaluation.reconstruct --config configs/eval_tsdf.yaml --checkpoint path/to/best.pt
```

---

## Notes for Codex

- Prefer a clean restart over adapting the old network.
- Keep tensor axis ordering explicit everywhere.
- Fail loudly on shape mismatches.
- Do not claim compression without quantization and a rate term.
- Do not use only global MSE as the target.
- Build the system in runnable milestones.

Read `AGENT.md` for behavioral constraints and `TASKS.md` for the implementation checklist.