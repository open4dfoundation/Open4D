"""Transfer decoded mesh frames over TCP using JSON and NumPy array bytes."""

from __future__ import annotations

import json
import math
import socket
import struct
import time

import numpy as np

from ..core import Frame, TriangleMesh


_HEADER = struct.Struct("!4sIQ")
_MAGIC = b"O4S1"
_MAX_HEADER = 1024 * 1024
_DEFAULT_LIMIT = 64 * 1024 * 1024
_GEOMETRY_FIELDS = ("positions", "triangles", "colors", "normals", "texture_coordinates")


def _limit(value):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError("max_frame_bytes must be a positive integer")
    return value


def _read(sock, size):
    data = bytearray()
    while len(data) < size:
        part = sock.recv(min(size - len(data), 1024 * 1024))
        if not part:
            raise EOFError("sender disconnected before the stream finished")
        data.extend(part)
    return data


def _encode(frame, limit):
    if not isinstance(frame, Frame):
        raise TypeError("stream items must be Open4D Frames")
    if not isinstance(frame.geometry, TriangleMesh):
        raise TypeError("streaming currently supports triangle mesh frames")
    geometry = frame.geometry
    values = {name: getattr(geometry, name) for name in _GEOMETRY_FIELDS
              if getattr(geometry, name) is not None}
    values.update({f"attribute:{name}": value for name, value in geometry.attributes.items()})
    length = sum(value.nbytes for value in values.values())
    if length > limit:
        raise ValueError("frame exceeds max_frame_bytes")
    arrays = [np.ascontiguousarray(value) for value in values.values()]
    if any(array.dtype.kind not in "fiub" or array.dtype.itemsize > 8 for array in arrays):
        raise ValueError("stream arrays must contain real numbers, integers or booleans")
    header = json.dumps({
        "frame_index": frame.frame_index,
        "timestamp": frame.timestamp,
        "metadata": dict(frame.metadata),
        "arrays": [{"name": name, "dtype": array.dtype.str, "shape": array.shape}
                   for name, array in zip(values, arrays)],
    }, allow_nan=False, separators=(",", ":")).encode("utf-8")
    if len(header) > _MAX_HEADER:
        raise ValueError("frame metadata exceeds 1 MiB")
    return _HEADER.pack(_MAGIC, len(header), length), header, arrays


def _decode(header, payload):
    try:
        info = json.loads(header)
        descriptions = info["arrays"]
        if not isinstance(descriptions, list):
            raise ValueError("arrays must be a list")
        arrays = {}
        offset = 0
        for description in descriptions:
            name = description["name"]
            dtype = np.dtype(description["dtype"])
            shape = description["shape"]
            if (not isinstance(name, str) or name in arrays
                    or (name not in _GEOMETRY_FIELDS and not name.startswith("attribute:"))
                    or dtype.kind not in "fiub" or dtype.itemsize > 8
                    or not isinstance(shape, list) or not 1 <= len(shape) <= 8
                    or any(type(size) is not int or size < 0 for size in shape)):
                raise ValueError("invalid array description")
            size = math.prod(shape) * dtype.itemsize
            if size > len(payload) - offset:
                raise ValueError("array exceeds frame payload")
            arrays[name] = np.frombuffer(payload, dtype=dtype, count=math.prod(shape),
                                        offset=offset).reshape(shape)
            offset += size
        if offset != len(payload):
            raise ValueError("unexpected bytes after frame arrays")
        attributes = {name.removeprefix("attribute:"): value
                      for name, value in arrays.items() if name.startswith("attribute:")}
        fields = {name: value for name, value in arrays.items() if name in _GEOMETRY_FIELDS}
        return Frame(info["frame_index"], info["timestamp"],
                     TriangleMesh(**fields, attributes=attributes), info["metadata"])
    except (KeyError, TypeError, ValueError, OverflowError) as error:
        raise ValueError(f"invalid mesh stream frame: {error}") from error


def send(frames, host="127.0.0.1", port=47004, *, realtime=True, timeout=30.0,
         max_frame_bytes=_DEFAULT_LIMIT) -> int:
    """Send a Sequence or iterable of Frames to a running receiver.

    By default, preserve the spacing between timestamps. Set realtime=False
    to transfer as fast as possible. Returns the number of frames sent.
    This transfers decoded arrays and frame metadata; it does not compress them.
    """
    limit = _limit(max_frame_bytes)
    count = 0
    first_timestamp = previous_timestamp = None
    with socket.create_connection((host, port), timeout=timeout) as sock:
        started = time.monotonic()
        for frame in frames:
            fixed, header, arrays = _encode(frame, limit)
            if previous_timestamp is not None and frame.timestamp < previous_timestamp:
                raise ValueError("stream timestamps must be nondecreasing")
            if first_timestamp is None:
                first_timestamp = frame.timestamp
                started = time.monotonic()
            if realtime:
                remaining = frame.timestamp - first_timestamp - (time.monotonic() - started)
                if remaining > 0:
                    time.sleep(remaining)
            sock.sendall(fixed)
            sock.sendall(header)
            for array in arrays:
                if array.nbytes:
                    sock.sendall(memoryview(array).cast("B"))
            previous_timestamp = frame.timestamp
            count += 1
        sock.sendall(_HEADER.pack(_MAGIC, 0, 0))
    return count


class Receiver:
    """One incoming stream. Use as a context manager to close it on early exit."""

    def __init__(self, host, port, timeout, max_frame_bytes):
        self._limit = _limit(max_frame_bytes)
        self._connection = None
        self._previous_timestamp = None
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._listener.settimeout(timeout)
            self._listener.bind((host, port))
            self._listener.listen(1)
            self.address = self._listener.getsockname()
        except BaseException:
            self.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()

    def __iter__(self):
        return self

    def __next__(self):
        if self._listener is None:
            raise StopIteration
        try:
            if self._connection is None:
                self._connection, _ = self._listener.accept()
                self._connection.settimeout(self._listener.gettimeout())
            magic, header_size, payload_size = _HEADER.unpack(_read(self._connection, _HEADER.size))
            if magic != _MAGIC:
                raise ValueError("unrecognized mesh stream version")
            if header_size == 0 and payload_size == 0:
                self.close()
                raise StopIteration
            if not 0 < header_size <= _MAX_HEADER or payload_size > self._limit:
                raise ValueError("stream frame exceeds header or max_frame_bytes limits")
            frame = _decode(_read(self._connection, header_size),
                            _read(self._connection, payload_size))
            if (self._previous_timestamp is not None
                    and frame.timestamp < self._previous_timestamp):
                raise ValueError("stream timestamps must be nondecreasing")
            self._previous_timestamp = frame.timestamp
            return frame
        except BaseException:
            self.close()
            raise

    def close(self):
        for name in ("_connection", "_listener"):
            sock = getattr(self, name, None)
            if sock is not None:
                sock.close()
                setattr(self, name, None)


def receive(host="127.0.0.1", port=47004, *, timeout=30.0,
            max_frame_bytes=_DEFAULT_LIMIT) -> Receiver:
    """Listen for one sender and yield Frames; use `with receive() as frames:`.

    The default listens on this computer only. For remote use, bind to the
    desired interface and use a trusted network or an SSH tunnel. Transport
    has no encryption or authentication. Port 0 chooses an available port,
    reported by the returned receiver's address property.
    """
    return Receiver(host, port, timeout, max_frame_bytes)
