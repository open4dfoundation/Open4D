# Integration plan

Bringing QUEEN and 3DGStream into one module, one environment, one rasterizer.
This records the measurements the design rests on, so a later reader can tell
which choices were forced by the code and which were judgment.

## Phases

| Phase | Work | Gate |
| --- | --- | --- |
| 0 | Module skeleton, `open4d-gs` environment, ignore rules, licensing notes | `conda env create` succeeds; `gs-tools doctor` runs with no upstream code present |
| 1 | Pinned submodules, `setup.sh`, build **upstream's own** extensions unmodified | both methods train one scene, unmodified, in the same environment |
| 2 | Unified rasterizer: patch QUEEN's grad fork, add the frustum flags, repoint 3DGStream | parity test below passes; then the other two extensions are deleted |
| 3 | Shared data adapters, IO, metrics, manifests, full CLI | one command trains either method; metrics comparable across methods |
| 4 | Optional: `open4d.core` bridge, ply viewer in `examples/visualization/` | — |

Phase 1 proves the environment before anything touches CUDA. Those are the two
hard parts and debugging them together is what sprawled the previous attempt.

### Phase 1 status

Done: pinned submodules, the patch series, `setup.sh`, the CLI and its argument
translation, manifests, metrics, scene detection. Not done: `setup.sh` has not run
on a GPU host, so nothing is built and neither method has trained through the
module.

Two things phase 1 turned up that the original plan did not anticipate:

- **The plain rasterizers collide.** QUEEN's vendored fork and 3DGStream's both
  install as `diff_gaussian_rasterization`, so one environment cannot hold both,
  which would have blocked the phase-1 gate on its own. Resolved by renaming
  3DGStream's to `gstream_rasterization` — a patch to its `setup.py` and one
  import line, changing no kernel. The alternative, pointing QUEEN at 3DGStream's
  extension, would have silently changed QUEEN's frustum culling (below).
- **QUEEN imports MiDaS at module scope**, in `train.py` and `scene/utils.py`, so
  `timm==0.6.13` was an import-time dependency of training and the separate MiDaS
  environment would not have helped. Both call sites were already inside
  conditionals; only the imports needed moving.

## Why one rasterizer is cheap

Changed lines against `graphdeco-inria/diff-gaussian-rasterization` at its
default branch:

| File | inria | QUEEN `diff-gaussian-rasterization` | QUEEN `gaussian-rasterization-grad` | 3DGStreamRasterizer |
| --- | --- | --- | --- | --- |
| `cuda_rasterizer/forward.cu` | 454 | **0** | +178 | +17 |
| `cuda_rasterizer/backward.cu` | 656 | **0** | +519 | +67 |
| `cuda_rasterizer/rasterizer_impl.cu` | 433 | **0** | +69 | +36 |
| `rasterize_points.cu` | 216 | **0** | +106 | +19 |
| `cuda_rasterizer/auxiliary.h` | 174 | 2 | 4 | **0** |
| `diff_gaussian_rasterization/__init__.py` | 221 | **0** | +82 | +14 |

Two conclusions:

1. QUEEN's plain `diff-gaussian-rasterization` **is** inria's, except one line of
   `auxiliary.h`. It does not need to exist separately.
2. QUEEN's `gaussian-rasterization-grad` is already a functional superset of
   3DGStream's fork. 3DGStream's whole delta is a depth output plus depth
   gradients (`bwd_depth`, `dLd_ddepth`); QUEEN's fork has depth forward *and*
   backward (`dL_dpix_depth` -> `dL_ddepth`, wired through its autograd
   `backward` as `grad_depth`), plus 2D flow, per-Gaussian influence and count,
   alpha backward, and pixel/color/cov/update masks.

So unification is not a CUDA merge. It is: build QUEEN's grad extension, and
rewrite 3DGStream's `gaussian_renderer.render` to call it — `bwd_depth=True`
becomes `render_depth=True`, and the return unpacks as
`(image, flow2D, infl, count_infl, depth, alpha, radii)` instead of
`(image, radii, depth)`, with empty tensors for the unused mask and flow inputs.

## The trap: three different frustum tests

