# Third-party components

Everything under `upstream/` is someone else's work, fetched at a pinned commit.
Our changes live in `patches/` as diffs, applied to the working trees by
`scripts/setup.sh`, so that what Open4D actually contributes stays legible and a
pin bump is reviewable. A patched tree is therefore dirty by design; what must
hold is that its diff equals the patch series.

## Pins

| Path | Upstream | Commit | Date | License |
| --- | --- | --- | --- | --- |
| `upstream/queen` | https://github.com/NVlabs/queen | `4d761ae6a2893f220cd049fac6c97bea13de9b98` | 2026-02-11 | NVIDIA License (non-commercial) |
| `upstream/3dgstream` | https://github.com/SJoJoK/3DGStream | `747ddfef646edf3ea628f2bd13b7bedce7c5fe47` | 2024-11-18 | MIT |

The two checkouts handle their own dependencies differently, which is worth
knowing before editing `setup.sh`.

**QUEEN has no submodules at this commit.** Its `.gitmodules` is an inert
leftover: `git -C upstream/queen submodule status` is empty, and MiDaS,
SIBR_viewers, simple-knn, both rasterizers, and two copies of `third_party/glm`
are all committed as plain files. Nothing needs initializing, and 53 MB arrives
with the clone whether or not it gets built.

| Path (vendored in QUEEN) | Original upstream | Built | Why |
| --- | --- | --- | --- |
| `submodules/gaussian-rasterization-grad` | no separate upstream; NVIDIA's extension of inria's | yes | base of the unified rasterizer |
| `submodules/simple-knn` | https://gitlab.inria.fr/bkerbl/simple-knn | yes | KNN for point-cloud init; built once and used by both methods |
| `submodules/diff-gaussian-rasterization` | https://github.com/graphdeco-inria/diff-gaussian-rasterization | yes, for now | QUEEN's plain path; parity reference |
| `MiDaS` | https://github.com/isl-org/MiDaS | n/a | depth priors, run from the separate MiDaS environment |
| `SIBR_viewers` | https://gitlab.inria.fr/sibr/sibr_core | **no** | C++ viewer, its own toolchain; Open4D uses `examples/visualization/` |

**3DGStream uses real submodules**, three of them, and `setup.sh` initializes
exactly one:

| Nested path | Upstream | Initialized | Why |
| --- | --- | --- | --- |
| `submodules/diff-gaussian-rasterization` | https://github.com/SJoJoK/3DGStreamRasterizer | yes, recursively for `third_party/glm` | its depth-gradient fork; parity reference |
| `submodules/simple-knn` | https://gitlab.inria.fr/bkerbl/simple-knn | **no** | same inria source QUEEN vendors; built once from there |
| `SIBR_viewers` | https://gitlab.inria.fr/sibr/sibr_core | **no** | as above |

Skipping both gitlab paths also means setup does not depend on
`gitlab.inria.fr` being reachable.

## Our patches

| Patch | Target | Effect |
| --- | --- | --- |
| `queen/0001-lazy-midas-import.patch` | `upstream/queen` | moves the module-scope `MiDaS` imports in `train.py` and `scene/utils.py` inside the branches that use them, so training does not require `timm==0.6.13` |
| `3dgstream/0001-rename-rasterizer-import.patch` | `upstream/3dgstream` | one import line in `gaussian_renderer/__init__.py` |
| `3dgstream-rasterizer/0001-rename-package.patch` | `upstream/3dgstream/submodules/diff-gaussian-rasterization` | renames the installed package to `gstream_rasterization`, because QUEEN's plain rasterizer already occupies the name `diff_gaussian_rasterization` and one environment cannot hold both |

None of the three changes what any kernel computes.

## Licenses

- **NVIDIA License** (`upstream/queen/LICENSE.md`) — §3.3 limits use of the Work
  and any derivative work to non-commercial research or evaluation. §3.1 requires
  redistribution under the same license with notices intact. §3.2 requires that
  derivative works carry the same use limitation. This reaches the unified
  rasterizer, which derives from QUEEN's `gaussian-rasterization-grad`.
- **MIT** (`upstream/3dgstream/LICENSE`, © 2024 Jac Sun) — permissive, but
  3DGStream's rasterizer derives from inria's, below.
- **Gaussian-Splatting research license** — inria/MPII, non-commercial, applies
  to `diff-gaussian-rasterization` and everything forked from it, which is all
  three rasterizers here.
- **MIT** (`upstream/queen/MiDaS`) — isl-org.

Net effect: this module is non-commercial. Open4D's MIT license covers the
`gs_tools/` package, `patches/`, `configs/`, and `scripts/` only.

## Weights

| File | Source | Size |
| --- | --- | --- |
| `dpt_beit_large_512.pt` | https://github.com/isl-org/MiDaS/releases/download/v3_1/dpt_beit_large_512.pt | 1.5 GB |

Fetched by `scripts/setup.sh --midas-weights` into `upstream/queen/MiDaS/weights/`,
which is ignored. Not committed, per [docs/artifacts.md](../../../docs/artifacts.md).

## Bumping a pin

```bash
git -C upstream/queen checkout -- .      # drop the applied patch series
git -C upstream/queen fetch origin && git -C upstream/queen checkout <sha>
./scripts/setup.sh                       # re-applies patches; fails loudly on conflict
```

Then update the table above and re-run the parity test before trusting any
result. A patch that no longer applies is the intended signal that upstream
changed something the unified rasterizer depends on.
