# Third-party provenance and release ledger

This ledger records known distribution evidence for Open4D. It was initialized
from the `v0.2-dev` audit of commit `96b8c7b` on 2026-08-13 and checked against
`origin/main` at `6d5faf3` on 2026-08-14. It is not legal advice.

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
| `open4d`, `open4d.codec`, `open4d.core`, `open4d.io`, `open4d.visualization`, `open4d.streaming` | Project-authored, root MIT; NumPy base | Candidate after final review; research implementations remain excluded |
| `open4d.torch_ops` | Project-authored source; Torch optional and not redistributed | Candidate after final review |
| `integrations`, `integrations.open3d` | Project-authored adapter source; Open3D optional and not redistributed | Candidate after final review |

Tests, examples, codecs, reconstruction systems, Gaussian implementations,
datasets, generated artifacts, papers, Unity files, and vendored source are
excluded from both candidate wheel and source distribution.

## Component ledger

`BLOCK` means no release artifact may include the component until the required
evidence is resolved. `EXCLUDED` means it is outside the candidate Python
distribution and still needs separate review before any redistribution.

| Path / component | Immutable source evidence | License evidence | Decision and required action |
| --- | --- | --- | --- |
| `open4d/codecs/draco` | Wrapper plus `google/draco` submodule `47238930f698250f474163e1a29d77858aa5c158` | Upstream Apache-2.0 after initialization; wrapper currently relies on root license | `EXCLUDED`; retain upstream notices and record any patches before source/binary distribution |
| `open4d/codecs/klt` | Local research implementation; upstream revision not recorded | No component license found | `BLOCK`; identify authorship, upstream revision, and distribution terms |
| `open4d/codecs/n4mc` | Local/copied implementation; historical generated outputs and three unused machine-specific scratch scripts moved out of the checkout | No component license; training/checkpoint lineage absent | `BLOCK`; identify source rights and externalize outputs only after recording hashes and data/model rights |
| `open4d/codecs/qndf` | Copied research code plus `libigl` submodule `e60423e28c86b6aa2a3f6eb0112e8fd881f96777` | Top-level and `ssp_remesh` contain GPL-3.0 text | `BLOCK`; determine a GPL-compatible source strategy and preserve source/notice obligations |
| `open4d/codecs/qndf_int8` | Derived experiment; exact base revision not recorded | No component license found | `BLOCK`; record derivation, copyright, license, weights, and data lineage |
| `open4d/codecs/tvmc` | Research pipeline, copied ARAP/editor trees, Draco `47238930f698250f474163e1a29d77858aa5c158` | `LICENSE.md` permits non-commercial internal research and prohibits redistribution | `BLOCK`; obtain written redistribution authority or keep outside every distributed artifact |
| `open4d/codecs/tsmc` | Research pipeline, copied trees, Draco `47238930f698250f474163e1a29d77858aa5c158`, SAM3 `5dd401d1c5c1d5c3eedff06d41b77af824517619`, tracked datasets/paper. Local change: entropy coding supports constant displacement matrices and rejects values outside the int32/ANS range | No top-level component license; copied subtrees have separate terms | `BLOCK`; inventory source, patches, paper/media, data rights, SAM3 terms, and notices |
| `open4d/codecs/vdmc` | MPEG reference submodule `ecffe4212e5e956761c4fa14a17c453ae916b0b1` | Not audited in the uninitialized tree | `BLOCK`; initialize, record license/notices, and review intended source/binary distribution |
| `open4d/codecs/faster_vdmc` | Fork `cicm4/mpeg-vdmc-tm` at `93cdd5e1367b0f9f81c251ef89255bcb2f0d2d3f`, derived from `MPEGGroup/mpeg-vdmc-tm` at `ecffe4212e5e956761c4fa14a17c453ae916b0b1` with eight encoder/decoder performance commits | Fork `COPYING` contains the ISO/IEC BSD 3-Clause terms and expressly excludes patent and other third-party rights; build-fetched dependencies need separate review | `BLOCK`; preserve the fork history and modification record, inventory dependency licenses/notices and patent implications, and review source/binary distribution before release |
| `open4d/streaming` | Project reconstruction and protocol experiments | Root MIT is current repository evidence; camera SDK/native dependencies external | `EXCLUDED` for native/capture code; project-authored top-level Python API only is in the candidate package; camera/native dependency review remains open |
| `open4d/reconstruction/3dgstream` | Imported research tree; consolidated upstream revision/patch list absent | Top-level MIT plus separate SIBR/rasterizer/Gaussian terms | `BLOCK`; record all upstream revisions, patches, subtree licenses, data/model rights, and allowed artifact boundary |
| `open4d/reconstruction/queen` | Imported research tree including MiDaS, SIBR, and rasterizers | Top-level NVIDIA non-commercial license plus multiple subtree licenses | `BLOCK`; record revisions, patches, all notices, model/data rights, and redistribution limits |
| `open4d/reconstruction/gs_tools` | Consolidated Gaussian tooling tree containing SIBR viewers, GLM, and three rasterizer imports; one immutable upstream/patch manifest is absent | Component-local SIBR, GLM, and rasterizer license files exist with differing terms | `BLOCK`; inventory exact upstream revisions and patches, preserve every notice, and determine compatible source/binary distribution terms |
| `open4d/reconstruction/vega` | Copied from `4DVideoStreaming` `baselines/Vega`, working tree above commit `6c2569de85ebe4592a8294c8cb0268efd3659212`, so not a clean revision: it carries three uncommitted modifications (`vega/encoder.py`, `vega/gov.py`, `orbitvega/prepare.py`) and two never-committed files (`orbitvega/scene_export.py`, `orbitvega/scene_render.py`). Local re-implementation of Vega (Kim et al., ACM MobiCom '25, `10.1145/3680207.3765267`); no upstream code release exists. Open4D modifications: import roots rerooted off `baselines.Vega`, `pytest.ini` added | No component license file; `citation.txt` records the paper only, and `vega_engine_pyproject.toml` names it a research prototype | `BLOCK`; identify the implementation's copyright holder and distribution terms, and pin an immutable source revision |
| `open4d/reconstruction/nevo` | Copied from `4DVideoStreaming` `baselines/NeVo` at its introducing commit `6c2569de85ebe4592a8294c8cb0268efd3659212` plus one uncommitted modification (`orbitnevo/train.py`). NeVo (Wu et al., ACM MobiCom '25, `10.1145/3680207.3723473`) has no released code, so the simulator is local; it vendors `https://github.com/aoliao12138/ReRF` (CVPR 2023) under `rerf/`, **cloned at depth 1 so no upstream revision is recorded**, byte-identical apart from four non-code deletions listed in `rerf/PATCHES.md`. Open4D modifications: import roots rerooted off `baselines.NeVo`, `MODULE_ROOT` repointed, `pytest.ini` and `orbitnevo/objects.py` added | `rerf/LICENSE` is GPL-3.0 carrying a research-purposes-only rider, and its code base derives from DVGO; no top-level component license | `BLOCK`; recover and record the exact ReRF revision, resolve the GPL-3.0 plus research-only boundary and the DVGO lineage, and identify terms for the local simulator |
| `integrations/unity` | Project glue, copied Eigen, prebuilt plugins, encoded archive | No integration-wide manifest; Eigen has multiple license files; binaries/data unresolved | `BLOCK`; inventory producers, licenses, build revisions, symbols, and fixture rights |
| `integrations/unity/TVMCUnity/Unity Files/Plugins` | Prebuilt Android `.so` and macOS `.dylib` | Reproducible build and dependency bill absent | `BLOCK`; exclude until reproduced from reviewed source with notices |
| `integrations/unity/TVMCUnity/EncodedExample/DanceSequence.zip` | Historical encoded dataset archive | Source dataset license/consent absent | `BLOCK`; identify redistribution permission or replace with a licensed synthetic fixture |
| `open4d/codecs/n4mc/outputs` | Historical checkpoints, configs, metrics, and reconstructions | Training inputs, authorship, and checkpoint rights absent | `BLOCK`; moved outside the checkout to `../open4d-local-artifacts` without redistribution. SHA-256 inventory: `cleanup-manifest.json`, digest `5b85cf3bfbf02d352542362fa3265359c0b937e68f21b89436de039a9dfca627`. Data/model rights remain unresolved |
| Tracked papers and large media | TVMC/TSMC papers and demonstration media are committed | Publication copyright and redistribution basis not recorded centrally | `BLOCK`; record publisher/author permission or link externally instead of redistributing |

## Local runtime repairs (2026-09-21)

The following changes preserve the existing component licenses and exclusion
boundaries; they do not resolve any release block above:

- `open4d/reconstruction/queen`: preserve valid tensor index types during
  compressed residual I/O, support empty residuals and disabled gates, and
  remove an unused mapping access from ungated decoding. Native round-trip
  regressions are under `queen/tests/test_compressed_residuals.py`.
- `open4d/reconstruction/3dgstream`: retain temporal model loading by default,
  omit evaluation results when no test cameras exist, and adapt a warmed
  cache's disjoint world bounds to the input scene while retaining its weights.
  Regression tests are under `3dgstream/tests/test_training_regressions.py`.
- `open4d/reconstruction/gs_tools/rasterizers/diff-gaussian-rasterization`:
  restore the 0.2-unit near guard used by the other bundled rasterizers; the
  prior 4-unit guard incorrectly discarded nearby visible geometry. This
  source change requires rebuilding the native extension.
- `open4d/reconstruction/vega`: use the corrected near guard for ORBIT
  refinement, test nearby-splat rendering/gradients, and allow the ORBIT test
  corpus path to be configured without failing collection on denied paths.
- `open4d/codecs/tsmc/tsmc/get_reference_center.py`: retain the fifth reference
  frame on longer clips, use the last available frame on short clips, and
  remove unused alignment work that also accessed an unavailable sixth frame.

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
