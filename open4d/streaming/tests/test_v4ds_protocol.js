'use strict';

/**
 * V4DS protocol tests, against golden bytes from the Python encoder.
 *
 * The fixtures in `fixtures_v4ds_golden.json` were produced by
 * `fixtures_v4ds_golden.gen.py`, which imports
 * `baselines/DeltaStream/orbitstream/protocol.py` and encodes real messages.
 * That file is the authority; this suite checks the JS port against it rather
 * than against itself. Regenerate with:
 *
 *   <env-python> tests/fixtures_v4ds_golden.gen.py > tests/fixtures_v4ds_golden.json
 *
 * Why golden bytes and not hand-written assertions: a byte-order or field-order
 * mistake in a binary protocol does not throw. It yields plausible numbers —
 * point counts in the millions, cameras at absurd positions — and the first
 * symptom is a renderer that draws nothing or draws nonsense. Comparing whole
 * encoded messages catches that immediately.
 *
 * Run: node --test tests/test_v4ds_protocol.js
 */

const assert = require('assert');
const test = require('node:test');
const fs = require('fs');
const path = require('path');

const {
    decodeConnection, decodeFrame, encodeFeedback, messageType,
    MessageType, ProtocolError, MAGIC
} = require('../system/WebClient/src/v4ds-protocol');

const GOLDEN = JSON.parse(fs.readFileSync(
    path.join(__dirname, 'fixtures_v4ds_golden.json'), 'utf8'));

const hexToBuffer = hex => {
    const bytes = Uint8Array.from(
        hex.match(/../g).map(pair => parseInt(pair, 16)));
    return bytes.buffer;
};
const toHex = bytes => Buffer.from(bytes).toString('hex');

// --------------------------------------------------------------------------
// Common header
// --------------------------------------------------------------------------

test('the golden fixtures are present and non-trivial', () => {
    for (const key of ['connection', 'connection_vivo_tiles', 'frame_delta',
                       'frame_keyframe', 'feedback_minimal',
                       'feedback_extended', 'feedback_full']) {
        assert.ok(GOLDEN[key], `missing fixture: ${key}`);
        assert.ok(GOLDEN[key].length > 16, `fixture too short: ${key}`);
    }
});

test('every message starts with the V4DS magic and version 1', () => {
    for (const [name, hex] of Object.entries(GOLDEN)) {
        const buffer = hexToBuffer(hex);
        const view = new DataView(buffer);
        assert.strictEqual(view.getUint32(0, false), MAGIC, `${name} magic`);
        assert.strictEqual(view.getUint16(4, false), 1, `${name} version`);
    }
});

test('messageType identifies each fixture without consuming it', () => {
    assert.strictEqual(messageType(hexToBuffer(GOLDEN.connection)),
        MessageType.CONNECTION);
    assert.strictEqual(messageType(hexToBuffer(GOLDEN.frame_delta)),
        MessageType.FRAME);
    assert.strictEqual(messageType(hexToBuffer(GOLDEN.feedback_minimal)),
        MessageType.FEEDBACK);
});

test('a corrupt header is rejected rather than misread', () => {
    const bad = new Uint8Array(hexToBuffer(GOLDEN.connection));
    bad[0] = 0x00;
    assert.throws(() => decodeConnection(bad.buffer), /bad magic/);

    const wrongVersion = new Uint8Array(hexToBuffer(GOLDEN.connection));
    wrongVersion[5] = 9;
    assert.throws(() => decodeConnection(wrongVersion.buffer),
        /unsupported protocol version/);

    const wrongType = new Uint8Array(hexToBuffer(GOLDEN.frame_delta));
    assert.throws(() => decodeConnection(wrongType.buffer),
        /expected message type/);

    assert.throws(() => messageType(new ArrayBuffer(4)),
        /shorter than the common header/);
});

// --------------------------------------------------------------------------
// CONNECTION
// --------------------------------------------------------------------------

test('decodeConnection recovers every field the Python encoder wrote', () => {
    const header = decodeConnection(hexToBuffer(GOLDEN.connection));
    assert.strictEqual(header.mode, 'metastream');
    assert.strictEqual(header.width, 640);
    assert.strictEqual(header.height, 480);
    assert.strictEqual(header.fpsNum, 30000);
    assert.strictEqual(header.fpsDen, 1000);
    assert.strictEqual(header.fps, 30);
    assert.strictEqual(header.blockSize, 16);
    assert.strictEqual(header.calibrationHash, 'abc123');
    assert.strictEqual(header.objects.length, 1);

    const [object] = header.objects;
    assert.strictEqual(object.objectId, 7);
    assert.strictEqual(object.name, 'dancer');
    assert.strictEqual(object.loopFrames, 300);
    assert.strictEqual(object.cameras.length, 1);

    const [camera] = object.cameras;
    assert.strictEqual(camera.cameraId, 2);
    assert.strictEqual(camera.fx, 525);
    assert.strictEqual(camera.fy, 524.5);
    assert.strictEqual(camera.cx, 319.5);
    assert.strictEqual(camera.cy, 239.5);
});

