# WebClient

Four browser pages, served by `system/Server` at `/web`, one per system under
comparison.

Start at **`/web/compare.html`** — the chooser. It asks `/api/systems` which
systems have their assets present, and for our own system it lists each
object's published ladder cost so you can pick a scene the link can actually
carry before launching. Every page carries a `switch` link back to it.

| Page | System | Runs the ABR? |
|---|---|---|
| `/web/compare.html` | the chooser — availability, scene picker, and which systems adapt | — |
| `/web/` | **ours** — adaptive mesh ladder | yes, in the browser |
| `/web/baseline.html` | ViVo / NAVA point clouds (`?bridge=` selects which) | yes, server-side |
| `/web/vega.html` | Vega (3D Gaussian splatting) | no, fixed quality |
| `/web/nevo.html` | NeVo (ReRF neural volumetric) | no, pre-rendered |

The first page is the important one: it runs the **same** `system/ClientCore`
as the Node desktop client — same MCKP ABR, same segment loop, same bandwidth
estimator, same metrics — behind a browser implementation of the
`ClientPlatform` contract. So a desktop/browser difference is a real difference
in the system under test, not a difference between two hand-written clients.

The other three do **not** use ClientCore, deliberately. ClientCore models an
HTTP segment loop over a published ladder; the baselines push frames at their
own cadence and adapt server-side. Wrapping them in a segment loop would
measure the wrapper.

## Build and run

```bash
cd system/WebClient && npm install && node build.js     # --watch to rebuild on change
cd ../.. && PYTHON_BIN=<env-python> scripts/run_web_demo.sh
```

That starts everything — the server on the **H.264** corpus plus a supervised
ViVo and NAVA — and prints the URLs. Start at
`http://<host>:3000/web/compare.html`. Ctrl-C stops all of it.

Two defaults in that script are deliberate. It serves H.264 because HEVC is
Firefox-no and Chrome-only-with-hardware, and it sets 300 segments because the
server's own default of 10 is a *trial* length: the page reaches "run complete"
in 20 s, before you have finished looking at it.

To make adaptation visible rather than merely running, replay a trace in
another shell (needs root, and shapes the whole host):

```bash
sudo scripts/shape_web_demo.sh cascade-20
```

## Page parameters

Query strings, mirroring how the Node client takes environment variables.

**`/web/` (ours)**

| parameter | default | meaning |
|---|---|---|
| `server` | page origin | server base URL, for a cross-host run |
| `mode` | `interactive` | `interactive` \| `simulated` |
| `storage` | `memory` | `memory` \| `opfs` asset store |
| `viewpoints` | — | viewpoint-index JSON; **required** for `simulated` |
| `decodeBudget` | `512` | decoded-clip cache budget, MB |
| `concurrency` / `inflight` | `10` / `2` | parallel asset requests / concurrent segments |
| `label` | `web-client` | run label recorded on the server |
| `objects` | server's full catalog | comma-separated object subset |

`simulated` has no default viewpoint file on purpose: solving the first ladder
against an arbitrary pose would silently invalidate the viewpoint-aware
comparison.

`objects` is the parameter that decides whether a run demonstrates adaptation
or a permanent deficit. The ladder publishes at least one representation per
object in the scene, so the scene size sets an irreducible floor: all nine
ORBIT objects floor at **116 Mbps**, which no ordinary link carries, and the
MCKP then buys the few highest-weighted objects at their cheapest rung and
freezes the rest for the whole run. Three objects floor near **21 Mbps**. An
absent list resets the server to its full catalog, so a run never inherits the
previous one's scene. The Node client reads the same subset from
`VS4D_SCENE_OBJECTS`.

**`/web/baseline.html`** — `bridge` (`ws://<host>:8790`), `pointSize` (`0.012` m),
`strict` (`0`; `1` aborts on a frame gap instead of resynchronising).

