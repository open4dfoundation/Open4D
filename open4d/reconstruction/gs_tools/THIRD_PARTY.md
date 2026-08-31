# Third-party components

QUEEN and 3DGStream are someone else's work. Both were pinned submodules under
`upstream/` until they were vendored as plain tracked files: the two methods now
sit beside this module under `open4d/reconstruction/`, and the pieces they share
-- three rasterizers, one `simple-knn`, one `glm`, one `SIBR_viewers` -- live
inside it.

Vendoring moved the boundary. The trees are committed in the state Open4D builds
against, patches included, so nothing is fetched and nothing is modified at setup
time. `patches/` is no longer a build step; it is the record of what Open4D
changed, and `scripts/setup.sh` verifies that each patch is still present rather
than applying it. A vendored tree that stops matching its series is a hard error,
because the diff against upstream is the only remaining evidence of what was
changed and why.

That verification runs from the repository root with `git apply --directory`.
Running `git apply` from inside one of these trees looks equivalent and is not:
now that they are ordinary directories of this repository rather than submodules,
patch paths resolve against the repository root, everything outside the current
directory is silently ignored, and the check reports success for both polarities
against no file at all. That is how `queen/0001-lazy-midas-import.patch` stayed
unapplied without anyone noticing.

## Pins

Repository-relative paths. The commits are the upstream revisions the trees were
vendored from; there is no longer a submodule to read them back out of, so this
table is the only record.

| Path | Upstream | Vendored from | Date | License |
| --- | --- | --- | --- | --- |
| `open4d/reconstruction/queen` | https://github.com/NVlabs/queen | `4d761ae6a2893f220cd049fac6c97bea13de9b98` | 2026-02-11 | NVIDIA License (non-commercial) |
| `open4d/reconstruction/3dgstream` | https://github.com/SJoJoK/3DGStream | `747ddfef646edf3ea628f2bd13b7bedce7c5fe47` | 2024-11-18 | MIT |

## Shared components inside this module

One copy each, because both methods wanted the same sources and two copies of a
CUDA extension cannot coexist in one environment. Paths are relative to
`gs_tools/`.

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

Nothing here is fetched from `gitlab.inria.fr`, so setup does not depend on it
being reachable.

## Our patches

Each patch is verified against the tree named here, from the repository root,
with `--directory` set to that path.

| Patch | Target | Effect |
| --- | --- | --- |
| `queen/0001-lazy-midas-import.patch` | `open4d/reconstruction/queen` | moves the module-scope `MiDaS` imports in `train.py` and `scene/utils.py` inside the branches that use them, so training does not require `timm==0.6.13`, which Open4D keeps in a separate environment |
| `3dgstream/0001-rename-rasterizer-import.patch` | `open4d/reconstruction/3dgstream` | one import line in `gaussian_renderer/__init__.py`. QUEEN's tree installs inria's fork under the name `diff_gaussian_rasterization`, and one environment cannot hold two extensions with that name, so 3DGStream imports the renamed package instead. Phase 2 of `docs/plan.md` replaces the import with the unified rasterizer |
| `3dgstream-rasterizer/0001-rename-package.patch` | `gs_tools/rasterizers/gstream-rasterization` | renames the installed package to `gstream_rasterization`, the other half of the clash above |
| `3dgstream-rasterizer/0002-cstdint-include.patch` | `gs_tools/rasterizers/gstream-rasterization` | adds `#include <cstdint>` to `cuda_rasterizer/rasterizer_impl.h`. The header uses `std::uintptr_t` and the fixed-width integer types, and on GCC 13 with CUDA 12.6 nothing above it pulls them in transitively any more. NVIDIA added the same include to their fork, which is why QUEEN's two rasterizers build here and this one did not |

None of the four changes what any kernel computes.

The first three patches carry an explanatory comment block in the source they
touch. `0001-rename-rasterizer-import` and `0002-cstdint-include` do not: the
vendored trees were committed with the code change but without those comments, so
the patches were regenerated to match what the trees actually contain, and the
reasoning moved into the table above.

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

Then update the tables above and re-run the parity test before trusting any
result. A patch that no longer applies is the intended signal that upstream
changed something the unified rasterizer depends on. Commit the re-vendored tree
in its patched state: `setup.sh` verifies presence and will fail on a tree that
carries the code change without the patch, as two of these did.