test('the camera matrix is row-major, with translation in the last column', () => {
    // The Python encoder writes `for row in camera_to_world for v in row`, so
    // the fixture's camera_to_world ((1,0,0,0.5),(0,1,0,1.5),(0,0,1,-2.25),...)
    // must come back with 0.5 at index 3, not index 12. Handing this array
    // straight to THREE.Matrix4.fromArray (which is column-major) would
    // silently transpose every camera.
    const header = decodeConnection(hexToBuffer(GOLDEN.connection));
    const m = header.objects[0].cameras[0].cameraToWorldRowMajor;
    assert.strictEqual(m.length, 16);
    assert.deepStrictEqual(m.slice(0, 4), [1, 0, 0, 0.5]);
    assert.deepStrictEqual(m.slice(4, 8), [0, 1, 0, 1.5]);
    assert.deepStrictEqual(m.slice(8, 12), [0, 0, 1, -2.25]);
    assert.deepStrictEqual(m.slice(12, 16), [0, 0, 0, 1]);
});

test('a ViVo header carries its tile/ABR catalogue', () => {
    const header = decodeConnection(hexToBuffer(GOLDEN.connection_vivo_tiles));
    assert.strictEqual(header.mode, 'vivo');
    assert.ok(header.tileAbr, 'tile catalogue present');
    assert.strictEqual(header.tileAbr.grid, 4);
    assert.deepStrictEqual(header.tileAbr.representationRatios, [1, 0.5, 0.25]);
    assert.strictEqual(header.tileAbr.objects.length, 1);

    const [object] = header.tileAbr.objects;
    assert.strictEqual(object.objectId, 7);
    assert.deepStrictEqual(object.boundsMin, [-1, 0, -1]);
    assert.deepStrictEqual(object.boundsMax, [1, 2, 1]);
    assert.strictEqual(object.tiles.length, 1);
    assert.strictEqual(object.tiles[0].tileId, 0);
    assert.strictEqual(object.tiles[0].pointCount, 1000);
    assert.deepStrictEqual(object.tiles[0].representationBytes,
        [9000, 4500, 2250]);
});

test('a header without a tile catalogue reports null, not an empty one', () => {
    // The distinction matters: MetaStream has no tiles at all, whereas a ViVo
    // catalogue with zero objects would be a broken prepare step.
    const header = decodeConnection(hexToBuffer(GOLDEN.connection));
    assert.strictEqual(header.tileAbr, null);
    assert.strictEqual(header.livoMaxDepthMm, null);
});

// --------------------------------------------------------------------------
// FRAME
// --------------------------------------------------------------------------

test('decodeFrame recovers a DeltaStream residual frame exactly', () => {
    const frame = decodeFrame(hexToBuffer(GOLDEN.frame_delta));
    assert.strictEqual(frame.frameType, 'delta');
    assert.strictEqual(frame.frameId, 42);
    assert.strictEqual(frame.sourceTimestampNs, 1234567890123n);
    assert.strictEqual(frame.encodeFinishedNs, 1234567890999n);
    assert.strictEqual(frame.records.length, 1);

    const [record] = frame.records;
    assert.strictEqual(record.objectId, 7);
    assert.strictEqual(record.cameraId, 2);
    assert.strictEqual(record.pointCount, 20677);
    assert.deepStrictEqual(Array.from(record.draco),
        Array.from({ length: 16 }, (_, i) => i), 'draco payload byte-exact');
    assert.deepStrictEqual(Array.from(record.removalBlocks), [3, 9]);
    assert.strictEqual(record.motions.length, 1);
    assert.deepStrictEqual(record.motions[0], {
        blockIndex: 5, deltaX: 0.25, deltaY: -0.5, deltaZ: 1.5,
        sourceX: -3, sourceY: 4
    });
});

test('nanosecond timestamps are exact BigInts, not lossy Numbers', () => {
    // Epoch nanoseconds are ~1.8e18, two hundred times MAX_SAFE_INTEGER. A
    // decoder that assumed they fit in a Number threw on every real frame; one
    // that silently coerced them would corrupt every latency measurement.
    const frame = decodeFrame(hexToBuffer(GOLDEN.frame_delta));
    assert.strictEqual(typeof frame.sourceTimestampNs, 'bigint');
    assert.strictEqual(frame.sourceTimestampNs, 1234567890123n);
    assert.strictEqual(frame.sourceTimestampMs, 1234567);
});

