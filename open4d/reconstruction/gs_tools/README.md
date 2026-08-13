# gs-tools

Gaussian-splatting free-viewpoint-video reconstruction. Two upstream methods run
side by side in one environment, against one CUDA rasterizer:

- **QUEEN** (NVlabs) — quantized, entropy-coded streaming FVV
- **3DGStream** — on-the-fly per-frame training with a neural transformation cache

Both take multi-view video plus COLMAP cameras and produce a temporal Gaussian
representation, which is why they live under `reconstruction/` and not
`codecs/`, even though each carries its own compression stage.

## Licensing: this directory is not MIT

Open4D is MIT. This module is not, and neither is anything built from it:

| Component | License | Effect |
| --- | --- | --- |
| `upstream/queen` | NVIDIA License | **non-commercial (research or evaluation) only** |
| `upstream/3dgstream` | MIT | — |
| the rasterizer (derived from QUEEN's) | NVIDIA License | non-commercial; derivative works inherit the limitation |
| inria `diff-gaussian-rasterization` at its base | Gaussian-Splatting research license | non-commercial |

Nothing here may be used commercially. See [THIRD_PARTY.md](THIRD_PARTY.md) for
pinned commits and the full attribution list.

## Layout

```
gs_tools/          Open4D's own code (MIT): CLI, data adapters, IO, metrics
upstream/queen     submodule, pinned
upstream/3dgstream submodule, pinned
patches/           every change we make to upstream, as diffs
scripts/setup.sh   fetch, patch, build, verify
docs/plan.md       what is built, what is gated, and on what evidence
```

Upstream is modified **only** by the patch series in `patches/`, applied by
`scripts/setup.sh`. So the submodule working trees are expected to be dirty after
setup, and `gs-tools doctor` marks their commits `-dirty` for that reason; what
must stay true is that the diff equals the patch series and nothing else. Three
patches exist today, 85 lines total — versus the 3,700 files vendoring these two
repositories would add:

| Patch | Why |
| --- | --- |
| `queen/0001-lazy-midas-import.patch` | `train.py` and `scene/utils.py` import MiDaS at module scope, which drags in `timm==0.6.13`. Made lazy so training reads cached depth maps without it. |
| `3dgstream/0001-rename-rasterizer-import.patch` | one import line, following the rename below |
| `3dgstream-rasterizer/0001-rename-package.patch` | both methods' plain rasterizers install as `diff_gaussian_rasterization`, and one environment cannot hold two. 3DGStream's becomes `gstream_rasterization`, so neither method's semantics change. |

Configs default to upstream's own (`upstream/queen/configs/dynerf.yaml`), not to
copies here, because a file listing every hyperparameter of a method is exactly
what drifts silently from the code it configures.

## Setup

Requires Linux, an NVIDIA GPU, CUDA 12.6 on `PATH`, and the same toolchain the
[root requirements-gpu.txt](../../../requirements-gpu.txt) describes.

```bash
conda env create -f environment.yml     # creates open4d-gs
conda activate open4d-gs
pip install torch==2.7.0 torchvision==0.22.0 --index-url https://download.pytorch.org/whl/cu126
pip install -r requirements.txt
pip install -e .
./scripts/setup.sh                      # submodules, patches, CUDA extensions
gs-tools doctor                         # verify the environment
```

`open4d-gs` is deliberately a second environment, not the shared `open4d` one.
Three of QUEEN's pins cannot coexist with the codec set: `timm==0.6.13` (n4mc
tracks current timm), `opencv-python-headless` (the codecs install
`opencv-python`, and the two distributions both provide `cv2`), and
`einops==0.6.0`. Merging them would mean unpinning a research dependency to suit
an unrelated codec.

### MiDaS depth priors

QUEEN's depth priors need `timm==0.6.13`, which is a 2023 release. Rather than
holding the training environment to it, depth-map generation is a separate step
in its own environment:

```bash
conda create -n open4d-gs-midas python=3.12 && conda activate open4d-gs-midas
pip install -r requirements-midas.txt
gs-tools depth-prior -s <scene>         # writes depth maps beside the images
```

`gs-tools depth-prior` is not wired up yet; until it is, run MiDaS from
`upstream/queen` directly in that environment. The patch that makes the import
lazy is already in place, so training does not need timm either way.

### tiny-cuda-nn (3DGStream only)

The neural transformation cache needs tiny-cuda-nn, which compiles against the
exact torch build and cannot be resolved from a version number. `setup.sh`
builds it; the equivalent by hand, on the 4090 box:

```bash
export PATH=/usr/local/cuda/bin:$PATH
export TCNN_CUDA_ARCHITECTURES=89
pip install --no-build-isolation git+https://github.com/NVlabs/tiny-cuda-nn/#subdirectory=bindings/torch
```

QUEEN does not use it. Skip this and everything except `--method 3dgstream`
works.

## Running

```bash
gs-tools data -s data/dynerf/coffee_martini --layout dynerf   # inspect a scene
gs-tools depth-prior -s data/dynerf/coffee_martini            # QUEEN only

gs-tools train --method queen -s data/dynerf/coffee_martini -m output/coffee_queen

# 3DGStream is two-stage: a static timestep-0 model, then per-frame training
gs-tools train --method 3dgstream --stage init \
               -s data/dynerf/coffee_martini -m output/coffee_gstream
gs-tools train --method 3dgstream \
               -s data/dynerf/coffee_martini -m output/coffee_gstream

gs-tools render   --method queen -s data/dynerf/coffee_martini -m output/coffee_queen
gs-tools manifest -m output/coffee_queen
```

`--dry-run` prints the translated upstream command without running it, which
works without a GPU and is the way to check an adapter off the training host.
Anything after `--` goes to the upstream trainer unchanged:

```bash
gs-tools train --method 3dgstream -s <scene> -m <run> -- --eval --resolution 2
```

`train` and `render` delegate to the upstream trainers, translating arguments and
running each from its own checkout as a subprocess. They cannot share a process:
both upstreams use flat imports and both define `scene`, `utils`, and
`arguments`.

Metrics are Open4D's, one implementation for both methods, because upstream's do
not agree — QUEEN rounds to 8 bits before taking the MSE and 3DGStream does not,
so their published PSNRs are not directly comparable.
`gs_tools.metrics.image.evaluate` reports both conventions side by side. Every run
writes a `manifest.json` recording dataset, frame range, upstream commit, config,
environment, timing, and quality, per
[docs/artifacts.md](../../../docs/artifacts.md).

## Data and outputs

Datasets, checkpoints, and runs stay out of git; the root `.gitignore` covers
`open4d/reconstruction/*/{data,output,outputs,logs,checkpoints}/`. Fetch
externally stored inputs with `scripts/fetch_artifact.sh` at the repo root.

## Status

Phase 1 of the plan in [docs/plan.md](docs/plan.md). Working: the module, the
environment definition, the patch series, the CLI and its argument translation
(checkable with `--dry-run`), manifests, metrics, scene detection.

Not done yet: `setup.sh` has not been run on a GPU host, so no extension has been
built and neither method has trained through this module. The unified rasterizer
is phase 2, gated on the parity test in the plan — until it passes, `setup.sh`
builds all four extensions so the comparison is runnable. `gs-tools depth-prior`
and an export verb are stubs.
