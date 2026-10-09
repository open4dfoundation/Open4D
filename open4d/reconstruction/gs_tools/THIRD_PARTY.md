# Third-party components

QUEEN and 3DGStream are vendored under `open4d/reconstruction/`. Shared
rasterizers, `simple-knn`, `glm` and `SIBR_viewers` live in this module.

The vendored trees include local patches. `scripts/setup.sh` verifies patches
rather than applying them. Run checks from the repository root with
`git apply --directory=<target>`: running inside a target directory can silently
skip paths and report success without checking any files.

## Pins

Paths are repository-relative; commits identify the vendored upstream revisions.

| Path | Upstream | Vendored from | Date | License |
| --- | --- | --- | --- | --- |
| `open4d/reconstruction/queen` | https://github.com/NVlabs/queen | `4d761ae6a2893f220cd049fac6c97bea13de9b98` | 2026-02-11 | NVIDIA License (non-commercial) |
| `open4d/reconstruction/3dgstream` | https://github.com/SJoJoK/3DGStream | `747ddfef646edf3ea628f2bd13b7bedce7c5fe47` | 2024-11-18 | MIT |

## Shared components inside this module

Shared dependency paths are relative to `gs_tools/`.

| Path | Original upstream | Built | Why |
| --- | --- | --- | --- |
| `rasterizers/gaussian-rasterization-grad` | no separate upstream; NVIDIA's extension of inria's, via QUEEN | yes | base of the unified rasterizer; the intended survivor |
| `rasterizers/diff-gaussian-rasterization` | https://github.com/graphdeco-inria/diff-gaussian-rasterization, via QUEEN | yes, for now | QUEEN's plain path; parity reference |
| `rasterizers/gstream-rasterization` | https://github.com/SJoJoK/3DGStreamRasterizer | yes, for now | 3DGStream's depth-gradient fork; parity reference |
| `simple-knn` | https://gitlab.inria.fr/bkerbl/simple-knn, via QUEEN | yes | KNN for point-cloud init; QUEEN's copy, whose added `<float.h>`/`<cfloat>` includes are what let it compile here |
| `glm` | https://github.com/g-truc/glm, via the rasterizers | header-only | all three rasterizers vendored byte-identical trees; each `setup.py` now includes `../../glm/` |
| `SIBR_viewers` | https://gitlab.inria.fr/sibr/sibr_core | **no** | C++ viewer with its own toolchain; Open4D uses `examples/visualization/` |

