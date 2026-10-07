from concurrent.futures import ThreadPoolExecutor
import itertools
import json
import socket
import threading
import time

import numpy as np
import pytest

import open4d
from open4d.core import Frame, PointCloud, Sequence, TriangleMesh
from open4d.demo import mesh_sequence
from open4d.io import write_sequence
from open4d.transport import StreamStats, _tcp, receive, send


TRIANGLE = TriangleMesh([[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]], [[0, 1, 2]])


def _frames(*timestamps):
    return [Frame(index, timestamp, TRIANGLE) for index, timestamp in enumerate(timestamps)]


def _wire(frame):
    fixed, header, arrays = _tcp._encode(frame, _tcp._DEFAULT_LIMIT)
    return fixed + header + b"".join(array.tobytes() for array in arrays)


def _message(header, payload=b"", *, header_size=None, payload_size=None):
    if not isinstance(header, bytes):
        header = json.dumps(header).encode()
    size = len(header) if header_size is None else header_size
    length = len(payload) if payload_size is None else payload_size
    return _tcp._HEADER.pack(_tcp._MAGIC, size, length) + header + payload


def _header(*arrays, **fields):
    return {"frame_index": 0, "timestamp": 0.0, "metadata": {},
            "arrays": [{"name": name, "dtype": dtype, "shape": shape}
                       for name, dtype, shape in arrays], **fields}


POSITIONS = ("positions", "<f8", [3, 3])
TRIANGLES = ("triangles", "<u4", [1, 3])
VALID_PAYLOAD = np.zeros((3, 3)).tobytes() + np.array([[0, 1, 2]], "<u4").tobytes()


def test_stream_preserves_frames_and_attributes():
    mesh = TriangleMesh(
        [[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]], [[0, 1, 2]],
        colors=np.full((3, 3), 0.5), normals=np.tile([0., 0., 1.], (3, 1)),
        texture_coordinates=np.array([[0., 0.], [1., 0.], [0., 1.]]),
        attributes={"confidence": np.array([0.1, 0.5, 0.9])},
    )
    expected = [Frame(8, 1.25, mesh, {"camera": "left", "tags": [1, "rgbd"]}),
                Frame(9, 1.5, mesh)]
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, expected, *receiver.address, realtime=False)
        actual = list(receiver)
        assert future.result(timeout=3) == 2
    assert [frame.frame_index for frame in actual] == [8, 9]
    assert [frame.timestamp for frame in actual] == [1.25, 1.5]
    assert actual[0].metadata == expected[0].metadata
    for name in ("positions", "triangles", "colors", "normals", "texture_coordinates"):
        np.testing.assert_array_equal(getattr(actual[0].geometry, name), getattr(mesh, name))
    np.testing.assert_array_equal(actual[0].geometry.attributes["confidence"],
                                  mesh.attributes["confidence"])


def test_stream_synthetic_sequence_and_empty_mesh():
    sample = mesh_sequence(side=4, frames=3)
    empty = Frame(3, 1, TriangleMesh(np.empty((0, 3)), np.empty((0, 3), dtype=np.uint32)))
    expected = [*sample, empty]
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, expected, *receiver.address, realtime=False)
        actual = list(receiver)
        assert future.result(timeout=3) == 4
    for original, decoded in zip(expected, actual):
        np.testing.assert_array_equal(original.geometry.positions, decoded.geometry.positions)
        np.testing.assert_array_equal(original.geometry.triangles, decoded.geometry.triangles)


def test_empty_stream_finishes():
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, [], *receiver.address)
        assert list(receiver) == []
        assert future.result(timeout=3) == 0


def test_receiver_closes_early_and_releases_port():
    with receive(port=0, timeout=3) as receiver:
        address = receiver.address
    with receive(*address, timeout=0.01) as second:
        with pytest.raises(TimeoutError):
            next(second)
    assert list(receiver) == []


@pytest.mark.parametrize("payload,error", [
    (_tcp._HEADER.pack(b"BAD!", 0, 0), ValueError),
    (_tcp._HEADER.pack(_tcp._MAGIC, 10, 1000), ValueError),
    (_tcp._HEADER.pack(_tcp._MAGIC, 10, 10) + b"{}", EOFError),
])
def test_receiver_rejects_invalid_or_truncated_messages(payload, error):
    with receive(port=0, timeout=3, max_frame_bytes=100) as receiver:
        with socket.create_connection(receiver.address, timeout=3) as sender:
            sender.sendall(payload)
        with pytest.raises(error):
            next(receiver)