Which point-cloud baselines can actually run depends on the corpus, and the two
cases need opposite responses:

`?bridge=` is what selects the baseline: the page is identical either way, and
`/api/systems` probes each port so the chooser marks a baseline ready only when
something is actually listening on it (which is different from the tile corpus
merely existing -- the two need opposite fixes). Override the port map with
`VS4D_POINTCLOUD_PORTS=vivo:8790:12345,nava:8791:12346`.

- **ViVo and NAVA** run from the prepared tile corpus with
  `--tile-catalog-ladder`. Everything they read at serve time is in
  `catalog.json`, and the cameras they put in the connection header are
  synthetic per-tile identities, so the absent RGB-D source costs them nothing.
  See [`baselines/ViVo/orbitvivo/tile_ladder.py`](../../baselines/ViVo/orbitvivo/tile_ladder.py).
- **MetaStream, DeltaStream and LiVo** index the real capture rig while serving
  (`obj.cameras[camera_index]`) and read the source RGB-D frames. For them the
  absent corpus is missing *data*, and no adapter can substitute for it.

**`/web/vega.html`** — `assets` (`/vega-assets`), `objects` (all nine),
`frame` (`object`\|`all`), `splatMode` (`isotropic`\|`anisotropic`),
`splatScale` (`1.0`).

The Vega page **loads the whole clip before playing** and then loops it from
memory; expect ~15 s of visible progress on a 35 Mbps link for the 64 MB
two-object export. It is not streamed, and cannot be: a frame is ~1.1 MB at
30 fps, so two objects in real time would need **509 Mbps**. The full
nine-object export is ~345 MB, so use `?objects=` (or the chooser's picker,
which defaults to the two smallest clips) rather than opening all of them. The earlier
sliding-window prefetch only worked when served from the same machine — over a
real link it issued requests faster than they completed and, because it checked
only the decoded cache, re-issued each pending frame every 33 ms tick until
Chrome's socket pool gave out and every fetch returned a bare
`TypeError: Failed to fetch` with no backoff. Fetches are now deduplicated by
`(object, frame)`, bounded to 6 in flight, and a failed frame is remembered
rather than retried.

**`/web/nevo.html`** — `assets` (`/nevo-assets`), `object` (`g_dancer`),
`fps` (`8`), `nevoOnly` (`0`).

### Getting the baselines to serve something

```bash
# point clouds: raw TCP, which a browser cannot open, so bridge it. This runs
# the bridge and supervises the baseline server, because a baseline serves one
# connection from frame zero and then exits -- correct for a measured trial,
# but it means an unsupervised server survives exactly one page load.
# Run both: they take different default ports (vivo 8790, nava 8791), so the
# chooser offers either and switching is a click rather than a restart.
PYTHON_BIN=<env-python> scripts/serve_pointcloud_baseline.sh vivo dancer,thomas &
PYTHON_BIN=<env-python> scripts/serve_pointcloud_baseline.sh nava dancer,thomas &
# -> /web/baseline.html?bridge=ws://<host>:8790   (vivo)
# -> /web/baseline.html?bridge=ws://<host>:8791   (nava)

# vega: export the trained bitstream to the portable VGS format once
python -m baselines.Vega.orbitvega.export_quest \
  --prepared-dir results/vega-gaussian/prepared-final --output-dir results/vega-web \
  --dataset-root <ORBIT_datasets_gaussian> --objects dancer thomas
# -> /web/vega.html?objects=dancer          (override location: VS4D_VEGA_WEB_ROOT)

# nevo: renders come from orbitnevo/render_frames.py
# -> /web/nevo.html?object=g_dancer         (override location: VS4D_NEVO_WEB_ROOT)
```

## Files

