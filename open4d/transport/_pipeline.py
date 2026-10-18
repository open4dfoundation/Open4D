"""Bounded capture -> encode -> send pipeline for live O4S2 streams.

Capture is the caller: `LiveSender.submit` never waits for encoding or the
network when ``latest=True``. One worker encodes and one sends. Each hand-off
is a `LatestFrameQueue`, so a slow stage replaces stale items instead of
growing a backlog. Encoded packets are only replaced for profiles whose frames
are all independent; a dependent packet that is dropped (for example after its
send deadline) makes the encoder emit a keyframe and the sender skip packets
until that keyframe, so the receiver never sees a broken reference chain.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field, replace
import math
from numbers import Real
import socket
import threading
import time

from ..codec._stream import DEFAULT_MAX_FRAME_BYTES, PREFIX, MAGIC, END, StreamEncoder
from ..core import Frame


@dataclass(frozen=True)
class StageTiming:
    """Count, total and maximum seconds spent in one pipeline stage."""

    count: int = 0
    total: float = 0.0
    maximum: float = 0.0

    @property
    def mean(self) -> float | None:
        """Average seconds per item, or None before the first item."""
        return self.total / self.count if self.count else None

    def add(self, seconds: float) -> "StageTiming":
        seconds = max(0.0, float(seconds))
        return StageTiming(self.count + 1, self.total + seconds, max(self.maximum, seconds))


class LatestFrameQueue:
    """A bounded, thread-safe hand-off that keeps the newest items.

    ``put`` never blocks while ``drop_stale`` is true: when the queue is full it
    discards the oldest item, returns it and counts it in ``dropped``. With
    ``drop_stale=False`` it is an ordinary bounded FIFO whose ``put`` waits for
    space. After ``close()``, ``get`` drains the remaining items and then raises
    EOFError; ``close(discard=True)`` drops them as well.
    """

    def __init__(self, capacity: int = 1, *, drop_stale: bool = True):
        if isinstance(capacity, bool) or not isinstance(capacity, int) or capacity < 1:
            raise ValueError("capacity must be a positive integer")
        self.capacity = capacity
        self.drop_stale = drop_stale
        self.dropped = 0
        self._items = deque()
        self._closed = False
        self._condition = threading.Condition()

    def __len__(self) -> int:
        with self._condition:
            return len(self._items)

    @property
    def closed(self) -> bool:
        return self._closed

    def put(self, item, timeout: float | None = None):
        """Add an item; return the item it displaced, or None."""
        with self._condition:
            if not self.drop_stale and not self._condition.wait_for(
                    lambda: self._closed or len(self._items) < self.capacity, timeout):
                raise TimeoutError("queue stayed full")
            if self._closed:
                raise ValueError("put on a closed queue")
            displaced = None
            if len(self._items) >= self.capacity:
                displaced = self._items.popleft()
                self.dropped += 1
            self._items.append(item)
            self._condition.notify_all()
            return displaced

    def get(self, timeout: float | None = None):
        """Remove the oldest item, waiting up to ``timeout`` seconds."""
        with self._condition:
            if not self._condition.wait_for(lambda: self._items or self._closed, timeout):
                raise TimeoutError("queue stayed empty")
            if not self._items:
                raise EOFError("queue closed")
            item = self._items.popleft()
            self._condition.notify_all()
            return item

    def close(self, *, discard: bool = False) -> None:
        """Stop accepting items and wake every waiting thread."""
        with self._condition:
            self._closed = True
            if discard:
                self._items.clear()
            self._condition.notify_all()


@dataclass(frozen=True)
class PipelineStats:
    """A snapshot of a `LiveSender`.

    ``submitted`` frames entered the pipeline; ``encoded`` and ``sent`` count
    frames that finished those stages. Frames lost on the way are split by
    cause: replaced while waiting to encode, replaced or chain-broken while
    waiting to send, or older than ``send_deadline`` when their turn came.
    ``queue`` is the time a sent frame waited between stages, and ``latency``
    runs from submit to the end of its send. Bytes count sent packets,
    including the stream header and end packet in ``wire_bytes``.
    """

    submitted: int = 0
    encoded: int = 0
    sent: int = 0
    dropped_before_encode: int = 0
    dropped_before_send: int = 0
    dropped_late: int = 0
    keyframes: int = 0
    payload_bytes: int = 0
    decoded_bytes: int = 0
    wire_bytes: int = 0
    encode: StageTiming = field(default_factory=StageTiming)
    queue: StageTiming = field(default_factory=StageTiming)
    send: StageTiming = field(default_factory=StageTiming)
    latency: StageTiming = field(default_factory=StageTiming)
    elapsed: float = 0.0

    @property
    def dropped(self) -> int:
        """Every submitted frame that was not sent."""
        return self.dropped_before_encode + self.dropped_before_send + self.dropped_late

    @property
    def fps(self) -> float | None:
        """Frames sent per second of pipeline time, or None."""
        return self.sent / self.elapsed if self.sent and self.elapsed > 0 else None

    @property
    def bits_per_second(self) -> float | None:
        """Average sent rate over the pipeline's lifetime, or None."""
        return self.wire_bytes * 8 / self.elapsed if self.sent and self.elapsed > 0 else None

    @property
    def compression_ratio(self) -> float | None:
        """Decoded array bytes per sent payload byte, or None."""
        return self.decoded_bytes / self.payload_bytes if self.payload_bytes else None