def test_receiver_rejects_object_arrays():
    header = json.dumps({"frame_index": 0, "timestamp": 0, "metadata": {},
                         "arrays": [{"name": "positions", "dtype": "O", "shape": [1, 3]}]}).encode()
    with receive(port=0, timeout=3) as receiver:
        with socket.create_connection(receiver.address, timeout=3) as sender:
            sender.sendall(_tcp._HEADER.pack(_tcp._MAGIC, len(header), 0) + header)
        with pytest.raises(ValueError, match="invalid array"):
            next(receiver)


def test_sender_rejects_large_frames_without_hanging_receiver():
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, mesh_sequence(side=4, frames=1), *receiver.address,
                             max_frame_bytes=1)
        with pytest.raises(ValueError, match="max_frame_bytes"):
            future.result(timeout=3)
        with pytest.raises(EOFError):
            next(receiver)


def test_stream_stats_measure_frames_bytes_and_rate():
    expected = _frames(0, 0.05, 0.1)
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        assert receiver.stats == StreamStats()
        assert receiver.stats.fps is None and receiver.stats.bits_per_second is None
        future = pool.submit(send, expected, *receiver.address)
        next(receiver)
        assert receiver.stats.frames == 1 and receiver.stats.fps is None
        list(receiver)
        assert future.result(timeout=3) == 3
    stats = receiver.stats
    payload = sum(TRIANGLE.positions.nbytes + TRIANGLE.triangles.nbytes for _ in expected)
    assert stats.frames == 3
    assert stats.payload_bytes == payload
    assert stats.wire_bytes == sum(len(_wire(frame)) for frame in expected)
    assert 0.08 <= stats.elapsed < 2
    assert stats.fps == pytest.approx(2 / stats.elapsed)
    assert stats.bits_per_second == pytest.approx(stats.wire_bytes * 8 / stats.elapsed)
    with pytest.raises(AttributeError):
        stats.frames = 0


def test_record_saves_and_reloads_through_frame_folder(tmp_path):
    source = mesh_sequence(side=4, frames=3)
    expected = [Frame(frame.frame_index + 5, 0.25 + frame.timestamp, frame.geometry)
                for frame in source]
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, expected, *receiver.address, realtime=False)
        recorded = receiver.record()
        assert future.result(timeout=3) == 3
        assert next(receiver, None) is None
    assert isinstance(recorded, Sequence) and len(recorded) == 3
    folder = write_sequence(recorded, tmp_path / "frames", format="ply")
    with open4d.load(folder) as restored:
        assert restored.timestamps == tuple(frame.timestamp for frame in expected)
        for original, actual in zip(expected, restored):
            np.testing.assert_allclose(actual.geometry.positions, original.geometry.positions)
            np.testing.assert_array_equal(actual.geometry.triangles, original.geometry.triangles)


def test_record_max_frames_leaves_receiver_open():
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, _frames(0, 1, 2, 3, 4), *receiver.address, realtime=False)
        assert len(receiver.record(max_frames=0)) == 0
        assert receiver.record(max_frames=2).timestamps == (0, 1)
        assert [frame.timestamp for frame in receiver] == [2, 3, 4]
        assert future.result(timeout=3) == 5
        assert len(receiver.record(max_frames=2)) == 0


def test_record_duration_uses_stream_time_and_keeps_the_next_frame():
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, _frames(10, 10.1, 10.2, 10.3, 10.4), *receiver.address,
                             realtime=False)
        assert receiver.record(duration=0.25).timestamps == (10, 10.1, 10.2)
        assert next(receiver).timestamp == 10.3
        assert receiver.record(duration=5).timestamps == (10.4,)
        assert future.result(timeout=3) == 5
    assert receiver.stats.frames == 5


