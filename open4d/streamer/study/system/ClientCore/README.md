# ClientCore

Platform-free streaming logic, shared by the Node desktop client
(`system/Client`) and the browser client (`system/WebClient`).

The rule: **nothing here may import `fs`, `http`, `https`, `path`, `os` or
`process`, or touch `console`, `Date.now`, `setInterval` or `fetch` directly.**
Everything platform-shaped goes through the `ClientPlatform` contract in
[platform.js](platform.js), and `tests/test_client_core_purity.js` enforces it.
If both clients run the same core, a difference between their results is a real
difference in the system under test rather than a difference between two
hand-written clients.

## Contents

| File | Role |
|---|---|
| [streaming-client.js](streaming-client.js) | `StreamingClient` — segment loop, playback clock, download orchestration, server reporting, shutdown |
| [platform.js](platform.js) | The contract: typedefs, `PLATFORM_CONTRACT` as data, `validatePlatform` |
| [testing/fake-platform.js](testing/fake-platform.js) | In-memory platform — virtual clock, routed transport, Map-backed storage. Test substrate and reference implementation |
| [abr.js](abr.js) | The MCKP selector and per-object buffers |
| [link-throughput.js](link-throughput.js) | `LinkThroughputMeter` — byte/busy-time accounting across overlapping downloads |
| [bandwidth.js](bandwidth.js) | `BandwidthEstimator` + `computeBudget` — harmonic estimate and health-scaled spend budget |
| [bandwidth-estimator.js](bandwidth-estimator.js) | The causal harmonic-mean throughput estimate itself |
| [download-plan.js](download-plan.js) | What a segment needs, and how to judge the outcome |
| [metrics.js](metrics.js) | The run's metrics record and its builders |
| [payloads.js](payloads.js) | Server request bodies (wire formats read by `system/Server`) |
| [stalls.js](stalls.js) | Per-segment stall collection and stall-event folding |
| [stream-config.js](stream-config.js) | `resolveStreamConfig` — validated timing from `/api/config` |

## The seven capabilities

| Capability | Methods |
|---|---|
| `transport` | `getJson`, `postJson`, `assetUrl`, `fetchAsset` |
| `storage` | `handle`, `createScratch`, `release`, `writeText`, `writeResult`, `openAppendStream` |
| `viewpoints` | `list` |
| `clock` | `now`, `every`, `cancel`, `delay` |
| `logger` | `emit` |
| `renderer` | `start`, `stop`, `stageSegment`, `setPlaybackState`, `setCallbacks`, `latestCamera` (nullable) |
| `lifecycle` | `exit`, `onShutdownRequest` |

Nothing here is speculative: the set was derived by auditing every platform
touchpoint in the pre-refactor `client.js` (10 fetch sites, 9 distinct `fs`
calls, 5 `process` calls, 4 timer sites, the renderer child process, console
logging), and each method backs at least one real call site. The two
implementations are [`Client/node-platform.js`](../Client/node-platform.js) and
[`WebClient/src/browser-platform.js`](../WebClient/src/browser-platform.js).

Call `validatePlatform(platform, { requireRenderer })` once at startup. It
reports *every* missing method in one error, because the alternative is
`undefined is not a function` twelve segments into a run — which leaves a
truncated metrics file that looks like a legitimate result.

## Asset handles

The core never learns where a downloaded byte lives. `storage.handle(...)`
mints an **opaque** handle, `transport.fetchAsset` fills it,
`renderer.stageSegment` consumes it. In Node a handle is a filesystem path; in
the browser it is an OPFS path or a key into a buffer map. Treat handles as
values to pass along, never to parse. This is why
[download-plan.js](download-plan.js) takes `destinationFor` injected: it slots
assets by frame index without knowing what a destination is.

## Constraints a new platform must respect

1. **`fetchAsset` must not be cacheable** (`cache: 'no-store'`). The segment
   loop has to keep generating real network load or the shaped-bandwidth
   experiment stops meaning anything — the same reason
   `system/Client/decode_cache.py` caches decodes but never downloads.
2. **`lifecycle.exit` must not navigate away.** The final `POST /api/results`
   happens during shutdown. Stop timers, resolve the run promise, leave the
   page alive.
3. **`openAppendStream.write` must not block.** Render telemetry arrives at
   30 Hz; in Node a synchronous write per frame filled the renderer's stdout
   pipe and blocked the loop calling `poll_events()`, so client disk I/O was
   directly stalling the Open3D window. Buffer and flush. (OPFS
   `createSyncAccessHandle` is worker-only.)
4. **Keep the `renderer: null` path working.** Simulated mode credits segments
   straight from the download result; interactive mode waits for
   `object_ready`. They measure different things and both must survive.

## Testing against the fake platform

`createFakePlatform()` returns a contract-valid platform with a **virtual
clock**, so a 40-segment run costs a millisecond of test time instead of
80 seconds. `tests/test_streaming_client.js` drives complete runs this way.

```js
const { createFakePlatform } = require('../system/ClientCore/testing/fake-platform');

const platform = createFakePlatform();
platform.transport.onGet('/api/config', () => ({
    segmentDuration: 2, framesPerSegment: 60, segmentIntervalMs: 2000,
    totalSegments: 20, updateIntervalSegments: 1
}));
platform.transport.onGet('/api/manifest', () => menu);
platform.transport.failAsset('dancer_fr0003');   // one bad geometry frame

await platform.clock.advance(20 * 2000);         // 20 segments, instantly

assert.ok(platform.storage.result);              // the run wrote its metrics
assert.strictEqual(platform.clock.pending, 0);   // no timer leaked
```

Assert on observable state — `storage.files`, `transport.requests`,
`renderer.staged`, `logger.at('WARN')` — rather than on call expectations.

```bash
node --test tests/test_bandwidth_estimator.js tests/test_client_core.js \
  tests/test_client_core_parity.js tests/test_client_platform.js \
  tests/test_client_core_purity.js tests/test_streaming_client.js
```

`node --test <directory>` does not work in this environment; name the files.
`test_client_core_parity.js` runs the extracted download planner against the
pre-refactor implementation across 12 manifest shapes — keep it until the
end-to-end parity harness (same trace through both clients, compared with
`vstream/evaluation/QoE_full.py`) is running.
