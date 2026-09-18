# streaming

Browser clients for volumetric adaptive streaming, and the platform-free
streaming logic they share with the desktop client. Copied from the
`4DVideoStreaming` research repo; a sibling of [`../reconstruction`](../reconstruction),
which vendors the *reconstruction* methods (`vega`, `rerf`, `queen`, …) that
some of these clients play.

## What is here

| Path | Role |
|---|---|
| `system/ClientCore/` | Platform-free streaming logic: segment loop, MCKP ABR, bandwidth estimator, metrics, behind a `ClientPlatform` contract |
| `system/WebClient/` | Five browser pages — our adaptive mesh system, ViVo/NAVA point clouds, Vega splats, NeVo — plus a WebSocket↔TCP bridge |
| `system/Server/` | Express server: publishes the per-segment ladder, serves media, logs viewpoints and selections |
| `tile_ladder.py` | Catalogue-backed ladder letting ViVo and NAVA serve prepared tiles without the RGB-D source |
| `tests/` | 294 tests, including decoders cross-checked against the authoritative Python implementations |
| `scripts/` | Demo launcher, baseline supervisor, trace-based bandwidth shaping |

The `system/` layout is preserved deliberately: every relative `require()`
(`tests/` → `../system/ClientCore/…`, `WebClient/src` → `../../ClientCore/…`)
keeps working, so nothing needed import rewriting.

Read [`system/WebClient/README.md`](system/WebClient/README.md) for how the
pages work and the pitfalls that cost real debugging time, and
[`system/ClientCore/README.md`](system/ClientCore/README.md) for the platform
contract.

## What this does NOT include

This is the **web demo scope**. Three things it depends on live in the research
repo and were deliberately not copied, so the demo is **not runnable from here
as-is**:

1. **`vstream/`** — the Python package that solves the bitrate ladder. The
   server spawns `python -m vstream.ladder.ladder_service`, so without it no
   manifest is published and the mesh client has nothing to select from.
2. **`baselines/`** — the ViVo/NAVA/DeltaStream servers. `tile_ladder.py` is
   here for reference but imports `baselines.ViVo.orbitvivo.ladder`, and the
   point-cloud pages need one of those servers running behind the bridge.
3. **The corpora** — encoded media, the prepared ViVo tiles, the Vega export.
   All gitignored; they are hundreds of GB.

`scripts/run_web_demo.sh` and `scripts/shape_web_demo.sh` came across for
reference but expect the full repo (the latter also needs
`system/Client/traces/*.csv`). Point them at a checkout of the research repo,
or treat this directory as the client-side source of truth that gets vendored
back.

## Tests

```bash
cd system/WebClient && npm install && node build.js   # bundles; some tests load dist/
cd ../.. && node --test tests/*.js
```

292 of 294 pass standalone. The two that do not are cross-repo **by design** —
they exist to prove the JavaScript decoders match the Python ones byte for
byte, which is exactly the check you lose if you let them drift:

```bash
PYTHONPATH=/path/to/4DVideoStreaming \
VS4D_TEST_PYTHON=/path/to/env/bin/python \
  node --test tests/*.js
```

That fixes the V4DS protocol round-trip. The last one,
`test_vgs_format.js`'s "golden fixture and its source asset are present",
wants the real 1.1 MB `results/vega-web/dancer/frame_0000.vgs` export — run
from a repo checkout that has it, or re-export with
`orbitvega.export_quest`. The rest of that file's assertions run against the
committed golden JSON and pass without the asset.
