"""Transfer frames over TCP: O4S1 decoded meshes or O4S2 codec packets.

O4S1, the default for `send` without a profile, carries uncompressed triangle
mesh arrays. O4S2 (see `open4d.codec.StreamEncoder`) carries mesh, point and
Gaussian frames through a codec profile. A receiver reads either, decided by
the first packet's magic.
"""

from __future__ import annotations

from dataclasses import dataclass, field, replace
import json
import math
from numbers import Real
import re
import selectors
import socket
import struct
import threading
import time

import numpy as np

from ..codec._stream import MAGIC as _O4S2, StreamDecoder
from ..core import Frame, MemoryFrameProvider, Sequence, TriangleMesh
from ._pipeline import LatestFrameQueue, LiveSender, StageTiming


_HEADER = struct.Struct("!4sIQ")
_MAGIC = b"O4S1"
_MAX_HEADER = 1024 * 1024
_DEFAULT_LIMIT = 64 * 1024 * 1024
_GEOMETRY_FIELDS = ("positions", "triangles", "colors", "normals", "texture_coordinates")
_DTYPE = re.compile(r"[<>|=]?[biuf][1248]")


@dataclass(frozen=True)
class StreamStats:
    """Totals for the frames a Receiver has decoded so far.

    payload_bytes counts array bytes for O4S1 and codec payload bytes for
    O4S2; decoded_bytes counts the decoded arrays. wire_bytes also counts
    packet headers and JSON (and the O4S2 stream header), but not the
    end-of-stream marker. elapsed is the time in seconds from the arrival of
    the first frame to the moment the most recent frame finished decoding; it
    does not grow while the receiver waits for the next frame.

    O4S2 streams also report decode time per frame, latency from the sender's
    capture clock to decoded frame (meaningful when both clocks agree, as on
    one machine), ``skipped`` packets the sender encoded but dropped, and
    ``replaced`` frames that `Receiver.latest` discarded for newer ones.
    """

    frames: int = 0
    payload_bytes: int = 0
    wire_bytes: int = 0
    elapsed: float = 0.0
    decoded_bytes: int = 0
    skipped: int = 0
    replaced: int = 0
    decode: StageTiming = field(default_factory=StageTiming)
    latency: StageTiming = field(default_factory=StageTiming)

    @property
    def fps(self) -> float | None:
        """Frame arrivals per second, (frames - 1) / elapsed, or None."""
        if self.frames < 2 or self.elapsed <= 0:
            return None
        return (self.frames - 1) / self.elapsed

    @property
    def bits_per_second(self) -> float | None:
        """Average received rate, wire_bytes * 8 / elapsed, or None."""
        if self.frames < 2 or self.elapsed <= 0:
            return None
        return self.wire_bytes * 8 / self.elapsed


def _limit(value):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError("max_frame_bytes must be a positive integer")
    return value


def _encode(frame, limit):
    if not isinstance(frame, Frame):
        raise TypeError("stream items must be Open4D Frames")
    if not isinstance(frame.geometry, TriangleMesh):
        raise TypeError("streaming currently supports triangle mesh frames")
    geometry = frame.geometry
    if geometry.texture is not None:
        raise ValueError("O4S1 does not carry texture images")
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
        "attribute_domains": dict(geometry.attribute_domains),
        "arrays": [{"name": name, "dtype": array.dtype.str, "shape": array.shape}
                   for name, array in zip(values, arrays)],
    }, allow_nan=False, separators=(",", ":")).encode("utf-8")
    if len(header) > _MAX_HEADER:
        raise ValueError("frame metadata exceeds 1 MiB")
    return _HEADER.pack(_MAGIC, len(header), length), header, arrays


def _reject_constant(name):
    raise ValueError(f"{name} is not allowed")