| File | Role |
|---|---|
| `src/browser-platform.js` | The `ClientPlatform` capabilities: transport, storage, viewpoints, clock, logger, lifecycle |
| `src/webgl-renderer.js` | Renderer capability: Three.js scene, frame presentation, decode orchestration |
| `src/decode-cache.js` | Decoded-clip LRU under a byte budget, keyed `(objectName, repId)` |
| `src/texture-decoder.js` | MP4 demux (mp4box) + WebCodecs `VideoDecoder` |
| `src/draco-worker.js` | Draco mesh and point-cloud decode, off the main thread |
| `src/camera-pose.js` | Three.js camera → Open3D `PinholeCameraParameters` |
| `src/main.js` | Entry point for `/web/` |
| `src/chooser.js` | The chooser: `/api/systems`, the scene picker, and the floor-vs-capacity check |
| `bridge/v4ds-bridge.js` | WebSocket ↔ TCP proxy for the point-cloud baselines; reframes only |
| `src/v4ds-protocol.js` | V4DS CONNECTION/FRAME decode, FEEDBACK encode |
| `src/point-reconstruction.js` | MetaStream/DeltaStream delta reconstruction |
| `src/point-renderer.js` | `THREE.Points` per object |
| `src/vgs-format.js` | VGS1 decoder — positions, scales, rotations, opacity, colour |
| `src/splat-renderer.js` | Instanced splat quads, depth sort, GLSL3 shaders |
| `src/nevo-manifest.js` | Conditions, frame filenames, panel layout, captions (pure) |
| `src/nevo-client.js` | Preload, subject crop, 2D-canvas compositing |
| `src/{baseline,vega,nevo}-client.js`, `*-main.js` | Per-page logic and wiring |
| `public/*.html` | Canvas, status line, log pane, artifact links |
| `vendor/draco/` | Official Draco JS/WASM decoder, unmodified |

## Pitfalls

Each of these cost real debugging time. Most fail *silently* — plausible output
rather than an error — which is why they are written down.

**Measurement integrity**

- Media and API fetches are `cache: 'no-store'`. A segment served from the HTTP
  cache turns a shaped-bandwidth measurement into fiction; a cached manifest
  pins the client to a stale ladder with no error.
- Stop finalizes, it does not navigate. The final `POST /api/results` happens
  during shutdown, so unloading would cancel it. `pagehide` finalizes too.
- Telemetry writes never block: 30 Hz render telemetry through a synchronous
  write per frame starved the renderer in Node.
- Only decodes are cached, never downloads. A 1920-wide frame is `w*h*1.5` =
  5.5 MB decoded however it was stored, so a 60-frame clip × 5 objects is
  1.66 GB. The cache pins the clip on screen, since evicting *that* looks like
  a frame reverting mid-playback rather than an error.
- Bandwidth shaping stays `tc` on the Linux host, exactly as
  `system/Client/run-client-experiments.sh` does it — the browser is just
  another process behind the same qdisc. DevTools throttling is not scriptable
  enough to be an experimental control.
- **Without shaping the demo cannot show adaptation**, only run it. On an
  unshaped LAN every adaptive system settles on one operating point: NAVA held
  quality level 5 for thirteen consecutive segments while its DP solved each
  one. Replay a trace with `sudo scripts/shape_web_demo.sh cascade-20` (a
  12.5 → 25 → 50 → 100 → 175 Mbps staircase in 20 s steps) and both `/web/` and
  `/web/baseline.html` show the enforced rate on their `link:` line beside the
  client's own estimate, so a representation switch reads as cause and effect.
  `GET /api/shaping` is the unprivileged source — reading `tc` needs no root,
  only installing rules does. Note the trace player installs a **root** token
  bucket, so it shapes every outbound flow on the host, not just the demo;
  that is inherited from the Quest methodology because a destination `u32`
  filter silently misses traffic on a multiqueue NIC, and these NICs are
  multiqueue.

**Formats and coordinates**

