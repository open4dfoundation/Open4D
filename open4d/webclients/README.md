# webclients

A browser user study for volumetric streaming methods, and the clients it
runs them in. [`../reconstruction`](../reconstruction) holds the methods; this
holds the app that delivers them to a participant under one network trace and
one camera path, and asks them to rate each. Not to be confused with
[`../streamer`](../streamer), the Python bundle server, or
[`../streaming`](../streaming), RGB-D reconstruction sent over TCP.

The methods it knows:

| system | representation |
|---|---|
| Ours | textured meshes, viewpoint-aware ladder |
| ViVo | point clouds, 4×4×4 tiles |
| NAVA | point clouds |
| Vega | 3D Gaussian splats |
| NeVo | ReRF neural volumetric |

## Quick start

```bash
cd system/WebClient && npm install && node build.js
cd ../.. && PYTHON_BIN=<env-python> scripts/run_web_demo.sh
```

Open the URL it prints (`…:3000/web/`): that is the study. Each method's own
page is still there on its own (`mesh.html`, `baseline.html`, `vega.html`,
`nevo.html`) for looking at one without running a session. Ctrl-C stops
everything.

A study trial shapes the link itself, from the trace chosen in setup. Outside
the study, `scripts/shape_web_demo.sh` still replays a trace with `tc` for the
method pages, with the caveats in that script: it needs root, and it shapes
every flow on the host.

**Needs the research repo.** The ladder solver (`vstream/`), the baseline
servers (`baselines/`) and the corpora live in `4DVideoStreaming`, so
`run_web_demo.sh` expects a checkout of it. Without one you can still build the
clients and run the tests.

## Adding a method

A method takes part in the study once it has a page that can run a study pass
and the study knows about it:

1. **The page.** Read `study.studyParams()`; when set, load the context with
   `study.loadStudyContext`, hide the page chrome with `study.enterStudyChrome`,
   and hand the renderer's camera to `study.createStudyDriver` -- the renderer
   calls `driver.apply(camera, controls, info)` once per frame, just before its
   orbit update. See `src/study/harness.js`; `src/vega-main.js` is the smallest
   example.
2. **The study.** Add the method to `METHODS` in
   [`system/Server/study/store.js`](system/Server/study/store.js) and to
   `STUDY_PAGES` in `src/study/study-page.js`, and give it a page in `build.js`
   and `public/`. `/api/systems` in `system/Server/server.js` says whether it
   is available and which objects it has.
3. **Its bytes.** Whatever it downloads must pass the study's shaper: `/files`
   and `/vega-assets` do in the server, and the WebSocket bridge does for the
   V4DS baselines.

If a client **adapts in the browser**, implement the `ClientPlatform` contract in
[`system/ClientCore/platform.js`](system/ClientCore/platform.js) and hand it to
`StreamingClient` for the segment loop, MCKP selector, bandwidth estimation and
metrics. If it **adapts server-side or not at all**, write a plain page and do
*not* wrap it in `ClientCore`: that models an HTTP segment loop over a published
ladder, and forcing a push-based or fixed-quality method into it measures the
wrapper rather than the method.

## User study

`…:3000/web/study.html` runs one participant through every chosen method under
the **same network trace, from the same start view, along the same camera path**,
then asks them to rate each clip. It is the browser counterpart of the Quest
study (`scripts/user_study.py` in the research repo), and uses the same
questionnaire wording and the same counterbalancing.

1. **Setup.** A pseudonymous participant code (never a name), the methods, the
   objects on stage, the trial length, and a network trace: a built-in one from
   `system/Client/traces/` or an uploaded `time,bandwidth` CSV in seconds and
   Mbps, optionally windowed and scaled.
2. **Record the path.** The participant drives one unrated pass on Ours, from
   a fixed start that frames every object on stage. That path is the one every
   trial replays, including the first, so no method is rated from active
   viewing while the others are rated from passive viewing. It can be
   downloaded and reused for later participants.