test('an epoch-nanosecond timestamp decodes exactly', () => {
    // The value that exposed the bug: Date.now() * 1e6 on a real stream.
    const epochNs = 1789137968228000000n;
    const bytes = new Uint8Array(hexToBuffer(GOLDEN.frame_delta));
    // source_timestamp_ns is the second u64: 8 common + 1 type + 8 frameId = 17
    new DataView(bytes.buffer).setBigUint64(17, epochNs, false);
    const frame = decodeFrame(bytes.buffer);
    assert.strictEqual(frame.sourceTimestampNs, epochNs,
        'no precision lost at 1.8e18');
    assert.strictEqual(frame.sourceTimestampMs, 1789137968228);
});

test('an absurd frame id is still refused', () => {
    // Frame ids are sequence numbers; one above 2^53 means a corrupt stream, so
    // unlike a timestamp it should fail loudly rather than be accepted.
    const bytes = new Uint8Array(hexToBuffer(GOLDEN.frame_delta));
    new DataView(bytes.buffer).setBigUint64(9, 2n ** 60n, false);
    assert.throws(() => decodeFrame(bytes.buffer),
        /frameId .* exceeds the exact Number range/);
});

test('a MetaStream keyframe with several objects decodes in order', () => {
    const frame = decodeFrame(hexToBuffer(GOLDEN.frame_keyframe));
    assert.strictEqual(frame.frameType, 'keyframe');
    assert.strictEqual(frame.records.length, 2);
    assert.deepStrictEqual(frame.records.map(r => r.objectId), [7, 8]);
    assert.deepStrictEqual(Array.from(frame.records[0].draco),
        [0xaa, 0xbb, 0xcc]);
    assert.deepStrictEqual(Array.from(frame.records[1].draco), [0xde, 0xad]);
    assert.strictEqual(frame.records[0].removalBlocks.length, 0,
        'a keyframe carries no residual records');
    assert.strictEqual(frame.records[0].motions.length, 0);
});

test('a truncated frame is rejected rather than half-decoded', () => {
    const full = new Uint8Array(hexToBuffer(GOLDEN.frame_delta));
    const cut = full.slice(0, full.length - 8);
    assert.throws(() => decodeFrame(cut.buffer), /truncated message/);
});

test('trailing bytes are rejected', () => {
    // A framing error in the bridge would show up as extra bytes; failing loudly
    // beats decoding the first message and silently dropping the rest.
    const full = new Uint8Array(hexToBuffer(GOLDEN.frame_delta));
    const padded = new Uint8Array(full.length + 3);
    padded.set(full);
    assert.throws(() => decodeFrame(padded.buffer), /trailing bytes/);
});

test('an absurd record count is refused before allocating', () => {
    const frame = new Uint8Array(hexToBuffer(GOLDEN.frame_delta));
    // record count is the u16 after type(1) + 3 x u64, at offset 8+1+24 = 33
    frame[33] = 0xff;
    frame[34] = 0xff;
    assert.throws(() => decodeFrame(frame.buffer, { maxRecords: 4096 }),
        /too many frame records/);
});

// --------------------------------------------------------------------------
// FEEDBACK — encoded here, so compare bytes with Python
// --------------------------------------------------------------------------

test('minimal feedback encodes byte-identically to Python', () => {
    const bytes = encodeFeedback({
        displayedFrameId: 41, measuredFps: 29.5, repeatedFrames: 2
    });
    assert.strictEqual(toHex(bytes), GOLDEN.feedback_minimal);
});

test('extended feedback encodes byte-identically to Python', () => {
    const bytes = encodeFeedback({
        displayedFrameId: 41, measuredFps: 29.5, repeatedFrames: 2,
        viewPosition: [1.0, 1.6, 4.0], viewForward: [0.0, 0.0, -1.0],
        bandwidthMbps: 250.5
    });
    assert.strictEqual(toHex(bytes), GOLDEN.feedback_extended);
});

test('full feedback with frustum and tile selections matches Python', () => {
    const bytes = encodeFeedback({
        displayedFrameId: 41, measuredFps: 29.5, repeatedFrames: 2,
        viewPosition: [1.0, 1.6, 4.0], viewForward: [0.0, 0.0, -1.0],
        bandwidthMbps: 250.5, viewUp: [0.0, 1.0, 0.0],
        verticalFovDegrees: 60.0, viewAspect: 1.7777, viewNear: 0.05,
        viewFar: 200.0,
        selections: [{ objectId: 7, tileId: 13, representationId: 2 }],
        selectionsPresent: true
    });
    assert.strictEqual(toHex(bytes), GOLDEN.feedback_full);
});