- The pose POSTed to `/api/viewpoint` must be Open3D `PinholeCameraParameters`;
  the server writes it straight to disk and reads it with
  `o3d.io.read_pinhole_camera_parameters`. Anything else leaves the ladder
  unable to parse it, and it returns **equal** object weights with no error —
  silently disabling the whole point of the system. `camera-pose.js` handles
  both conversions: Three.js is Y-up/−Z-forward, Open3D is Y-down/+Z-forward,
  and translations are in **millimetres** (`VIEW_RAYCAST_UNITS_PER_METER`).
- V4DS is **big-endian**; VGS1 is **little-endian**. Same client decodes both.
- Streamed baseline points are in **camera** space; `camera_to_world` arrives
  **row-major**, and `THREE.Matrix4.fromArray` is column-major, so handing it
  over directly transposes every camera.
- V4DS timestamps are epoch **nanoseconds** (~1.8×10¹⁸, ~200× `MAX_SAFE_INTEGER`)
  and need BigInt. Frame ids stay Numbers; `sourceTimestampMs` is derived.
- VGS1 quaternion components are **signed** bytes. Read unsigned, every
  rotation mirrors into a still-plausible blob of Gaussians.

**Rendering**

- The camera auto-frames the content. ORBIT subjects are baked into venue world
  coordinates with three floor tiers (`scene_layout.json`), so a mesh can sit
  metres above the origin — the first `dancer` frame centres at y=3.03. Framing
  stops as soon as the viewer touches the camera; their pose is the input.
- **Fitting the whole scene is the wrong default for a two-object viewer.** The
  same venue layout puts `dancer` at z −1.8..−0.9 and `thomas` at z +8.0..+8.4,
  ten metres apart, so a camera that fits both retreats to z 18.5 and each
  subject covers **0.43%** of the canvas — indistinguishable from an empty
  player. Vega therefore frames one object (**3.6–5.1%** coverage), names the
  others in its log, and offers a `Focus` button to cycle; `?frame=all`
  restores the whole-venue fit for inspecting the layout itself.
- Geometry gaps hold, they do not shift: a null frame re-presents the previous
  one, which is why `ClientCore/download-plan.js` preallocates a null slot per
  frame rather than collecting only successes.
- Splats: data lives in a texture and only a 16-bit-depth counting sort over an
  index attribute is reordered (230 KB instead of ~3 MB per sort). Depth test
  and depth write are both off — in 3DGS the ordering *is* the depth resolution.
- **A `SplatObject` must be constructed at its sequence's real splat count.**
  Three caches `_maxInstanceCount` from the instanced attributes the first time
  it binds a geometry, and draws
  `min(geometry.instanceCount, _maxInstanceCount)`. Swapping in a bigger
  `splatIndex` attribute afterwards does not reliably reset that cache, so a
  geometry first bound at a placeholder capacity keeps drawing **one** instance
  however large `instanceCount` is — 4 triangles instead of 213,224, an
  empty-looking canvas with every other diagnostic reading healthy. Whether it
  happened depended on whether the render loop bound the mesh before the first
  frame arrived, so it failed on roughly three page loads in four while the
  stats panel still reported 108,342 splats and zero decode failures.
  `_allocate` now throws on post-draw growth instead of capping silently, the
  mesh stays hidden until it has data, and `VegaClient` takes each capacity
  from the catalogue.
- Vega colour is **baked**, not view-dependent: `export_quest.py` quantises a
  view-independent sample per splat. Geometry and opacity are exact; appearance
  is the same approximation the offline evaluator uses, so it is comparable
  with those numbers but is not the paper's full appearance.
- NeVo has no camera control because it cannot run client-side: a frame takes
  ~0.5 s to ray-march on a workstation GPU and its entropy decoder is CUDA +
  Python 3.8. The page plays pre-rendered frames — plain ReRF, NeVo's
  visibility-filtered output, and the captured camera at the same instant and
  viewpoint, each captioned with its kept voxel fraction. All renders preload
  before playback (a condition one frame behind would be indistinguishable from
  a real difference) and the crop is measured once and applied to every panel.

