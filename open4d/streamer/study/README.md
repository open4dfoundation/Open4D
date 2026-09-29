# study

A browser user study for volumetric video streaming. One participant watches
several methods under the **same network trace, start view and camera path**,
then rates each clip. It's a separate Node app: it doesn't use the `streamer`
Python package next to it and isn't part of its wheel. The methods themselves
are in [`../../reconstruction`](../../reconstruction).

| Method | Content | In the study |
|---|---|---|
| Ours | textured meshes, viewpoint-aware ladder | yes |
| ViVo, NAVA | point clouds | yes (needs the tile corpus) |
| Vega | 3D Gaussian splats | yes, for objects it has exported (`dancer`, `thomas`) |
| NeVo | ReRF, pre-rendered | no: fixed camera, can't follow a path |
| LiVo, MetaStream, DeltaStream | RGB-D | no: need source RGB-D data that isn't available |

## Run it

It needs a checkout of the
[4DVideoStreaming](https://github.com/frozzzen3/4DVideoStreaming) research repo,
which has the ladder solver, the baseline servers and the datasets.

```bash
cd system/WebClient && npm install && node build.js
cd ../.. && PYTHONPATH=/path/to/4DVideoStreaming PYTHON_BIN=<env-python> scripts/run_web_demo.sh
```

Open the printed `http://<host>:3000/web/`. Ctrl-C stops everything. Each
method also has its own page (`mesh.html`, `baseline.html`, `vega.html`,
`nevo.html`); see [`system/WebClient`](system/WebClient/README.md).

Serve the H.264 corpus (the default): every browser can decode it, but HEVC
only plays in Safari and hardware-enabled Chrome.

## A session

1. **Setup:** participant code (never a name), methods, objects, trial length
   and network trace. Pick a built-in trace from `system/Client/traces/` or
   upload a `time,bandwidth` CSV (seconds, Mbps).
2. **Record the path:** the participant drives one unrated practice pass. Every
   trial replays that same camera path.
3. **Trials:** blinded as "Clip A", "Clip B"…, in Williams order. Each one
   restarts the trace from its beginning.
4. **Questionnaire:** C1–C4 on five stars, plus up to two artifacts.
5. **Results:** a table and `export.csv`, one row per trial.

Each trial also records the GPU the browser rendered with. The setup page
warns if that's a software renderer (for example, headless Chrome's
SwiftShader), because methods play far below their frame rate there. Vega
loads its whole clip *before* the trace starts, so its trial starts at
playback like every other method (`preloadSeconds` records the load time).

Sessions are saved in `system/Server/server_results/study/`. That directory
holds participant data and is git-ignored.

The trace is applied inside the server (`system/Server/study/shaper.js`, and
`follower.js` for the ViVo/NAVA bridge), so it needs no root and doesn't
throttle anything else on the machine. The CSV's `shaped_by` and
`shaped_bytes` columns show which shaper carried each trial's bytes and how
many; the bridge reports its own at `GET /shaping`. Outside the study,
`sudo scripts/shape_web_demo.sh <trace>` shapes the method pages with `tc`,
which throttles the whole machine.

## Adding a method

1. **Page:** if `study.studyParams()` is set, call `study.loadStudyContext`,
   `study.enterStudyChrome` and `study.createStudyDriver`. The renderer then
   calls `driver.apply(camera, controls, info)` once per frame.
   `src/vega-main.js` is the smallest example.
2. **Register it** in `METHODS` (`system/Server/study/store.js`), in
   `STUDY_PAGES` (`src/study/study-page.js`), in `build.js` and in `public/`.
3. **Shape its traffic:** every download must go through the study's shaper.
4. **If it loads a whole clip before playing**, add it to `PRELOAD` in
   `src/study/study-page.js`. With `?preload=1`, the page posts `preloaded`,
   then waits for `study.awaitGo()` before it starts its driver.

If the method adapts in the browser, implement the
[`ClientPlatform`](system/ClientCore/README.md) contract and use
`StreamingClient`. If it adapts on the server, or not at all, write a plain page.

## Layout and tests

| Path | |
|---|---|
| `system/ClientCore` | browser-independent streaming logic: ABR, bandwidth estimation, metrics |
| `system/WebClient` | the study and the method pages, plus the WebSocket↔TCP bridge |
| `system/Server` | serves the ladder, media and study sessions |
| `tile_ladder.py` | lets ViVo/NAVA serve prepared tiles without the RGB-D source |

The `system/` level mirrors the research repo so relative imports still work.
This directory started as a copy of that repo (see `PROVENANCE`) and is now
developed here.

```bash
node --test tests/*.js
```

Two tests compare the JS decoders against the Python ones, so they need
`PYTHONPATH=/path/to/4DVideoStreaming` and `VS4D_TEST_PYTHON=<env-python>`.