def _decode(header, payload):
    try:
        info = json.loads(header, parse_constant=_reject_constant)
        descriptions = info["arrays"]
        if not isinstance(descriptions, list):
            raise ValueError("arrays must be a list")
        arrays = {}
        offset = 0
        for description in descriptions:
            name = description["name"]
            if (not isinstance(description["dtype"], str)
                    or not _DTYPE.fullmatch(description["dtype"])):
                raise ValueError("invalid array description")
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
                     TriangleMesh(**fields, attributes=attributes,
                                  attribute_domains=info.get("attribute_domains", {})), info["metadata"])
    except (KeyError, TypeError, ValueError, OverflowError, RecursionError) as error:
        raise ValueError(f"invalid mesh stream frame: {error}") from error


class _Pace:
    """Sleep so frames leave with their timestamps' spacing."""

    def __init__(self, realtime):
        self.realtime = realtime
        self.first = self.started = None

    def wait(self, timestamp):
        if self.first is None:
            self.first, self.started = timestamp, time.monotonic()
        if self.realtime:
            remaining = timestamp - self.first - (time.monotonic() - self.started)
            if remaining > 0:
                time.sleep(remaining)


def send(frames, host="127.0.0.1", port=47004, *, realtime=True, timeout=30.0,
         max_frame_bytes=_DEFAULT_LIMIT, profile=None, profile_options=None,
         latest=False, send_deadline=None) -> int:
    """Send a Sequence or iterable of Frames to a running receiver.

    By default, preserve the spacing between timestamps. Set realtime=False
    to transfer as fast as possible. Returns the number of frames sent.
    Without a profile this is the original O4S1 protocol: decoded triangle
    mesh arrays and frame metadata, uncompressed. ``profile`` selects O4S2 and
    a codec profile for mesh, point or Gaussian frames: ``raw``, ``lossless``,
    ``zlib``, ``zstd``, ``draco`` or ``quantized``, configured by
    ``profile_options`` (see `open4d.codec.stream_profiles`). With O4S2,
    ``latest=True`` lets a busy encoder or network replace stale frames and
    ``send_deadline`` drops frames that waited longer than that many seconds;
    use `LiveSender` directly for per-stage timing statistics.
    Raises ConnectionError if the receiver closes mid-stream; frames still in
    the operating system's send buffer when it closes go unnoticed.
    """
    pace = _Pace(realtime)
    if profile is not None:
        with LiveSender(host, port, profile=profile, latest=latest, send_deadline=send_deadline,
                        timeout=timeout, max_frame_bytes=max_frame_bytes,
                        **(profile_options or {})) as sender:
            for frame in frames:
                if not isinstance(frame, Frame):
                    raise TypeError("stream items must be Open4D Frames")
                pace.wait(frame.timestamp)
                sender.submit(frame)
        return sender.stats.sent
    if profile_options or latest or send_deadline is not None:
        raise ValueError("profile_options, latest and send_deadline need an O4S2 profile")
    limit = _limit(max_frame_bytes)
    count = 0
    previous_timestamp = None
    with socket.create_connection((host, port), timeout=timeout) as sock:
        for frame in frames:
            fixed, header, arrays = _encode(frame, limit)
            if previous_timestamp is not None and frame.timestamp < previous_timestamp:
                raise ValueError("stream timestamps must be nondecreasing")
            pace.wait(frame.timestamp)
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
    """One incoming stream. Use as a context manager to close it on early exit.

    Iterating decodes each frame as its packets arrive, so frames can feed a
    viewer (``Visualizer().stream(receiver)``) without collecting the stream.
    close() may be called from another thread: a next() call blocked waiting
    for the sender then ends promptly with StopIteration.
    """

    def __init__(self, host, port, timeout, max_frame_bytes):
        self._limit = _limit(max_frame_bytes)
        self._timeout = timeout
        self._lock = threading.RLock()
        self._closing = False
        self._connection = None
        self._previous_timestamp = None
        self._pending = None
        self._started = None
        self._stats = StreamStats()
        self._protocol = None
        self._decoder = None
        self._unread = b""
        self._received = 0
        self._wake, self._waker = socket.socketpair()
        self._selector = selectors.DefaultSelector()
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self._selector.register(self._wake, selectors.EVENT_READ)
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

    @property
    def stats(self) -> StreamStats:
        """A snapshot of the frames, bytes and time received so far."""
        return self._stats

    @property
    def protocol(self) -> str | None:
        """``"O4S1"`` or ``"O4S2"`` once the first packet arrives, else None."""
        return self._protocol

    @property
    def header(self) -> dict | None:
        """The O4S2 stream header (representation, codec, fields), else None."""
        return None if self._decoder is None else self._decoder.header

    def _wait(self, sock):
        self._selector.register(sock, selectors.EVENT_READ)
        try:
            ready = self._selector.select(self._timeout)
        finally:
            self._selector.unregister(sock)
        if self._closing:
            raise StopIteration
        if not ready:
            raise TimeoutError("timed out")

    def _read(self, size):
        data = bytearray(self._unread[:size])
        self._unread = self._unread[size:]
        while len(data) < size:
            self._wait(self._connection)
            try:
                part = self._connection.recv(min(size - len(data), 1024 * 1024))
            except ConnectionResetError:
                part = b""
            if not part:
                raise EOFError("sender disconnected before the stream finished")
            self._received += len(part)
            data.extend(part)
        return data

    def _next_o4s1(self):
        fixed = self._read(_HEADER.size)
        arrived = time.monotonic()
        magic, header_size, payload_size = _HEADER.unpack(fixed)
        if magic != _MAGIC:
            raise ValueError("unrecognized mesh stream version")
        if header_size == 0 and payload_size == 0:
            return None
        if not 0 < header_size <= _MAX_HEADER or payload_size > self._limit:
            raise ValueError("stream frame exceeds header or max_frame_bytes limits")
        frame = _decode(self._read(header_size), self._read(payload_size))
        if (self._previous_timestamp is not None
                and frame.timestamp < self._previous_timestamp):
            raise ValueError("stream timestamps must be nondecreasing")
        self._previous_timestamp = frame.timestamp
        if self._started is None:
            self._started = arrived
        stats = self._stats
        self._stats = replace(
            stats, frames=stats.frames + 1, payload_bytes=stats.payload_bytes + payload_size,
            decoded_bytes=stats.decoded_bytes + payload_size,
            wire_bytes=stats.wire_bytes + _HEADER.size + header_size + payload_size,
            elapsed=time.monotonic() - self._started)
        return frame

    def _next_o4s2(self):
        frame = self._decoder.read(self._read)
        if frame is None:
            return None
        packet = self._decoder.last_packet
        now = time.monotonic()
        if self._started is None:
            self._started = now - packet.decode_seconds
        stats = self._stats
        self._stats = replace(
            stats, frames=stats.frames + 1,
            payload_bytes=stats.payload_bytes + packet.payload_bytes,
            decoded_bytes=stats.decoded_bytes + packet.decoded_bytes,
            wire_bytes=self._received, skipped=stats.skipped + packet.skipped,
            decode=stats.decode.add(packet.decode_seconds),
            latency=stats.latency.add(time.time() - packet.captured_at),
            elapsed=now - self._started)
        return frame

    def __next__(self):
        with self._lock:
            if self._closing:
                raise StopIteration
            if self._pending is not None:
                frame, self._pending = self._pending, None
                return frame
            try:
                if self._connection is None:
                    self._wait(self._listener)
                    self._connection, _ = self._listener.accept()
                    self._connection.settimeout(self._timeout)
                    # One sender per receiver: refuse later connections.
                    self._listener.close()
                    self._listener = None
                if self._protocol is None:
                    # The first packet's magic fixes the protocol for the connection.
                    magic = bytes(self._read(4))
                    if magic == _O4S2:
                        self._decoder = StreamDecoder(max_frame_bytes=self._limit)
                    elif magic != _MAGIC:
                        raise ValueError("unrecognized stream version")
                    self._protocol = magic.decode("ascii")
                    self._unread = magic
                frame = self._next_o4s1() if self._protocol == "O4S1" else self._next_o4s2()
                if frame is None:
                    self.close()
                    raise StopIteration
                return frame
            except BaseException:
                self.close()
                raise

    def run(self, callback, *, max_frames=None) -> int:
        """Call ``callback(frame)`` for each frame as it is decoded.

        Stops when the stream ends, the receiver closes or after max_frames
        frames, and returns how many frames were delivered. Nothing is kept.
        """
        if max_frames is not None and (isinstance(max_frames, bool)
                                       or not isinstance(max_frames, int) or max_frames < 0):
            raise ValueError("max_frames must be a nonnegative integer or None")
        count = 0
        while max_frames is None or count < max_frames:
            frame = next(self, None)
            if frame is None:
                break
            callback(frame)
            count += 1
        return count

    def latest(self):
        """Yield the newest decoded frame, skipping frames a slow consumer missed.

        A background thread keeps reading and decoding, so dependent frames
        stay decodable and a slow consumer never stalls the sender; a decoded
        frame not yet taken is replaced by the next one and counted in
        ``stats.replaced``. Feed a viewer with
        ``Visualizer().stream(receiver.latest())``. The reader stops when the
        stream ends or the receiver closes; its errors re-raise here.
        """
        mailbox = LatestFrameQueue(1)
        failure = []

        def pump():
            try:
                for frame in self:
                    if mailbox.put(frame) is not None:
                        with self._lock:
                            self._stats = replace(self._stats, replaced=self._stats.replaced + 1)
            except BaseException as error:
                failure.append(error)
            finally:
                mailbox.close()

        threading.Thread(target=pump, name="open4d-receive-latest", daemon=True).start()
        while True:
            try:
                yield mailbox.get()
            except EOFError:
                break
        if failure:
            raise failure[0]

    def record(self, *, max_frames=None, duration=None) -> Sequence:
        """Collect received frames into an in-memory Sequence.

        Stops when the sender ends the stream, when close() is called from
        another thread, after max_frames frames, or at the first frame whose
        timestamp is duration seconds or more after the first recorded frame's
        timestamp (stream time, not wall-clock time). That frame is not
        recorded; the next next() or record() call returns it. Stopping at
        max_frames or duration leaves the receiver open. Errors propagate
        and discard the frames recorded by this call.
        """
        if max_frames is not None and (isinstance(max_frames, bool)
                                       or not isinstance(max_frames, int) or max_frames < 0):
            raise ValueError("max_frames must be a nonnegative integer or None")
        if duration is not None and (isinstance(duration, bool) or not isinstance(duration, Real)
                                     or not 0 < duration < math.inf):
            raise ValueError("duration must be a positive number of seconds or None")
        frames = []
        while max_frames is None or len(frames) < max_frames:
            frame = next(self, None)
            if frame is None:
                break
            if duration is not None and frames and frame.timestamp - frames[0].timestamp >= duration:
                self._pending = frame
                break
            frames.append(frame)
        return Sequence(MemoryFrameProvider(frames))

    def close(self):
        """Stop receiving and release the port. Safe to call from any thread."""
        if not self._closing:
            self._closing = True
            try:
                self._waker.send(b"\0")
            except (AttributeError, OSError):
                pass
        with self._lock:
            self._pending = None
            for name in ("_connection", "_listener", "_selector", "_wake", "_waker"):
                resource = getattr(self, name, None)
                if resource is not None:
                    resource.close()
                    setattr(self, name, None)


def receive(host="127.0.0.1", port=47004, *, timeout=30.0,
            max_frame_bytes=_DEFAULT_LIMIT) -> Receiver:
    """Listen for one sender and yield Frames; use `with receive() as frames:`.

    The receiver reads O4S1 mesh streams and O4S2 codec streams alike and
    decodes each frame as it arrives. max_frame_bytes bounds every payload
    and every decoded frame.

    The default listens on this computer only. For remote use, bind to the
    desired interface and use a trusted network or an SSH tunnel. Transport
    has no encryption or authentication. Port 0 chooses an available port,
    reported by the returned receiver's address property. The receiver accepts
    one sender and then stops listening. See Receiver.stats, Receiver.record
    and Receiver.close for measuring, saving and stopping a live stream.
    """
    return Receiver(host, port, timeout, max_frame_bytes)