## Known limitations

- **Texture codec.** The decoder is codec-agnostic: it reads `track.codec` and
  whichever of avcC/hvcC the file carries, so it decodes H.264
  (`avc1.64001f`) and HEVC (`hvc1.1.6.L90.90`) through the same path. **Serve
  the H.264 corpus for a browser audience** — HEVC is Safari-yes, Firefox-no,
  Chrome-only-where-the-OS-provides-hardware-decode, and
  `analysis/compare_codec_rd.py` measures H.264 at just **1.003x** the bitrate
  of HEVC at matched quality across all nine objects, so the ladder's floor and
  ceiling barely move. Where the served codec cannot be decoded the page
  renders untextured geometry and logs the reason once per object
  (`texture-decoder.js: probeCodecSupport`). Never transcode at serve time:
  the quality/bitrate models are codec-specific, so re-encoding changes
  delivered bitrate out from under the ladder.


- **`splatMode=anisotropic` is untested.** Isotropic is the default because
  Vega's Gaussians are near-isotropic (log scales within ~0.3) and because a
  vertex shader that merely *contains* the covariance projection draws nothing
  under headless SwiftShader, even when `gl_Position` ignores the result. Every
  input was verified independently, which points at the driver. Try it on real
  GPU hardware.
- **MetaStream, DeltaStream and LiVo are not in the demo.** They index the real
  capture rig while serving (`obj.cameras[camera_index]`) and read the source
  RGB-D frames, which are absent with no surviving copy — missing data, not
  missing metadata, so no adapter can substitute. LiVo additionally needs
  client-side depth unprojection for its `LIVO_SEGMENT` colour+depth messages;
  the protocol decoder recognises the type and says so rather than failing
  silently.
- **WebCodecs texture decode has never succeeded here**, for the HEVC reason
  above; only the degradation path is exercised. OrbitControls interaction is
  untested (headless has no pointer).
- **The multi-view NeVo sprite path** (`export_quest.py`, 8 yaw views/frame)
  needs `rerf_render.py --render_views 8` under the ReRF CUDA environment.
  Those renders do not exist, so the viewer is single-view.

## Tests

```bash
node --test tests/test_browser_platform.js tests/test_decode_cache.js \
  tests/test_camera_pose.js tests/test_v4ds_protocol.js \
  tests/test_v4ds_bridge.js tests/test_point_reconstruction.js \
  tests/test_vgs_format.js tests/test_nevo_manifest.js \
  tests/test_web_client_bundle.js
```

The binary decoders are checked against the authoritative Python
implementations on real data: `test_v4ds_protocol.js` against golden bytes from
the Python encoder plus a live round-trip, `test_point_reconstruction.js`
point-for-point, `test_vgs_format.js` on a real exported frame,
`test_camera_pose.js` against a real captured viewpoint file. The
`fixtures_*.gen.py` scripts regenerate the golden files.
`test_v4ds_bridge.js` covers adversarial TCP chunk boundaries and a 3 MiB
message through real sockets. `test_web_client_bundle.js` loads the **built**
`dist/main.js` in a stubbed DOM and drives a complete simulated run, so run
`node build.js` first.

### Debugging in a real browser

`window.__vs4d.inspect()` returns live renderer state — object visibility,
vertex counts, camera pose, decode-cache stats — which is the fastest way to
tell "nothing is drawn" from "drawn off-screen". Two headless traps:

1. `--virtual-time-budget` deadlocks the Draco worker: `importScripts` is
   synchronous and the worker goes silent with no error. Drive the page with
   puppeteer-core against the installed Chrome instead.
2. `page.screenshot()` comes out black — SwiftShader does not composite the
   WebGL surface. Call `renderer.render()` and `gl.readPixels()` in the *same*
   synchronous task, since `preserveDrawingBuffer: false` clears after
   compositing.
