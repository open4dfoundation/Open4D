# Third-party provenance and release ledger

This ledger records known distribution evidence for Open4D. It was initialized
from the `v0.2-dev` audit of commit `96b8c7b` on 2026-08-13 and checked against
`origin/main` at `6d5faf3` on 2026-08-14. It is not legal advice.
Rows for the streamer, the browser study and documentation media, and the
reviewer column, were added on 2026-10-07.

## Release decision: blocked

**Do not publish the full repository, a PyPI distribution, container, dataset,
model, native plugin, or GitHub release.** The tree contains non-commercial and
no-redistribution terms, GPL code, components with no identified license,
copied upstream source without complete revision records, native binaries,
datasets, papers, and model checkpoints with unresolved provenance.

The explicit package list in `pyproject.toml`, `MANIFEST.in`, and the archive
checks under `scripts/` provide technical containment only. They are not
permission to publish. A release requires every `BLOCK` entry to be resolved,
all required notices to be assembled, and a recorded maintainer approval.

## Candidate lightweight boundary

| Package | Basis | Candidate decision |
| --- | --- | --- |
| `open4d`, `open4d.codec`, `open4d.core`, `open4d.io`, `open4d.visualization`, `open4d.reconstruction.rgbd`, `open4d.transport` | Project-authored, root MIT; NumPy base | Candidate after final review; research implementations remain excluded |
| `open4d.torch_ops` | Project-authored source; Torch optional and not redistributed | Candidate after final review |
| `open4d.integrations`, `open4d.integrations.open3d` | Project-authored adapter source; Open3D optional and not redistributed | Candidate after final review |
| `open4d.data`, `open4d.ml` | Project-authored cache/synthetic generators and shared training utilities; NumPy base, Torch optional | Candidate after final review; no captured datasets, checkpoints or research model implementations bundled |

Research source trees now live under top-level `codecs/`, outside the shipped
Python namespace. Relocation preserves licenses, vendored documentation and
all seven pinned submodule revisions; path/import wiring is adapted to this
layout. Explicit runtime roots and legacy source checkouts stay supported.
Neural implementations remain codec backends rather than shared ML utilities.

Research registrations now have separate metadata-only adapter packages under
`plugins/`. Each ships the root MIT license, entry points and a dependency on
the matching core version. Their wheels and source archives are checked by
`scripts/check_plugin_contents.py`; research source, native binaries, media,
data and model weights are forbidden. Installing an adapter does not change
the license of its independently acquired runtime. Building downloadable
artifacts is not publication approval, and all release blocks below remain.

Tests, examples, codecs, reconstruction systems, Gaussian implementations,
datasets, generated artifacts, papers, Unity files, and vendored source are
excluded from both candidate wheel and source distribution.

## Component ledger

`BLOCK` means no release artifact may include the component until the required
evidence is resolved. `EXCLUDED` means it is outside the candidate Python
distribution and still needs separate review before any redistribution.
The reviewer is the maintainer who collects the missing evidence and records
the decision that closes the row. `Unassigned` rows have no reviewer yet.

After review, record `APPROVED` for a component with verified distribution
rights, or `EXCLUDED` for a component kept outside the artifacts. Publication
also requires changing the release decision to `approved`, removing every
`BLOCK` entry, and adding `Maintainer approval: NAME (YYYY-MM-DD)` with the
maintainer's actual name and approval date. CI validates either release state;
the publishing workflow requires the approved state. The current decision
remains blocked.