@pytest.mark.parametrize("options", [
    {"max_frames": -1}, {"max_frames": 1.0}, {"max_frames": True},
    {"duration": 0}, {"duration": -1}, {"duration": float("inf")}, {"duration": float("nan")},
    {"duration": "1"},
])
def test_record_rejects_invalid_limits(options):
    with receive(port=0, timeout=3) as receiver:
        with pytest.raises(ValueError):
            receiver.record(**options)


def test_close_from_another_thread_stops_blocked_accept():
    with receive(port=0, timeout=30) as receiver:
        threading.Timer(0.1, receiver.close).start()
        started = time.monotonic()
        with pytest.raises(StopIteration):
            next(receiver)
        assert time.monotonic() - started < 2


def test_close_from_another_thread_stops_blocked_recv_and_record():
    with receive(port=0, timeout=30) as receiver:
        with socket.create_connection(receiver.address, timeout=3) as sender:
            sender.sendall(_wire(_frames(0)[0]))
            threading.Timer(0.2, receiver.close).start()
            started = time.monotonic()
            recorded = receiver.record()
            assert time.monotonic() - started < 2
        assert recorded.timestamps == (0,)
        assert next(receiver, None) is None


def test_sender_raises_connection_error_when_receiver_closes():
    endless = (Frame(index, index, TRIANGLE) for index in itertools.count())
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, endless, *receiver.address, realtime=False, timeout=3)
        next(receiver)
        receiver.close()
        with pytest.raises(ConnectionError):
            future.result(timeout=5)


def test_receiver_accepts_one_sender_only():
    with receive(port=0, timeout=3) as receiver:
        with socket.create_connection(receiver.address, timeout=3) as sender:
            sender.sendall(_wire(_frames(0)[0]))
            assert next(receiver).timestamp == 0
            with pytest.raises(ConnectionRefusedError):
                socket.create_connection(receiver.address, timeout=3).close()


def test_disconnect_after_one_frame_raises_eof_after_delivering_it():
    with receive(port=0, timeout=3) as receiver:
        with socket.create_connection(receiver.address, timeout=3) as sender:
            sender.sendall(_wire(_frames(0.5)[0]))
        assert next(receiver).timestamp == 0.5
        with pytest.raises(EOFError):
            next(receiver)
        assert next(receiver, None) is None
    assert receiver.stats.frames == 1


VALID = _message(_header(POSITIONS, TRIANGLES), VALID_PAYLOAD)


@pytest.mark.parametrize("payload,error,match", [
    (VALID[:8], EOFError, "disconnected"),
    (VALID[:_tcp._HEADER.size + 5], EOFError, "disconnected"),
    (VALID[:-1], EOFError, "disconnected"),
    (_tcp._HEADER.pack(_tcp._MAGIC, 0, 8) + bytes(8), ValueError, "limits"),
    (_tcp._HEADER.pack(_tcp._MAGIC, _tcp._MAX_HEADER + 1, 0), ValueError, "limits"),
    (_tcp._HEADER.pack(_tcp._MAGIC, 10, 101), ValueError, "limits"),
    (_message(b"\xff\xfe{}"), ValueError, "invalid mesh stream frame"),
    (_message(b"{not json"), ValueError, "invalid mesh stream frame"),
    (_message(b"[" * 100_000), ValueError, "invalid mesh stream frame"),
    (_message(b"[]"), ValueError, "invalid mesh stream frame"),
    (_message({"arrays": []}), ValueError, "invalid mesh stream frame"),
    (_message(_header(arrays=None)), ValueError, "arrays must be a list"),
    (_message(_header(("unknown", "<f8", [1]))), ValueError, "invalid array"),
    (_message(_header(("positions", "<f8", [0, 3]), ("positions", "<f8", [0, 3]))),
     ValueError, "invalid array"),
    (_message(_header(("positions", "<f8", [3, 3])), bytes(64)), ValueError, "exceeds frame payload"),
    (_message(_header(POSITIONS, TRIANGLES), VALID_PAYLOAD + b"x"), ValueError, "unexpected bytes"),
    (_message(_header(("positions", "<f8", [-1, 3]))), ValueError, "invalid array"),
    (_message(_header(("positions", "<f8", [1.5, 3]))), ValueError, "invalid array"),
    (_message(_header(("positions", "<f8", [True, 3]))), ValueError, "invalid array"),
    (_message(_header(("positions", "<f8", "3"))), ValueError, "invalid array"),
    (_message(_header(("positions", "<f8", [0] * 9))), ValueError, "invalid array"),
    (_message(_header(("positions", "i4,,", [0, 3]))), ValueError, "invalid array"),
    (_message(_header(("positions", "<c16", [0, 3]))), ValueError, "invalid array"),
    (_message(_header(("positions", "<f8", [0, 2 ** 70]))), ValueError, "invalid mesh"),
    (_message(_header(POSITIONS, TRIANGLES, frame_index=-1), VALID_PAYLOAD),
     ValueError, "invalid mesh stream frame"),
    (_message(_header(POSITIONS, TRIANGLES, timestamp="0"), VALID_PAYLOAD),
     ValueError, "invalid mesh stream frame"),
    (_message(_header(POSITIONS, TRIANGLES, metadata=[]), VALID_PAYLOAD),
     ValueError, "invalid mesh stream frame"),
    (_message(b'{"frame_index":0,"timestamp":0,"metadata":{"x":NaN},"arrays":[]}'),
     ValueError, "NaN is not allowed"),
])
def test_receiver_rejects_malformed_o4s1_frames(payload, error, match):
    with receive(port=0, timeout=3, max_frame_bytes=100) as receiver:
        with socket.create_connection(receiver.address, timeout=3) as sender:
            sender.sendall(payload)
        with pytest.raises(error, match=match):
            next(receiver)
        assert next(receiver, None) is None