test('the frustum flag is set only when a frustum is present', () => {
    const withFrustum = encodeFeedback({
        displayedFrameId: 1, measuredFps: 30,
        viewPosition: [0, 0, 0], viewForward: [0, 0, -1], bandwidthMbps: 10,
        viewUp: [0, 1, 0], verticalFovDegrees: 60, viewAspect: 1,
        viewNear: 0.1, viewFar: 100
    });
    const withoutFrustum = encodeFeedback({
        displayedFrameId: 1, measuredFps: 30,
        viewPosition: [0, 0, 0], viewForward: [0, 0, -1], bandwidthMbps: 10
    });
    assert.strictEqual(withFrustum[7], 1, 'flag bit 0 set');
    assert.strictEqual(withoutFrustum[7], 0, 'flag clear');
});

test('partial tiers are refused, matching the Python encoder', () => {
    // The Python side raises on these; accepting them here would produce a
    // message the server rejects mid-run, after the handshake succeeded.
    assert.throws(() => encodeFeedback({
        displayedFrameId: 1, measuredFps: 30, viewPosition: [0, 0, 0]
    }), /must be supplied together/);

    assert.throws(() => encodeFeedback({
        displayedFrameId: 1, measuredFps: 30,
        viewPosition: [0, 0, 0], viewForward: [0, 0, -1], bandwidthMbps: 10,
        viewUp: [0, 1, 0]
    }), /complete viewer frustum/);

    assert.throws(() => encodeFeedback({
        displayedFrameId: 1, measuredFps: 30, viewUp: [0, 1, 0],
        verticalFovDegrees: 60, viewAspect: 1, viewNear: 0.1, viewFar: 100
    }), /frustum requires extended/);

    assert.throws(() => encodeFeedback({
        displayedFrameId: 1, measuredFps: 30, selectionsPresent: true
    }), /selections require extended/);
});

test('an empty-but-present selection list is distinguishable from absent', () => {
    // "No visible tiles" must not read as "no decision yet", or the server
    // keeps an old selection active indefinitely.
    const base = {
        displayedFrameId: 1, measuredFps: 30,
        viewPosition: [0, 0, 0], viewForward: [0, 0, -1], bandwidthMbps: 10
    };
    const absent = encodeFeedback(base);
    const presentEmpty = encodeFeedback({ ...base, selectionsPresent: true });
    assert.strictEqual(presentEmpty.length, absent.length + 2,
        'a u16 count of zero is still written');
});

test('feedback round-trips through the Python decoder', () => {
    // Encoding matching bytes proves the layout; this proves the Python side
    // actually accepts what we produce.
    const { execFileSync } = require('child_process');
    const bytes = encodeFeedback({
        displayedFrameId: 4242, measuredFps: 28.25, repeatedFrames: 3,
        viewPosition: [1.5, 2.5, -3.5], viewForward: [0, 0, -1],
        bandwidthMbps: 175.75, viewUp: [0, 1, 0], verticalFovDegrees: 55,
        viewAspect: 1.5, viewNear: 0.01, viewFar: 150,
        selections: [{ objectId: 3, tileId: 9, representationId: 1 }],
        selectionsPresent: true
    });
    const python = process.env.VS4D_TEST_PYTHON
        || '/home/ryan/miniconda3/envs/open4d/bin/python';
    if (!fs.existsSync(python)) {
        console.log('skipping Python round-trip: no interpreter at', python);
        return;
    }
    const script = `
import json, sys
sys.path.insert(0, ${JSON.stringify(path.join(__dirname, '..'))})
from baselines.DeltaStream.orbitstream.protocol import decode_feedback
fb = decode_feedback(bytes.fromhex(sys.argv[1]))
print(json.dumps({
 "displayed": fb.displayed_frame_id, "fps": round(fb.measured_fps, 3),
 "repeats": fb.repeated_frames, "pos": [round(v,3) for v in fb.view_position],
 "fov": round(fb.vertical_fov_degrees, 3),
 "selections": [[s.object_id, s.tile_id, s.representation_id] for s in fb.selections],
 "selections_present": fb.selections_present}))
`;
    const out = execFileSync(python, ['-c', script, toHex(bytes)],
        { encoding: 'utf8' });
    const decoded = JSON.parse(out);
    assert.strictEqual(decoded.displayed, 4242);
    assert.strictEqual(decoded.fps, 28.25);
    assert.strictEqual(decoded.repeats, 3);
    assert.deepStrictEqual(decoded.pos, [1.5, 2.5, -3.5]);
    assert.strictEqual(decoded.fov, 55);
    assert.deepStrictEqual(decoded.selections, [[3, 9, 1]]);
    assert.strictEqual(decoded.selections_present, true);
});