`in_frustum` in `cuda_rasterizer/auxiliary.h` gates whether a Gaussian is
rendered at all, and every fork changed it:

| Variant | Predicate |
| --- | --- |
| inria, 3DGStream | `p_view.z <= 0.2f` (NDC bounds test commented out) |
| QUEEN grad | `p_view.z <= 0.2f \|\| NDC bounds` |
| QUEEN plain | `p_view.z <= 4.0f \|\| NDC bounds` |

Pointing 3DGStream at QUEEN's kernel silently enables bounds culling and changes
its images near frustum edges. The near plane and the bounds test become
`raster_settings` fields, defaulting per method to upstream behavior.

Also: QUEEN's fork drops `const` on `colors_precomp` and writes into it. Confirm
3DGStream does not reuse that buffer before repointing it.

## Parity test (the phase 2 gate)

One scene, one camera, fixed seed. Render through the upstream extensions and
through the unified one:

- vanilla path (all flags off) must be **bit-exact** — max abs pixel diff 0
- depth, alpha, and flow paths within 1e-5
- a 500-step training run per method whose loss curve matches upstream's

Only then are the upstream extensions removed. Until that passes, `setup.sh`
builds all three so the comparison is runnable.

Open risk: `dLd_ddepth` (3DGStream) and `dL_ddepth` (QUEEN) are both
depth-gradient paths, but it is not yet verified that they compute the same
quantity — expected versus median depth, alpha-weighted or not. If they differ,
the Python shim becomes real CUDA work. The parity test is what catches it.

## What can be shared, measured

Changed lines between the two upstreams' copies of the files they inherit from
3DGS:

| File | QUEEN | 3DGStream | Δ | Disposition |
| --- | --- | --- | --- | --- |
| `utils/system_utils.py` | 28 | 28 | **0** | share |
| `lpipsPyTorch/__init__.py` | 21 | 21 | **0** | share |
| `scene/colmap_loader.py` | 294 | 282 | 34 | share |
| `utils/sh_utils.py` | 117 | 148 | 37 | share |
| `utils/loss_utils.py` | 117 | 74 | 63 | QUEEN superset |
| `scene/cameras.py` | 171 | 72 | 113 | adapter |
| `utils/camera_utils.py` | 187 | 82 | 129 | adapter |
| `utils/graphics_utils.py` | 244 | 76 | 170 | adapter |
| `scene/dataset_readers.py` | 484 | 253 | 353 | adapter |
| `arguments/__init__.py` | 424 | 135 | 379 | per-method |
| `utils/image_utils.py` | 450 | 19 | 436 | per-method |
| `scene/gaussian_model.py` | 1837 | 790 | **2608** | never merge |
| `train.py` | 1287 | 252 | **1338** | never merge |

"One module" therefore means shared plumbing — extension, environment, data
adapters, IO, metrics, CLI, manifests — with the two Gaussian models and
trainers left as pinned upstream forks. Merging `gaussian_model.py` would mean
reimplementing both papers.

## Environment deviations to watch

Neither upstream was tested where we are running them:

| | QUEEN | 3DGStream | here |
| --- | --- | --- | --- |
| Python | >=3.11 declared | 3.8 tested | 3.12 |
| torch | unpinned | 2.0.1+cu118 tested | 2.7.0+cu126 |
| tinycudann | unused | 1.7 tested | built against torch 2.7 |

Known consequences: `timm==0.6.13` is a 2023 release and is isolated in the MiDaS
environment for that reason; 3DGStream calls `torch.compile` on the NTC, which
behaves differently on 2.7 than on 2.0; tiny-cuda-nn needs
`TCNN_CUDA_ARCHITECTURES=89` for the 4090s and a compiler CUDA 12.6 accepts
(gcc <= 12). `gs-tools doctor` reports what is actually installed, and every run
manifest records it.

## Rejected: port both to `gsplat`

nerfstudio's `gsplat` is pip-installable, maintained, and has absgrad and
`RGB+ED` depth. It does not expose QUEEN's per-Gaussian influence and count or
2D-flow gradients, and re-deriving published numbers through a different kernel
is a research risk rather than a refactor. Revisit only if maintaining the fork
becomes the actual cost.
