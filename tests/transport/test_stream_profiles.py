from concurrent.futures import ThreadPoolExecutor

import numpy as np
import pytest

from open4d.codecs import StreamEncoder, StreamDecoder, stream_profiles
from open4d.core import Frame, TriangleMesh, PointCloud, GaussianCloud
from open4d.transport import LiveSender, LatestFrameQueue, receive, send


def frames(kind, count=6):
    rng = np.random.default_rng(17)
    result = []
    for i in range(count):
        n = 6 + 3 * i
        positions = rng.random((n, 3), dtype=np.float32)
        colors = rng.random((n, 3), dtype=np.float32)
        attributes = {"confidence": rng.random(n, dtype=np.float32)}
        if kind == "mesh":
            geometry = TriangleMesh(positions, np.arange(n).reshape(-1, 3),
                                    colors=colors, attributes=attributes)
        elif kind == "points":
            geometry = PointCloud(positions, colors=colors, attributes=attributes)
        else:
            geometry = GaussianCloud(positions, np.full((n, 3), .03),
                                     np.tile([1., 0, 0, 0], (n, 1)),
                                     np.full(n, .7), colors=colors, attributes=attributes)
        result.append(Frame(i, i / 30, geometry, {"source": "capture", "i": i}))
    return result


@pytest.mark.parametrize("kind", ["mesh", "points", "gaussians"])
@pytest.mark.parametrize("profile", ["raw", "lossless", "quantized", "draco"])
def test_incremental_codec_and_tcp_roundtrip(kind, profile):
    if profile == "draco" and kind == "gaussians":
        pytest.skip("Draco represents meshes and points")
    if not stream_profiles()[profile]["available"]:
        pytest.skip(f"{profile} unavailable")
    expected = frames(kind)
    options = {"preserve_order": True} if profile == "draco" else {}
    encoder, decoder = StreamEncoder(profile, **options), StreamDecoder()
    wire = b"".join(encoder.push(frame) for frame in expected) + encoder.flush()
    actual = []
    for offset in range(0, len(wire), 37):
        actual.extend(decoder.feed(wire[offset:offset + 37]))
    decoder.flush()
    with receive(port=0, timeout=5) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, expected, *receiver.address, realtime=False, profile=profile,
                             profile_options=options)
        network = list(receiver)
        assert future.result(timeout=5) == len(expected)
        assert receiver.stats.frames == len(expected)
    for decoded in (actual, network):
        assert len(decoded) == len(expected)
        for a, b in zip(expected, decoded):
            assert a.metadata == b.metadata
            assert a.timestamp == b.timestamp
            assert a.geometry.representation == b.geometry.representation
            tolerance = 2e-3 if profile in ("draco", "quantized") else 0
            np.testing.assert_allclose(a.geometry.positions, b.geometry.positions, atol=tolerance, rtol=0)
            np.testing.assert_allclose(a.geometry.colors, b.geometry.colors, atol=tolerance, rtol=0)
            np.testing.assert_allclose(a.geometry.attributes["confidence"],
                                       b.geometry.attributes["confidence"], atol=tolerance, rtol=0)
            if kind == "gaussians":
                np.testing.assert_allclose(a.geometry.scales, b.geometry.scales, atol=tolerance, rtol=0)
                np.testing.assert_allclose(a.geometry.opacities, b.geometry.opacities, atol=tolerance, rtol=0)


def test_delta_stream_detects_lost_dependency_and_recovers_with_keyframe():
    encoder = StreamEncoder("zlib", keyframe_interval=30)
    packets = [encoder.encode(f) for f in frames("points", 4)]
    decoder = StreamDecoder()
    decoder.feed(encoder.header_packet)
    decoder.feed(packets[0].data)
    with pytest.raises(ValueError, match="depends on packet"):
        decoder.feed(packets[2].data)
    encoder.request_keyframe()
    key = encoder.encode(frames("points", 6)[4])
    fresh = StreamDecoder()
    assert len(fresh.feed(encoder.header_packet + key.data)) == 1


def test_bounded_latest_queue_and_live_sender_finish():
    queue = LatestFrameQueue(1)
    queue.put("old")
    assert queue.put("new") == "old"
    queue.close()
    assert queue.get() == "new"
    with pytest.raises(EOFError):
        queue.get()
    expected = frames("gaussians", 30)
    with receive(port=0, timeout=5) as receiver, ThreadPoolExecutor() as pool:
        def produce():
            with LiveSender(*receiver.address, profile="lossless", latest=True) as sender:
                for frame in expected:
                    sender.submit(frame)
            return sender.stats
        future = pool.submit(produce)
        actual = list(receiver)
        stats = future.result(timeout=5)
    assert actual[-1].frame_index == 29
    assert stats.sent + stats.dropped == stats.submitted == 30
    assert stats.encode.count == stats.encoded


def test_decode_enforces_size_before_allocating():
    encoder = StreamEncoder("raw")
    wire = encoder.push(frames("points", 1)[0])
    with pytest.raises(ValueError, match="max_frame_bytes"):
        StreamDecoder(max_frame_bytes=8).feed(wire)