def _seconds(value, name):
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, Real) or not 0 < value < math.inf:
        raise ValueError(f"{name} must be a positive number of seconds or None")
    return float(value)


class LiveSender:
    """Stream live frames to `open4d.receive` through an O4S2 codec profile.

    Use as a context manager: leaving it normally sends what is still queued
    and the end-of-stream packet; leaving with an exception aborts. With
    ``latest=True`` (the default) ``submit`` returns at once and a busy encoder
    or network replaces stale frames; with ``latest=False`` it waits, so every
    frame is delivered. ``send_deadline`` drops frames older than that many
    seconds since submit when their turn to send comes. ``stats`` reports
    per-stage timing and drop counts while running. Worker errors re-raise
    from ``submit`` and ``close``. Profile keyword options pass to
    `open4d.codec.StreamEncoder`.
    """

    def __init__(self, host="127.0.0.1", port=47004, *, profile="raw", latest=True,
                 send_deadline=None, timeout=30.0, max_frame_bytes=DEFAULT_MAX_FRAME_BYTES,
                 **options):
        self._encoder = StreamEncoder(profile, max_frame_bytes=max_frame_bytes, **options)
        self._deadline = _seconds(send_deadline, "send_deadline")
        self._latest = bool(latest)
        self._lock = threading.Lock()
        self._stats = PipelineStats()
        self._error = None
        self._previous_timestamp = None
        self._closed = False
        self._frames = LatestFrameQueue(1, drop_stale=self._latest)
        self._packets = LatestFrameQueue(
            1 if self._latest and self._encoder.independent else 2,
            drop_stale=self._latest and self._encoder.independent)
        self._socket = socket.create_connection((host, port), timeout=timeout)
        self._started = time.monotonic()
        self._threads = [threading.Thread(target=self._guarded, args=(work,), name=name, daemon=True)
                         for work, name in ((self._encode_loop, "open4d-stream-encode"),
                                            (self._send_loop, "open4d-stream-send"))]
        for thread in self._threads:
            thread.start()

    @property
    def profile(self) -> str:
        return self._encoder.profile.id

    @property
    def stats(self) -> PipelineStats:
        with self._lock:
            stats = self._stats
            if not self._closed:
                stats = replace(stats, elapsed=time.monotonic() - self._started)
            return stats

    def _update(self, **changes):
        """Apply ``name=function(old value)`` changes to the stats atomically."""
        with self._lock:
            self._stats = replace(self._stats, **{name: change(getattr(self._stats, name))
                                                  for name, change in changes.items()})

    def _guarded(self, work):
        try:
            work()
        except BaseException as error:  # Surfaced to the caller by submit/close.
            if self._error is None:
                self._error = error
            self._frames.close(discard=True)
            self._packets.close(discard=True)

    def _raise(self):
        if self._error is not None:
            raise self._error

    def submit(self, frame: Frame) -> bool:
        """Queue one captured frame; return False if it replaced an unencoded one."""
        self._raise()
        if self._closed:
            raise RuntimeError("the sender is closed")
        if not isinstance(frame, Frame):
            raise TypeError("stream items must be Open4D Frames")
        if self._previous_timestamp is not None and frame.timestamp < self._previous_timestamp:
            raise ValueError("stream timestamps must be nondecreasing")
        self._previous_timestamp = frame.timestamp
        try:
            displaced = self._frames.put((frame, time.monotonic(), time.time()))
        except ValueError:
            self._raise()
            raise
        self._update(submitted=lambda n: n + 1,
                     dropped_before_encode=lambda n: n + (displaced is not None))
        return displaced is None

    def _encode_loop(self):
        while True:
            try:
                frame, submitted, wall = self._frames.get()
            except EOFError:
                self._packets.close()
                return
            started = time.monotonic()
            packet = self._encoder.encode(frame, captured_at=wall)
            encoded = time.monotonic()
            displaced = self._packets.put((packet, submitted, started - submitted, encoded))
            self._update(encoded=lambda n: n + 1,
                         encode=lambda timing: timing.add(encoded - started),
                         dropped_before_send=lambda n: n + (displaced is not None))

    def _send(self, data):
        self._socket.sendall(data)
        self._update(wire_bytes=lambda n: n + len(data))

    def _send_loop(self):
        header_sent = False
        last_sent = None
        while True:
            try:
                packet, submitted, waited, encoded = self._packets.get()
            except EOFError:
                break
            started = time.monotonic()
            if self._deadline is not None and started - submitted > self._deadline:
                self._encoder.request_keyframe()
                self._update(dropped_late=lambda n: n + 1)
                continue
            if not packet.keyframe and packet.dependency != last_sent:
                self._encoder.request_keyframe()
                self._update(dropped_before_send=lambda n: n + 1)
                continue
            if not header_sent:
                self._send(self._encoder.header_packet)
                header_sent = True
            self._send(packet.data)
            finished = time.monotonic()
            last_sent = packet.sequence
            self._update(sent=lambda n: n + 1, keyframes=lambda n: n + packet.keyframe,
                         payload_bytes=lambda n: n + packet.payload_bytes,
                         decoded_bytes=lambda n: n + packet.decoded_bytes,
                         send=lambda timing: timing.add(finished - started),
                         queue=lambda timing: timing.add(waited + started - encoded),
                         latency=lambda timing: timing.add(finished - submitted))
        if self._error is not None:
            return  # Leave the stream unterminated so the receiver reports the failure.
        if not header_sent and self._encoder.header_packet is not None:
            self._send(self._encoder.header_packet)
        self._send(PREFIX.pack(MAGIC, END, 0, 0, 0, 0))

    def close(self) -> PipelineStats:
        """Deliver queued frames, end the stream and return the final stats."""
        if not self._closed:
            self._frames.close()
            for thread in self._threads:
                thread.join()
            self._finish()
        self._raise()
        return self._stats

    def abort(self) -> None:
        """Stop without delivering queued frames or ending the stream."""
        if not self._closed:
            self._frames.close(discard=True)
            self._packets.close(discard=True)
            try:
                self._socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            for thread in self._threads:
                thread.join()
            self._finish()

    def _finish(self):
        with self._lock:
            self._closed = True
            self._stats = replace(self._stats, elapsed=time.monotonic() - self._started)
        self._socket.close()

    def __enter__(self):
        return self

    def __exit__(self, kind, error, traceback):
        if kind is None:
            self.close()
        else:
            self.abort()
