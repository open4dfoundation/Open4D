from concurrent.futures import ThreadPoolExecutor
import json
import socket

import numpy as np
import pytest

from open4d.core import Frame, TriangleMesh
from open4d.demo import mesh_sequence
from open4d.transport import _tcp, receive, send


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
