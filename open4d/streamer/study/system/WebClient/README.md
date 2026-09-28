# WebClient

The user study and one browser page per method, served by `system/Server` at `/web`.

| Page | Method | Adapts |
|---|---|---|
| `/web/` | the user study | — |
| `/web/mesh.html` | Ours, adaptive mesh ladder | in the browser, via `ClientCore` |
| `/web/baseline.html` | ViVo / NAVA point clouds (`?bridge=` picks which) | on the server |
| `/web/vega.html` | Vega Gaussian splats | no, fixed quality |
| `/web/nevo.html` | NeVo (ReRF), pre-rendered | no |

Only `mesh.html` uses `ClientCore`, the same core the research repo's Node
client runs. The baselines push frames and adapt on the server, and wrapping
them in a segment loop would measure the wrapper instead of the method.

## Build and run

```bash
npm install && node build.js           # --watch to rebuild on change
cd ../.. && PYTHON_BIN=<env-python> scripts/run_web_demo.sh
```

This starts the server on the H.264 corpus with 300 segments, plus ViVo and
NAVA. To see adaptation, shape the link (needs root; affects the whole host):

```bash
sudo scripts/shape_web_demo.sh cascade-20      # 12.5 → 175 Mbps staircase
```

## Page parameters

**`mesh.html`**: `server` (page origin), `mode` (`interactive` | `simulated`),
`storage` (`memory` | `opfs`), `viewpoints` (required for `simulated`),
`decodeBudget` (512 MB), `concurrency` / `inflight` (10 / 2), `label`,
`objects` (comma list; default is the full catalog).

Keep `objects` small. Every object needs at least one representation, so all
nine ORBIT objects need **116 Mbps** before any quality choice, while three need
about 21 Mbps.

**`baseline.html`**: `bridge` (`ws://<host>:8790`), `pointSize` (0.012 m),
`strict` (1 = abort on a frame gap). Override the ports with
`VS4D_POINTCLOUD_PORTS=vivo:8790:12345,nava:8791:12346`.

**`vega.html`**: `assets`, `objects`, `frame` (`object` | `all`),
`splatMode` (`isotropic` | `anisotropic`; anisotropic is untested), `splatScale`.
The whole clip loads before playback, then loops. Streaming it live would need
about 509 Mbps for two objects.

**`nevo.html`**: `assets`, `object` (`g_dancer`), `fps` (8), `nevoOnly`.

## Serving the baselines

```bash
# point clouds: a supervised server + WebSocket bridge per baseline
PYTHON_BIN=<env-python> scripts/serve_pointcloud_baseline.sh vivo dancer,thomas &   # :8790
PYTHON_BIN=<env-python> scripts/serve_pointcloud_baseline.sh nava dancer,thomas &   # :8791

# vega: export once to VGS (location: VS4D_VEGA_WEB_ROOT)
python -m baselines.Vega.orbitvega.export_quest \
  --prepared-dir results/vega-gaussian/prepared-final --output-dir results/vega-web \
  --dataset-root <ORBIT_datasets_gaussian> --objects dancer thomas

# nevo: renders from orbitnevo/render_frames.py (location: VS4D_NEVO_WEB_ROOT)
```

## Main files

| File | Role |
|---|---|
| `src/browser-platform.js` | the browser `ClientPlatform` |
| `src/webgl-renderer.js`, `decode-cache.js`, `texture-decoder.js`, `draco-worker.js` | mesh rendering and decoding |
| `src/camera-pose.js` | Three.js camera → Open3D pose |
| `src/study/` | study page, questionnaire, trial driver and camera path |
| `bridge/v4ds-bridge.js`, `src/v4ds-protocol.js` | WebSocket↔TCP bridge and the V4DS wire format |
| `src/vgs-format.js`, `splat-renderer.js` | Vega splat decoding and rendering |
| `src/nevo-*.js`, `src/{baseline,vega}-client.js` | per-page logic |

## Pitfalls

Most of these fail silently.

- **No HTTP caching.** Media and API fetches use `cache: 'no-store'`;
  otherwise bandwidth measurements are wrong and manifests go stale.
- **Stop must not navigate away.** Results are POSTed during shutdown.
- **Viewpoints must be Open3D `PinholeCameraParameters`** (Y-down, +Z forward,
  millimetres). Anything else silently gives every object equal weight.
- **Byte order and number types:** V4DS is big-endian with nanosecond BigInt
  timestamps. VGS1 is little-endian, and its quaternions are *signed* bytes.
- **Matrix order:** `camera_to_world` arrives row-major, but Three.js is
  column-major.
- **Size `SplatObject` from the catalogue.** Three.js caches the instance count
  on first bind, so growing it later draws one splat.
- **HEVC** only decodes in Safari and hardware-enabled Chrome, and where it
  can't decode, pages fall back to untextured meshes. Serve H.264 and never
  transcode at serve time.

Known gaps: WebCodecs texture decode and mouse camera controls are untested
here, and NeVo has only a single view.

## Tests and debugging

```bash
node build.js && node --test ../../tests/*.js     # the bundle test needs dist/
```

In the browser, `window.__vs4d.inspect()` shows renderer state. For headless
runs, use puppeteer-core with real Chrome: `--virtual-time-budget` deadlocks
the Draco worker. Screenshots come out black under SwiftShader, so call
`gl.readPixels()` in the same task as `renderer.render()`.