def test_receiver_rejects_decreasing_timestamps_from_raw_sender():
    with receive(port=0, timeout=3) as receiver:
        with socket.create_connection(receiver.address, timeout=3) as sender:
            sender.sendall(b"".join(_wire(frame) for frame in _frames(1, 0.5)))
            assert next(receiver).timestamp == 1
            with pytest.raises(ValueError, match="nondecreasing"):
                next(receiver)


def _nan_timestamp():
    frame = Frame(0, 0, TRIANGLE)
    object.__setattr__(frame, "timestamp", float("nan"))
    return frame


@pytest.mark.parametrize("frames,error,match,delivered", [
    (_frames(1, 0.5), ValueError, "nondecreasing", 1),
    ([TRIANGLE], TypeError, "Frames", 0),
    ([Frame(0, 0, PointCloud(np.zeros((2, 3))))], TypeError, "triangle mesh", 0),
    ([Frame(0, 0, TRIANGLE, {"value": float("nan")})], ValueError, "JSON compliant", 0),
    ([Frame(0, 0, TRIANGLE, {"value": float("inf")})], ValueError, "JSON compliant", 0),
    ([_nan_timestamp()], ValueError, "JSON compliant", 0),
])
def test_sender_rejects_invalid_frames(frames, error, match, delivered):
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, frames, *receiver.address, realtime=False)
        with pytest.raises(error, match=match):
            future.result(timeout=3)
        received = []
        with pytest.raises(EOFError):
            received.extend(receiver)
    assert len(received) == delivered


def test_receiver_after_stream_end_keeps_stopping():
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        future = pool.submit(send, _frames(0), *receiver.address, realtime=False)
        assert len(list(receiver)) == 1
        assert future.result(timeout=3) == 1
        for _ in range(3):
            with pytest.raises(StopIteration):
                next(receiver)
        assert len(receiver.record()) == 0


def test_receiver_times_out_when_sender_stalls_mid_frame():
    with receive(port=0, timeout=0.2) as receiver:
        with socket.create_connection(receiver.address, timeout=3) as sender:
            sender.sendall(VALID[:-4])
            started = time.monotonic()
            with pytest.raises(TimeoutError):
                next(receiver)
            assert time.monotonic() - started < 2


def test_send_to_closed_port_is_refused():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    with pytest.raises(ConnectionRefusedError):
        send(_frames(0), "127.0.0.1", port, timeout=2)


def test_realtime_send_preserves_timestamp_spacing():
    with receive(port=0, timeout=3) as receiver, ThreadPoolExecutor() as pool:
        started = time.monotonic()
        future = pool.submit(send, _frames(5, 5.1, 5.2), *receiver.address)
        assert len(list(receiver)) == 3
        assert future.result(timeout=3) == 3
        assert time.monotonic() - started >= 0.18
