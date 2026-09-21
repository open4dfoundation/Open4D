# webclients

Browser clients for volumetric adaptive streaming. A sibling of
[`../reconstruction`](../reconstruction): that holds the methods, this holds the
clients that deliver and play them. Named for what it is rather than for what
it does, so that `streamer` -- the Python module in `../reconstruction` that
serves bundles -- and this are not two things both called streaming. Five systems on one page, side by side:

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

Open the URL it prints (`…:3000/web/compare.html`) and pick a system. Ctrl-C
stops everything. To *see* adaptation rather than just run it, replay a trace in
another shell — without it every adaptive system settles on one operating point
and looks static:

```bash
sudo scripts/shape_web_demo.sh cascade-20      # 12.5 → 175 Mbps staircase
```

**Needs the research repo.** The ladder solver (`vstream/`), the baseline
servers (`baselines/`) and the corpora live in `4DVideoStreaming`, so
`run_web_demo.sh` expects a checkout of it. Without one you can still build the
clients and run the tests.

## Adding a method

Write a page, then add one line to `DESCRIPTION` in `src/chooser.js`, one entry
to `build.js`, and one `public/<page>.html`. The chooser lists whatever
`/api/systems` reports, one row each, so it stays readable as methods pile up.

If your client **adapts in the browser**, implement the `ClientPlatform`
contract in [`system/ClientCore/platform.js`](system/ClientCore/platform.js)
and hand it to `StreamingClient` — you inherit the segment loop, MCKP selector,
bandwidth estimation and metrics. `src/browser-platform.js` is the reference.

If it **adapts server-side or not at all**, write a plain page and do *not* wrap
it in `ClientCore`: that models an HTTP segment loop over a published ladder,
and forcing a push-based or fixed-quality method into it measures the wrapper
rather than the method. `src/vega-main.js` is the smallest example.

## Tests

```bash
node --test tests/*.js      # 292 of 294
```

The other two cross-check the JS decoders against the Python ones byte for
byte, so they need `PYTHONPATH=/path/to/4DVideoStreaming` and
`VS4D_TEST_PYTHON=<env-python>`.

## Layout

`system/ClientCore` is the platform-free logic; `system/WebClient` the pages,
chooser and WebSocket↔TCP bridge; `system/Server` publishes the ladder and
serves media; `tile_ladder.py` lets ViVo/NAVA serve prepared tiles without the
RGB-D source. The `system/` level is kept so relative imports resolve unchanged
against the research repo — load-bearing, not a leftover.

Pitfalls worth reading before changing a client are in
[`system/WebClient/README.md`](system/WebClient/README.md); most fail silently.

This directory is vendored from the research repo, the same way
`../reconstruction` holds its methods. `PROVENANCE` records the commit it came
from, and `scripts/check-sync.sh` reports whether the two have drifted:

```bash
VS4D_REPO=/path/to/4DVideoStreaming scripts/check-sync.sh
```