`MiDaS` (https://github.com/isl-org/MiDaS) stays inside the QUEEN tree at
`open4d/reconstruction/queen/MiDaS`, run from its own environment.

## Our patches

Each patch is verified against the tree named here, from the repository root,
with `--directory` set to that path.

| Patch | Target | Effect |
| --- | --- | --- |
| `queen/0001-lazy-midas-import.patch` | `open4d/reconstruction/queen` | defers MiDaS imports in `train.py` and `scene/utils.py`, allowing training without `timm==0.6.13`. Uses resized `<scene>/depth_priors/<camera>.npy` maps when available for every training camera; `gs-tools depth-prior` generates them in a separate environment |
| `queen/0002-finite-mse-gradient.patch` | `open4d/reconstruction/queen` | adds `1e-12` inside `mse_loss`'s square root in `utils/loss_utils.py` to prevent infinite gradients at zero error from corrupting Gaussian gates and positions |
| `queen/0003-explicit-render-path.patch` | `open4d/reconstruction/queen` | uses `render_path.npy` beside `poses_bounds.npy` when available, replacing the LLFF spiral, which points away from scenes with cameras arranged around an object |
| `queen/0004-gate-init-without-change.patch` | `open4d/reconstruction/queen` | guards median- and mean-based gate denominators against zero, preventing NaN gates and positions when Gaussians are unchanged |
| `3dgstream/0001-rename-rasterizer-import.patch` | `open4d/reconstruction/3dgstream` | imports `gstream_rasterization` in `gaussian_renderer/__init__.py` to avoid a package-name clash with QUEEN's `diff_gaussian_rasterization` |
| `3dgstream/0002-config-supplies-defaults.patch` | `open4d/reconstruction/3dgstream` | makes `--read_config` supply argparse defaults so explicit frame ranges, NTC paths and options take precedence |
| `3dgstream/0003-blender-camera-translation.patch` | `open4d/reconstruction/3dgstream` | flips camera-to-world axes before inversion when reading Blender/NeRF `transforms_*.json`, correcting translations for cameras not aimed at the origin; rotations are unchanged |
| `3dgstream/0004-empty-added-gaussians.patch` | `open4d/reconstruction/3dgstream` | slices added Gaussians from the base count instead of `-len(added)`, preventing pruning crashes when no Gaussians were added |
| `3dgstream-rasterizer/0001-rename-package.patch` | `gs_tools/rasterizers/gstream-rasterization` | renames the installed package to `gstream_rasterization` |
| `3dgstream-rasterizer/0002-cstdint-include.patch` | `gs_tools/rasterizers/gstream-rasterization` | adds `<cstdint>` to `cuda_rasterizer/rasterizer_impl.h` for `std::uintptr_t` and fixed-width integer types on GCC 13 / CUDA 12.6 |

None of them changes what any CUDA kernel computes.

## Licenses

- **NVIDIA License** (`open4d/reconstruction/queen/LICENSE.md`) — §3.3 limits use
  of the Work and any derivative work to non-commercial research or evaluation.
  §3.1 requires redistribution under the same license with notices intact. §3.2
  requires that derivative works carry the same use limitation. This reaches the
  unified rasterizer, which derives from QUEEN's `gaussian-rasterization-grad`.
- **MIT** (`open4d/reconstruction/3dgstream/LICENSE`, © 2024 Jac Sun) —
  permissive, but 3DGStream's rasterizer derives from inria's, below.
- **Gaussian-Splatting research license** — inria/MPII, non-commercial, applies
  to `diff-gaussian-rasterization` and everything forked from it, which is all
  three rasterizers here.
- **MIT** (`open4d/reconstruction/queen/MiDaS`) — isl-org.
- **The Happy Bunny License or MIT** (`glm/copying.txt`) — g-truc, permissive.
- **SIBR** (`SIBR_viewers/LICENSE.md`) — inria, non-commercial; unbuilt but
  tracked, so its notices still ship with the repository.

Net effect: this module is non-commercial. Open4D's MIT license covers the
`gs_tools/` Python package, `patches/`, `configs/`, and `scripts/` only.

The root [THIRD_PARTY.md](../../../THIRD_PARTY.md) records this module as `BLOCK`
pending a complete upstream-and-patch manifest. The tables above are that
manifest for the pieces named in them; the exact per-file provenance of the
copied subtrees, and every notice that would have to ship with them, is not yet
assembled. Do not treat this file as clearing that entry.

## Weights

| File | Source | Size |
| --- | --- | --- |
| `dpt_beit_large_512.pt` | https://github.com/isl-org/MiDaS/releases/download/v3_1/dpt_beit_large_512.pt | 1.5 GB |

Fetched by `scripts/setup.sh --midas-weights` into
`open4d/reconstruction/queen/MiDaS/weights/`, which is ignored. Not committed, per
[docs/artifacts.md](../../../docs/artifacts.md).

## Bumping a pin

There is no submodule to move any more, so a bump is an explicit re-vendor:

```bash
git clone https://github.com/NVlabs/queen /tmp/queen && git -C /tmp/queen checkout <sha>
# replace open4d/reconstruction/queen with the new tree, keeping this repository's
# .gitignore entries, then re-apply the series from the repository root:
git apply --directory=open4d/reconstruction/queen \
  open4d/reconstruction/gs_tools/patches/queen/*.patch
./open4d/reconstruction/gs_tools/scripts/setup.sh --no-build   # verifies, does not apply
```

Update the revision and patch tables, rerun parity tests, and commit the
vendored tree with its patches applied. `setup.sh` fails if it does not match
the patch series.
