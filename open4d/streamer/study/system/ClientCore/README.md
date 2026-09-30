# ClientCore

Streaming logic with no platform code, shared by the browser client
(`system/WebClient`) and the research repo's Node client.

**Rule:** nothing here imports `fs`, `http`, `https`, `path`, `os` or
`process`, or calls `console`, `Date.now`, `setInterval` or `fetch`
directly. All of that goes through the `ClientPlatform` contract in
[platform.js](platform.js), and `tests/test_client_core_purity.js` enforces
the rule. That way, a difference between the two clients reflects the system
under test, not the client code.

## Files

| File | Role |
|---|---|
| [streaming-client.js](streaming-client.js) | `StreamingClient`: segment loop, playback clock, downloads, reporting |
| [platform.js](platform.js) | the contract and `validatePlatform` |
| [abr.js](abr.js) | MCKP rung selection and per-object buffers |
| [bandwidth.js](bandwidth.js), [bandwidth-estimator.js](bandwidth-estimator.js), [link-throughput.js](link-throughput.js) | throughput estimate and spend budget |
| [download-plan.js](download-plan.js) | what a segment needs, and whether it arrived |
| [metrics.js](metrics.js), [stalls.js](stalls.js), [payloads.js](payloads.js) | run metrics, stall tracking, server request bodies |
| [stream-config.js](stream-config.js) | validated timing from `/api/config` |
| [testing/fake-platform.js](testing/fake-platform.js) | in-memory platform with a virtual clock, for tests |

## The platform contract

| Capability | Methods |
|---|---|
| `transport` | `getJson`, `postJson`, `assetUrl`, `fetchAsset` |
| `storage` | `handle`, `createScratch`, `release`, `writeText`, `writeResult`, `openAppendStream` |
| `viewpoints` | `list` |
| `clock` | `now`, `every`, `cancel`, `delay` |
| `logger` | `emit` |
| `renderer` | `start`, `stop`, `stageSegment`, `setPlaybackState`, `setCallbacks`, `latestCamera`; may be `null` |
| `lifecycle` | `exit`, `onShutdownRequest` |

Call `validatePlatform(platform, { requireRenderer })` at startup. It lists
every missing method at once. Storage handles are opaque, so pass them along
and never parse them.

A new platform must:

1. make `fetchAsset` uncacheable (`cache: 'no-store'`);
2. never navigate away in `lifecycle.exit`, because results are POSTed during
   shutdown;
3. never block in `openAppendStream.write` (telemetry arrives at 30 Hz, so
   buffer it);
4. keep `renderer: null` working, since simulated mode depends on it.

## Testing

`createFakePlatform()` has a virtual clock, so a 40-segment run takes about a
millisecond.

```js
const { createFakePlatform } = require('../system/ClientCore/testing/fake-platform');
const platform = createFakePlatform();
platform.transport.onGet('/api/config', () => ({ segmentDuration: 2, framesPerSegment: 60,
    segmentIntervalMs: 2000, totalSegments: 20, updateIntervalSegments: 1 }));
platform.transport.onGet('/api/manifest', () => menu);
await platform.clock.advance(20 * 2000);
assert.ok(platform.storage.result);
```

Assert on state (`storage.files`, `transport.requests`, `logger.at('WARN')`),
not on call expectations.

```bash
node --test tests/test_client_core*.js tests/test_bandwidth_estimator.js \
  tests/test_client_platform.js tests/test_streaming_client.js
```