3. **Trials.** In Williams order keyed on the participant number, each shown only
   as "Clip A", "Clip B"… Each trial re-arms the trace at its start, replays the
   path on the content clock, and ends after the trial length.
4. **Questionnaire.** C1–C4 on five stars, then up to two artifacts ("None of
   the above" is exclusive), with time on each page recorded.
5. **Results.** A table, and `export.csv`: one row per trial with the ratings,
   artifacts, and the same playback metrics for every method.

```bash
PYTHONPATH=/path/to/4DVideoStreaming PYTHON_BIN=<env-python> scripts/run_web_demo.sh
# then open http://<host>:3000/web/
```

Sessions are written under `system/Server/server_results/study/`, one
self-contained directory each: the trace exactly as uploaded with its hash, the
path, and one `trial.json` per trial. That directory is ignored by git, since
it holds participant data.

What the study does **differently from the Quest** is how it shapes the link,
because this one must accept a trace uploaded from a browser:

- The trace is applied **in-process**, not with `tc`. `/files` responses pass
  through a token bucket in the server (`system/Server/study/shaper.js`) and
  the ViVo/NAVA WebSocket bridge follows the same trace from the same instant
  (`follower.js`). It needs no root and throttles nothing else on the host;
  what it gives up is a kernel queue under TCP, so congestion control sees
  pacing rather than loss. The burst and step semantics match
  `quest-trace-player.js`.

Which methods can take part, and what the rest are missing:

| method | in the study | notes |
|---|---|---|
| Ours (mesh) | yes | adapts in the browser |
| ViVo, NAVA | yes | restarted on the session's objects before each trial; need the tile corpus |
| Vega | yes | only for sessions of objects it has exported (here: `dancer`, `thomas`); preloads its 30-frame clip, then loops |
| NeVo | no | pre-rendered from one capture camera with no camera control, so it cannot follow a shared path |
| LiVo | no | the browser client does not decode LiVo's RGB-D segments, and no LiVo package is prepared |
| MetaStream, DeltaStream | no | need the RGB-D dataset from `baselines.DeltaStream.orbitstream.converter` |

The setup page lists all of them, and says why a method cannot be chosen —
including when it lacks one of the session's objects.

**Which method records the path is a setup choice.** Any that can take part
will do, but it matters: the participant has seen that method before rating
it. Ours in particular may not show the whole stage at startup — it begins with
a 5 Mbps estimate, and the five stage objects need about 89 Mbps even on the
H.264 corpus — so the practice pass holds the start view until every object is
visible, for up to 60 s, and flags a path recorded over a partial stage.

Serve the **H.264** corpus (`ORBIT_datasets_compressed_h264`, the one
`run_web_demo.sh` defaults to) where possible: every browser decodes it. The
HEVC corpus decodes only in Safari, and in Chrome with suitable hardware.

## Tests

```bash
node --test tests/*.js      # 347 of 349
```

The other two cross-check the JS decoders against the Python ones byte for
byte, so they need `PYTHONPATH=/path/to/4DVideoStreaming` and
`VS4D_TEST_PYTHON=<env-python>`.

## Layout

`system/ClientCore` is the platform-free logic; `system/WebClient` the study, the method
pages and the WebSocket↔TCP bridge; `system/Server` publishes the ladder and
serves media; `tile_ladder.py` lets ViVo/NAVA serve prepared tiles without the
RGB-D source. The `system/` level is kept so relative imports resolve unchanged
against the research repo — load-bearing, not a leftover.

Pitfalls worth reading before changing a client are in
[`system/WebClient/README.md`](system/WebClient/README.md); most fail silently.

This directory began as a copy of the research repo's clients (see
`PROVENANCE`) and is developed here now; it is not kept in sync with that repo.
It still needs a checkout of it at run time, for the ladder solver and the
baseline servers.