| Path / component | Immutable source evidence | License evidence | Decision and required action | Reviewer |
| --- | --- | --- | --- | --- |
| `plugins` | Project-authored entry point manifests tracked with the October core API revision; archive inventory checked by `scripts/build_plugins.py` and `scripts/check_plugin_contents.py` | Root MIT text shipped in each adapter; no upstream research implementation is copied into the adapter | `EXCLUDED` from the core wheel/sdist; separate metadata-only candidate adapter artifacts still require recorded publication approval; backend, data and weight rights remain independent | Unassigned |
| `codecs/draco` | Wrapper plus `google/draco` submodule `47238930f698250f474163e1a29d77858aa5c158` | Upstream Apache-2.0 after initialization; wrapper currently relies on root license. `util.py` is a copy of `n4mc/util.py` and carries an NVIDIA 2023 header prohibiting distribution without a license; the packaged `open4d.codec._draco` uses only the Apache-2.0 `DracoPy` binding and none of this tree | `EXCLUDED`; retain upstream notices and record any patches before source/binary distribution; remove or replace `util.py` | Unassigned |
| `codecs/klt` | Local research implementation extracted from N4MC (`klt.py`); `fmc.py` is an edited copy and `tables.py` a byte-identical copy of `rsy6318/NeCGS` | No component license found. `tables.py` carries an NVIDIA 2023 header prohibiting distribution without a license; NeCGS publishes no license | `BLOCK`; identify authorship of `klt.py`, and replace `fmc.py`/`tables.py` (for example with scikit-image marching cubes or the Apache-2.0 FlexiCubes tables) before any distribution | Unassigned |
| `codecs/n4mc` | Local/copied implementation forked from `rsy6318/NeCGS`; historical generated outputs and three unused machine-specific scratch scripts moved out of the checkout. The adapter loads only `models/`, `losses/` and `evaluation/metrics.py`, which import none of the files below | No component license; training/checkpoint lineage absent. `util.py`, `render.py`, `loss.py` and `tables.py` carry NVIDIA 2023 headers (adapted from `NVlabs/nvdiffrec`, non-commercial, redistribution only with the full license text, which is absent); NeCGS publishes no license | `BLOCK`; identify authorship of the files the adapter loads, keep the NVIDIA- and NeCGS-derived files out of every artifact, and add the NVIDIA license text or remove those files from the public tree | Unassigned |
| `codecs/qndf` | `karthikey97/Quantized-Neural-Displacement-Fields` at `456c7394` (an unofficial reimplementation of the EG 2025 paper), modified locally, plus `libigl` submodule `e60423e28c86b6aa2a3f6eb0112e8fd881f96777`; `ssp_remesh` derives from `HTDerekLiu/surface_multigrid_code` | Top-level and `ssp_remesh` contain GPL-3.0 text with no copyright line; libigl ships MPL-2.0 and GPL files. The adapter loads `compress.py` and `build_dataset_open3d.py` but not `ssp_remesh` | `BLOCK`; determine a GPL-compatible source strategy (a separate GPL-3.0 plugin) and preserve source/notice obligations; confirm the copyright holder with the upstream author | Unassigned |
| `codecs/qndf_int8` | Derived experiment; exact base revision not recorded. `mesh_errors.py` is identical to the QNDF file. The `qndf-int8` adapter loads `qndf.compress`, not this directory | No component license found; GPL-3.0 applies by derivation from QNDF | `BLOCK`; record derivation, copyright, license, weights, and data lineage, or remove the unused directory | Unassigned |
| `codecs/tvmc` | Research pipeline, copied ARAP/editor trees, Draco `47238930f698250f474163e1a29d77858aa5c158` | `LICENSE.md` permits non-commercial internal research and prohibits redistribution | `BLOCK`; obtain written redistribution authority or keep outside every distributed artifact | Unassigned |
| `codecs/tsmc` | Research pipeline imported from `SINRG-Lab/TSMC` at `5924636c`, copied ARAP/editor trees (MIT), Draco `47238930f698250f474163e1a29d77858aa5c158`, SAM3 `5dd401d1c5c1d5c3eedff06d41b77af824517619`. Local changes: retain the matrix shape for a single PCA component; entropy coding supports constant displacement matrices and rejects values outside the int32/ANS range. SAM3 is used only by notebooks and `util.py` helpers, not by the encode/decode path | No top-level component license here or upstream; copied subtrees have separate terms; SAM3 is under the SAM License | `BLOCK`; obtain a license from the rights holder, confirm co-author rights, inventory source, patches, media, and notices, and never bundle SAM3 | Unassigned |
| `codecs/vdmc` | MPEG reference submodule `ecffe4212e5e956761c4fa14a17c453ae916b0b1` (tag `v14.0`); the CMake build fetches HM, VTM, Draco, UVAtlas, DirectX headers, mmetric and tinyply | `COPYING` is BSD-3-Clause, Copyright (c) 2022 ISO/IEC, and states that patent and other third-party rights are not granted; fetched dependency licenses not inventoried | `BLOCK`; keep outside every artifact: users build it separately and Open4D finds it through `OPEN4D_VDMC_ROOT` or the executable variables; never publish binaries; inventory fetched dependency licenses and review patent exposure before offering any build helper | Unassigned |
| `codecs/faster_vdmc` | Fork `cicm4/mpeg-vdmc-tm` at `93cdd5e1367b0f9f81c251ef89255bcb2f0d2d3f`, derived from `MPEGGroup/mpeg-vdmc-tm` at `ecffe4212e5e956761c4fa14a17c453ae916b0b1` with eight encoder/decoder performance commits. The fork adds an opt-in `USE_X265_VIDEO_CODEC` build that fetches x265 (GPL-2.0), off by default | Fork `COPYING` contains the ISO/IEC BSD 3-Clause terms and expressly excludes patent and other third-party rights; build-fetched dependencies need separate review | `BLOCK`; preserve the fork history and modification record, record the license of the eight commits, inventory dependency licenses/notices and patent implications, never distribute x265-enabled builds without GPL review, and review source/binary distribution before release | Unassigned |
| `open4d/reconstruction/rgbd` | Project reconstruction and protocol experiments; `_receiver.py` packages the project-authored rectification subset of `python/live_two_camera_fusion.py`. The native tree was committed as the MeshReduce RGB-D module with no recorded origin; `include/sensor/cv_convert_util.hpp` is MIT, Copyright (c) 2019 Tsukasa Sugiura | Root MIT is current repository evidence; the packaged Python modules import only NumPy, and lazily Open3D (MIT), OpenCV (Apache-2.0) and zstandard (BSD); camera SDK/native dependencies external | `EXCLUDED` for native/capture code; project-authored top-level Python API only is in the candidate package; record the origin of the MeshReduce code; camera/native dependency review remains open | Unassigned |
| `codecs/3dgstream` | `SJoJoK/3DGStream` at `747ddfef646edf3ea628f2bd13b7bedce7c5fe47`, with local patches listed in [`gs_tools/THIRD_PARTY.md`](codecs/gs_tools/THIRD_PARTY.md). `ntc/flame_steak_ntc_params_F_4.pth` is the upstream checkpoint, trained on N3DV | Top-level MIT, but 18 of 29 Python files carry the Inria Gaussian-Splatting non-commercial header; separate SIBR/rasterizer terms; N3DV is CC BY-NC 4.0; imports plyfile (GPL-3.0) and tiny-cuda-nn (BSD-3) | `BLOCK`; keep outside every artifact; users fetch upstream at the pinned revision and keep the checkpoint in their own copy | Unassigned |
| `codecs/queen` | `NVlabs/queen` at `4d761ae6a2893f220cd049fac6c97bea13de9b98`, including MiDaS, SIBR, and rasterizers, with local patches listed in [`gs_tools/THIRD_PARTY.md`](codecs/gs_tools/THIRD_PARTY.md) | Top-level NVIDIA non-commercial license (redistribution only under the same license with its full text) plus the Inria Gaussian-Splatting license; MiDaS MIT; imports torchac and plyfile (both GPL-3.0) in the same process; pretrained models CC BY-NC-SA 4.0 | `BLOCK`; keep outside every artifact; users fetch upstream at the pinned revision; never publish prebuilt environments or images combining it with GPL code; record MiDaS weight and data rights | Unassigned |
| `codecs/gs_tools` | Consolidated Gaussian tooling tree containing SIBR viewers, GLM, and three rasterizer imports; one immutable upstream/patch manifest is absent. Applying `patches/queen` and `patches/3dgstream` to the pinned upstreams leaves residual differences, and the rasterizer edits are in no patch | Component-local SIBR, GLM, and rasterizer license files exist with differing terms; the rasterizers are Inria non-commercial, two also carry NVIDIA headers; `simple-knn` has no license file although its headers refer to one; the Python package declares `LicenseRef-NVIDIA-Non-Commercial AND MIT` | `BLOCK`; regenerate complete patch series, inventory exact upstream revisions, preserve every notice, remove the unused SIBR viewers, and determine compatible source/binary distribution terms | Unassigned |
| `codecs/vega` | Copied from `4DVideoStreaming` `baselines/Vega`, working tree above commit `6c2569de85ebe4592a8294c8cb0268efd3659212`, so not a clean revision: it carries three uncommitted modifications (`vega/encoder.py`, `vega/gov.py`, `orbitvega/prepare.py`) and two never-committed files (`orbitvega/scene_export.py`, `orbitvega/scene_render.py`). Local re-implementation of Vega (Kim et al., ACM MobiCom '25, `10.1145/3680207.3765267`); no upstream code release exists. Open4D modifications: import roots rerooted off `baselines.Vega`, `pytest.ini` added | No component license file; `citation.txt` records the paper only, and `vega_engine_pyproject.toml` names it a research prototype | `BLOCK`; identify the implementation's copyright holder and distribution terms, and pin an immutable source revision | Unassigned |
| `codecs/rerf` | `rerf_stream/` is project-authored. `upstream/` is a clone of `https://github.com/aoliao12138/ReRF` (Wang et al., CVPR 2023) at `510e6073827ff15f58efda17a8729f2c47e13401` (2023-07-24), byte-identical to that tree apart from four non-code deletions enumerated in the module README (`.git/`, `ac_dc/` CMake intermediates, a README image); upstream carries no Open4D patches, all local adaptation living in `rerf_stream/env.py`. Supersedes the removed `open4d/reconstruction/nevo`, which vendored the same ReRF code at an unrecorded depth-1 revision and remains in history at `3d33655`. `upstream/ac_dc/` ships five prebuilt `.so` modules and an `ncvv_code` binary with no sources, only CMake residue, over a pybind11 checkout whose source is likewise absent | `upstream/LICENSE` is GPL-3.0 prefixed with a `THIS CODE CAN ONLY BE USED FOR RESEARCH PURPOSES` rider that contradicts the GPL grant (the same text as DVGO's license); the code base derives from DVGO, whose kernels are `upstream/lib/cuda/`, and borrows from NHR (CC BY-NC-SA 4.0). The `ac_dc` binaries appear to be built from `rockcarry/ffjpeg` (GPL-3.0). No component license covers `rerf_stream/` beyond root MIT, but `rerf_stream/bitstream.py` is transcribed from upstream `rerf_render.py` | `BLOCK`; resolve the GPL-3.0 against research-only conflict and the DVGO lineage, obtain reproducible sources and a dependency bill for the `ac_dc` binaries or exclude them from every artifact, and record terms for the project-authored `rerf_stream/` layer | Unassigned |
| `open4d/streamer` | Project-authored `open4d-streamer` distribution; unmodified browser decoder, LICENSE and AUTHORS byte-matched to `google/draco` at `47238930f698250f474163e1a29d77858aa5c158`; pinned SHA-256 values in `scripts/check_streamer_package.py` | Project MIT license and exact Apache-2.0 license/AUTHORS shipped beside the decoder and in wheel license metadata. Upstream has no root NOTICE; the added NOTICE explicitly records Open4D-authored attribution | `EXCLUDED` from the core wheel/sdist; candidate separate archives verified to exclude study, benchmarks and tests and retain runtime files/notices. Publication still requires actual maintainer approval | Unassigned |
| `open4d/streamer/study` | Copied from `frozzzen3/4DVideoStreaming` at `c3df4f9b2ef4ce04af8a0ea08dce94441c9bafde` (see `PROVENANCE`), plus Open4D's study additions; vendored Draco decoder and npm lockfiles | No license recorded for the origin repository; npm dependency licenses not inventoried | `BLOCK`; record the origin repository's license and authorship, inventory npm dependency licenses, and keep it outside every distributed artifact until then | Unassigned |
| `docs/assets` | Project figures and GIFs; `basketball_comparison_demo.gif` is rendered from the TVMC basketball sequence | Root MIT for project figures; the rendered basketball capture inherits the TVMC data terms | `BLOCK`; excluded from the MkDocs wiki build; record the source of each figure and replace or license renders of third-party captures before any media bundle | Unassigned |
| `integrations/unity` | Project glue, copied Eigen, prebuilt plugins | No integration-wide manifest; Eigen has multiple license files; binaries/data unresolved | `BLOCK`; inventory producers, licenses, build revisions, and symbols | Unassigned |
| `integrations/unity/TVMCUnity/Unity Files/Plugins` | Prebuilt Android `.so` and macOS `.dylib` | Reproducible build and dependency bill absent | `BLOCK`; exclude until reproduced from reviewed source with notices | Unassigned |
| `codecs/n4mc/outputs` | Historical checkpoints, configs, metrics, and reconstructions | Training inputs, authorship, and checkpoint rights absent | `BLOCK`; moved outside the checkout to `../open4d-local-artifacts` without redistribution. SHA-256 inventory: `cleanup-manifest.json`, digest `5b85cf3bfbf02d352542362fa3265359c0b937e68f21b89436de039a9dfca627`. Data/model rights remain unresolved | Unassigned |
| Tracked media | TVM editor documentation media in `codecs/{tvmc,tsmc}/tvm-editing/Documentation` | Copyright and redistribution basis not recorded centrally | `BLOCK`; record author permission or link externally instead of redistributing | Unassigned |

## Local runtime repairs (2026-09-21)

The following changes preserve the existing component licenses and exclusion
boundaries; they do not resolve any release block above:

- `codecs/queen`: preserve valid tensor index types during
  compressed residual I/O, support empty residuals and disabled gates, and
  remove an unused mapping access from ungated decoding. Native round-trip
  regressions are under `queen/tests/test_compressed_residuals.py`.
- `codecs/3dgstream`: retain temporal model loading by default,
  omit evaluation results when no test cameras exist, and adapt a warmed
  cache's disjoint world bounds to the input scene while retaining its weights.
  Regression tests are under `3dgstream/tests/test_training_regressions.py`.
- `codecs/gs_tools/rasterizers/diff-gaussian-rasterization`:
  restore the 0.2-unit near guard used by the other bundled rasterizers; the
  prior 4-unit guard incorrectly discarded nearby visible geometry. This
  source change requires rebuilding the native extension.
- `codecs/vega`: use the corrected near guard for ORBIT
  refinement, test nearby-splat rendering/gradients, and allow the ORBIT test
  corpus path to be configured without failing collection on denied paths.
- `codecs/tsmc/tsmc/get_reference_center.py`: retain the fifth reference
  frame on longer clips, use the last available frame on short clips, and
  remove unused alignment work that also accessed an unavailable sixth frame.

Additional local patches (2026-09-23): N4MC restores SSIM loss; ReRF validates
inputs before native loading and makes test data configurable; the browser
study (then `open4d/webclients`, now `open4d/streamer/study`) uses a local tile
record and defaults to loopback. Licenses and release boundaries
are unchanged.

## Required record for new material

Any copied source, submodule, binary, model, dataset, paper, image, archive, or
generated artifact must add:

- canonical upstream URL and immutable revision or SHA-256;
- copyright holder and local modifications;
- exact license path and required notices;
- source/data/model lineage;
- inclusion or exclusion from each release artifact; and
- the decision and evidence that closes any former `BLOCK` state.

Run `python scripts/check_provenance.py` after editing this ledger. Automation
checks coverage and containment; it cannot determine legal compatibility.
