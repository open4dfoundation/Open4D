"""Incremental O4S2 stream sessions: frames in, packets out, and back.

O4S2 is the versioned live-frame protocol behind `open4d.transport`. It also
works without a socket: ``StreamEncoder.push`` returns bytes and
``StreamDecoder.feed`` accepts arbitrary chunks of them.

Every packet starts with a 20-byte prefix in network byte order::

    magic  4s  b"O4S2"
    kind   B   1 stream header, 2 frame, 3 end of stream
    flags  B   bit 0 marks a keyframe (frame packets only); others are 0
    zero   H   reserved, 0
    info   I   length of the UTF-8 JSON information that follows
    size   Q   length of the codec payload after the information

A frame packet has a 40-byte descriptor between the prefix and its JSON:
sequence (Q), frame index (Q), capture timestamp in stream seconds (d), sender
wall-clock capture time for latency measurement (d) and the sequence of the
packet it depends on, or -1 (q). Keyframes have no dependency. Sequences
increase by one per encoded frame; a gap means the sender dropped packets.

The stream header JSON declares the representation (``mesh``, ``points`` or
``gaussians``), the codec profile id, version and options, whether every frame
is independent, and a schema of at most 256 fields, each with a name of at most
128 characters, a little-endian dtype and a shape whose first dimension varies
per frame. Frame JSON carries the frame metadata, each field's shape (or null
when an optional field is absent) and profile-specific values. Receivers
reject unknown keys, wrong types, oversized JSON, payloads above
``max_frame_bytes`` and declared shapes whose decoded size exceeds it before
decompressing anything. Draco payloads are decoded by the Draco library itself
and their output is checked against the same limit afterwards.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
import math
import re
import struct
import time

import numpy as np

from open4d.core import Frame

from ._npz import _GEOMETRIES, _MAX_FRAME_MEMBERS, _geometry
from ._stream_profiles import PROFILES, StreamField, resolve_profile

MAGIC = b"O4S2"
PREFIX = struct.Struct("!4sBBHIQ")
DESCRIPTOR = struct.Struct("!QQddq")
HEADER, FRAME, END = 1, 2, 3
KEYFRAME = 1
DEFAULT_MAX_FRAME_BYTES = 64 * 1024 * 1024
MAX_HEADER_INFO = 64 * 1024
MAX_FRAME_INFO = 1024 * 1024
MAX_FIELDS = _MAX_FRAME_MEMBERS
MAX_NAME = 128
MAX_DIMENSIONS = 8
_MAX_DIMENSION = 2 ** 31 - 1
_DTYPE = re.compile(r"[<|][biuf][1248]")
_ATTRIBUTE = "attribute:"


def _limit(value):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError("max_frame_bytes must be a positive integer")
    return value


def _reject_constant(name):
    raise ValueError(f"{name} is not allowed")


def _json(data: bytes, what: str) -> dict:
    try:
        value = json.loads(bytes(data).decode("utf-8"), parse_constant=_reject_constant)
    except (UnicodeDecodeError, ValueError, RecursionError) as error:
        raise ValueError(f"invalid O4S2 {what}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"invalid O4S2 {what}: expected a JSON object")
    return value


def _dump(value) -> bytes:
    return json.dumps(value, allow_nan=False, separators=(",", ":")).encode("utf-8")


def _canonical(array: np.ndarray) -> np.ndarray:
    if array.dtype.byteorder == ">":
        array = array.astype(array.dtype.newbyteorder("<"))
    return np.ascontiguousarray(array)


def _frame_arrays(frame: Frame):
    geometry = frame.geometry
    if getattr(geometry, "texture", None) is not None:
        raise ValueError("live profiles do not carry texture images; stream an explicit appearance profile")
    representation = geometry.representation.value
    if representation not in _GEOMETRIES:
        raise TypeError(f"O4S2 streams carry mesh, points or gaussians, not {representation!r}")
    values = {}
    for name in _GEOMETRIES[representation][1]:
        value = getattr(geometry, name)
        if value is not None:
            values[name] = _canonical(value)
    for name, value in geometry.attributes.items():
        values[_ATTRIBUTE + name] = _canonical(value)
    for name, value in values.items():
        if value.dtype.kind not in "fiub" or value.dtype.itemsize > 8:
            raise ValueError(f"stream field {name!r} must contain real numbers, integers or booleans")
    return representation, values


@dataclass(frozen=True)
class StreamPacket:
    """One encoded frame packet and what a sender needs to schedule it.

    ``data`` is the complete frame packet. The stream header packet is
    available separately as `StreamEncoder.header_packet`. ``dependency`` is
    the sequence of the packet this one needs, or -1 for a keyframe.
    """

    sequence: int
    frame_index: int
    timestamp: float
    captured_at: float
    keyframe: bool
    dependency: int
    data: bytes
    payload_bytes: int
    decoded_bytes: int


@dataclass(frozen=True)
class DecodedPacket:
    """What `StreamDecoder` learned from the most recent frame packet."""

    sequence: int
    frame_index: int
    timestamp: float
    captured_at: float
    keyframe: bool
    dependency: int
    payload_bytes: int
    wire_bytes: int
    decoded_bytes: int
    skipped: int
    decode_seconds: float


def stream_profiles() -> dict:
    """Describe the O4S2 payload profiles: lossless, representations, options.

    ``lossless`` is an alias for ``zstd`` when the optional ``zstandard``
    package is installed and ``zlib`` otherwise.
    """
    result = {name: {"version": profile.version, "lossless": profile.lossless,
                   "available": profile.available,
                   "representations": sorted(profile.representations),
                   "options": profile.options({})}
            for name, profile in PROFILES.items()}
    selected = resolve_profile("lossless").id
    result["lossless"] = {**result[selected], "alias_for": selected}
    return result


class StreamEncoder:
    """Encode Frames into O4S2 packets for one stream at a time.

    ``profile`` is ``raw``, ``lossless`` (zstd, or zlib without
    ``zstandard``), ``zlib``, ``zstd``, ``draco`` (points and meshes) or
    ``quantized``; keyword options configure it (see `stream_profiles`). The
    first frame fixes the representation and field schema; later frames may
    omit optional fields but may not add new ones. ``push`` returns the bytes
    to transmit for one frame (preceded by the stream header the first time);
    ``flush`` returns the end-of-stream packet and ``reset`` starts a new
    stream. GaussianSplats travel as GaussianCloud frames; see
    `splats_to_frame`.
    """

    def __init__(self, profile: str = "raw", *, max_frame_bytes: int = DEFAULT_MAX_FRAME_BYTES,
                 **options):
        self.profile = resolve_profile(profile)
        if not self.profile.available:
            raise ValueError(f"the {self.profile.id} stream profile needs the optional "
                             f"{self.profile.requires!r} package")
        self.options = self.profile.options(options)
        self.max_frame_bytes = _limit(max_frame_bytes)
        self.reset()

    @property
    def independent(self) -> bool:
        """Whether every frame is a keyframe, so stale packets may be dropped."""
        return self.profile.independent(self.options)

    @property
    def header(self) -> dict | None:
        """The stream header JSON, available once the first frame is encoded."""
        return None if self._header is None else json.loads(self._header[PREFIX.size:])

    @property
    def header_packet(self) -> bytes | None:
        """The stream header packet, available once the first frame is encoded."""
        return self._header

    def reset(self) -> None:
        """Forget the current stream; the next frame starts a new header."""
        self._header = None
        self._fields = None
        self._representation = None
        self._sequence = 0
        self._reference = None
        self._since_keyframe = 0
        self._keyframe_requested = False
        self._previous_timestamp = None
        self._header_pushed = False
        self._finished = False

    def request_keyframe(self) -> None:
        """Make the next frame independent, e.g. after a dropped packet."""
        self._keyframe_requested = True

    def _schema(self, representation, values):
        if len(values) > MAX_FIELDS:
            raise ValueError(f"O4S2 streams carry at most {MAX_FIELDS} fields")
        fields = []
        for name, value in values.items():
            if len(name) > MAX_NAME:
                raise ValueError(f"stream field names are limited to {MAX_NAME} characters")
            if not 1 <= value.ndim <= MAX_DIMENSIONS:
                raise ValueError(f"stream field {name!r} must have 1 to {MAX_DIMENSIONS} dimensions")
            fields.append(StreamField(name, value.dtype, (None, *value.shape[1:])))
        header = {"stream": "O4S2", "version": 1, "representation": representation,
                  "codec": {"id": self.profile.id, "version": self.profile.version,
                            "options": self.options},
                  "independent": self.independent,
                  "fields": [field.describe() for field in fields]}
        info = _dump(header)
        if len(info) > MAX_HEADER_INFO:
            raise ValueError("O4S2 stream header exceeds 64 KiB")
        self._fields = tuple(fields)
        self._representation = representation
        self._header = PREFIX.pack(MAGIC, HEADER, 0, 0, len(info), 0) + info

    def encode(self, frame: Frame, *, captured_at: float | None = None) -> StreamPacket:
        """Encode one frame; ``captured_at`` defaults to the current wall clock."""
        if self._finished:
            raise RuntimeError("the stream was flushed; call reset() to start another")
        if not isinstance(frame, Frame):
            raise TypeError("stream items must be Open4D Frames")
        representation, values = _frame_arrays(frame)
        if representation not in self.profile.representations:
            raise TypeError(f"the {self.profile.id} stream profile does not carry "
                            f"{representation!r} frames")
        if self._previous_timestamp is not None and frame.timestamp < self._previous_timestamp:
            raise ValueError("stream timestamps must be nondecreasing")
        captured_at = time.time() if captured_at is None else float(captured_at)
        if not math.isfinite(captured_at):
            raise ValueError("captured_at must be finite")
        metadata = _dump(dict(frame.metadata))  # Reject non-JSON metadata before any state changes.
        decoded_bytes = sum(value.nbytes for value in values.values())
        if decoded_bytes > self.max_frame_bytes:
            raise ValueError("frame exceeds max_frame_bytes")
        if self._fields is None:
            self._schema(representation, values)
        elif representation != self._representation:
            raise ValueError("a stream carries one representation")
        names = {field.name for field in self._fields}
        if not set(values) <= names:
            raise ValueError("frame fields not declared by the stream's first frame: "
                             f"{', '.join(sorted(set(values) - names))}")
        arrays = []
        for field in self._fields:
            value = values.get(field.name)
            if value is not None and (value.dtype != field.dtype
                                      or (None, *value.shape[1:]) != field.shape):
                raise ValueError(f"stream field {field.name!r} changed dtype or shape")
            arrays.append(value)
        interval = self.options.get("keyframe_interval", 1)
        keyframe = (self.independent or self._reference is None or self._keyframe_requested
                    or self._since_keyframe + 1 >= interval)
        reference = None if keyframe else self._reference
        payload, codec = self.profile.encode(self._fields, arrays, reference, self.options)
        if len(payload) > self.max_frame_bytes:
            raise ValueError("encoded frame exceeds max_frame_bytes")
        info = (b'{"metadata":' + metadata + b',"attribute_domains":'
                + _dump(dict(getattr(frame.geometry, "attribute_domains", {}))) + b',"shapes":'
                + _dump([None if value is None else list(value.shape) for value in arrays])
                + b',"codec":' + _dump(codec) + b"}")
        if len(info) > MAX_FRAME_INFO:
            raise ValueError("frame metadata exceeds 1 MiB")
        sequence = self._sequence
        dependency = -1 if keyframe else sequence - 1
        data = b"".join((PREFIX.pack(MAGIC, FRAME, KEYFRAME if keyframe else 0, 0, len(info),
                                     len(payload)),
                         DESCRIPTOR.pack(sequence, frame.frame_index, frame.timestamp,
                                         captured_at, dependency),
                         info, payload))
        if not self.independent:
            self._reference = [None if value is None else value.tobytes() for value in arrays]
        self._since_keyframe = 0 if keyframe else self._since_keyframe + 1
        self._keyframe_requested = False
        self._sequence += 1
        self._previous_timestamp = frame.timestamp
        return StreamPacket(sequence, frame.frame_index, frame.timestamp, captured_at, keyframe,
                            dependency, data, len(payload), decoded_bytes)

    def push(self, frame: Frame, *, captured_at: float | None = None) -> bytes:
        """Encode one frame and return the bytes to transmit for it."""
        packet = self.encode(frame, captured_at=captured_at)
        if self._header_pushed:
            return packet.data
        self._header_pushed = True
        return self._header + packet.data

    def flush(self) -> bytes:
        """End the stream: return the end-of-stream packet (and any header not yet pushed)."""
        self._finished = True
        end = PREFIX.pack(MAGIC, END, 0, 0, 0, 0)
        if self._header is not None and not self._header_pushed:
            self._header_pushed = True
            return self._header + end
        return end


def _field(description, representation) -> StreamField:
    if not isinstance(description, dict) or set(description) != {"name", "dtype", "shape"}:
        raise ValueError("invalid O4S2 field description")
    name, dtype, shape = description["name"], description["dtype"], description["shape"]
    known = _GEOMETRIES[representation][1]
    if (not isinstance(name, str) or not 1 <= len(name) <= MAX_NAME
            or (name not in known and not (name.startswith(_ATTRIBUTE) and len(name) > len(_ATTRIBUTE)))):
        raise ValueError("invalid O4S2 field name")
    if not isinstance(dtype, str) or not _DTYPE.fullmatch(dtype):
        raise ValueError("invalid O4S2 field dtype")
    if (not isinstance(shape, list) or not 1 <= len(shape) <= MAX_DIMENSIONS
            or shape[0] is not None
            or any(type(size) is not int or not 0 <= size <= _MAX_DIMENSION for size in shape[1:])):
        raise ValueError("invalid O4S2 field shape")
    return StreamField(name, np.dtype(dtype), tuple(shape))


class StreamDecoder:
    """Decode O4S2 packets back into Frames as they arrive.

    ``feed(data)`` accepts any chunk of a byte stream and returns the frames it
    completed. ``decode(data)`` takes exactly one ``StreamEncoder.push`` result
    and returns its frame (None for header-only or end data). ``flush`` checks
    that no partial packet is pending, and ``reset`` accepts a new stream.
    ``max_frame_bytes`` bounds every payload and every frame's decoded arrays.
    """

    def __init__(self, *, max_frame_bytes: int = DEFAULT_MAX_FRAME_BYTES):
        self.max_frame_bytes = _limit(max_frame_bytes)
        self.reset()

    def reset(self) -> None:
        """Discard partial input and stream state."""
        self.header = None
        self.last_packet = None
        self.finished = False
        self._buffer = bytearray()
        self._profile = None
        self._options = None
        self._fields = None
        self._representation = None
        self._reference = None
        self._last_sequence = None
        self._decoded_sequence = None
        self._previous_timestamp = None
        self._failed = None

    @property
    def representation(self) -> str | None:
        return self._representation

    @property
    def fields(self) -> tuple[StreamField, ...] | None:
        return self._fields

    def _prefix(self, data):
        magic, kind, flags, zero, info, size = PREFIX.unpack(data)
        if magic != MAGIC:
            raise ValueError("not an O4S2 stream packet")
        if self.finished:
            raise ValueError("O4S2 data after the end of the stream")
        if zero or kind not in (HEADER, FRAME, END) or flags & ~KEYFRAME or (flags and kind != FRAME):
            raise ValueError("invalid O4S2 packet prefix")
        if kind == HEADER and (self.header is not None or size or not 0 < info <= MAX_HEADER_INFO):
            raise ValueError("invalid or repeated O4S2 stream header")
        if kind == END and (info or size):
            raise ValueError("invalid O4S2 end packet")
        if kind == FRAME:
            if self.header is None:
                raise ValueError("O4S2 frame before the stream header")
            if not 0 < info <= MAX_FRAME_INFO or size > self.max_frame_bytes:
                raise ValueError("O4S2 frame exceeds its information or max_frame_bytes limits")
        return kind, flags, info, size

    def _packet_size(self, kind, info, size):
        return PREFIX.size + (DESCRIPTOR.size if kind == FRAME else 0) + info + size

    def _read_header(self, info):
        header = _json(info, "stream header")
        if set(header) != {"stream", "version", "representation", "codec", "independent", "fields"}:
            raise ValueError("invalid O4S2 stream header keys")
        if header["stream"] != "O4S2" or type(header["version"]) is not int or header["version"] != 1:
            raise ValueError("unsupported O4S2 stream version")
        representation = header["representation"]
        if not isinstance(representation, str) or representation not in _GEOMETRIES:
            raise ValueError("invalid O4S2 representation")
        codec = header["codec"]
        if (not isinstance(codec, dict) or set(codec) != {"id", "version", "options"}
                or not isinstance(codec["id"], str) or codec["id"] not in PROFILES
                or not isinstance(codec["options"], dict)):
            raise ValueError("invalid O4S2 codec description")
        profile = PROFILES[codec["id"]]
        if type(codec["version"]) is not int or codec["version"] != profile.version:
            raise ValueError(f"unsupported {profile.id} profile version")
        if representation not in profile.representations:
            raise ValueError(f"the {profile.id} profile does not carry {representation}")
        options = profile.options(codec["options"])
        if header["independent"] is not profile.independent(options):
            raise ValueError("O4S2 header independence disagrees with its codec options")
        descriptions = header["fields"]
        if not isinstance(descriptions, list) or not 1 <= len(descriptions) <= MAX_FIELDS:
            raise ValueError("invalid O4S2 field list")
        fields = tuple(_field(description, representation) for description in descriptions)
        names = [field.name for field in fields]
        if len(set(names)) != len(names) or not _GEOMETRIES[representation][2] <= set(names):
            raise ValueError("O4S2 fields repeat or omit required geometry")
        self.header = header
        self._profile, self._options = profile, options
        self._fields, self._representation = fields, representation

    def _shapes(self, shapes):
        if not isinstance(shapes, list) or len(shapes) != len(self._fields):
            raise ValueError("O4S2 frame needs one shape per field")
        total = 0
        for field, shape in zip(self._fields, shapes):
            if shape is None:
                continue
            if (not isinstance(shape, list) or len(shape) != len(field.shape)
                    or type(shape[0]) is not int or not 0 <= shape[0] <= _MAX_DIMENSION
                    or tuple(shape[1:]) != field.shape[1:]):
                raise ValueError(f"invalid O4S2 shape for {field.name!r}")
            total += math.prod(shape) * field.dtype.itemsize
        if total > self.max_frame_bytes:
            raise ValueError("O4S2 frame shapes exceed max_frame_bytes")
        present = {field.name for field, shape in zip(self._fields, shapes) if shape is not None}
        if not _GEOMETRIES[self._representation][2] <= present:
            raise ValueError("O4S2 frame omits required geometry")
        return [None if shape is None else tuple(shape) for shape in shapes]

    def _read_frame(self, flags, descriptor, info, payload, wire_bytes):
        started = time.perf_counter()
        sequence, frame_index, timestamp, captured_at, dependency = DESCRIPTOR.unpack(descriptor)
        keyframe = bool(flags & KEYFRAME)
        if (frame_index > 2 ** 63 - 1 or sequence > 2 ** 63 - 1
                or not math.isfinite(timestamp) or not math.isfinite(captured_at)):
            raise ValueError("invalid O4S2 frame descriptor")
        if keyframe != (dependency == -1) or (self.header["independent"] and not keyframe):
            raise ValueError("O4S2 keyframe flag disagrees with its dependency")
        if self._last_sequence is not None and sequence <= self._last_sequence:
            raise ValueError("O4S2 sequence numbers must increase")
        if not keyframe and (dependency != self._decoded_sequence or self._reference is None):
            raise ValueError(f"O4S2 frame depends on packet {dependency}, which was not decoded")
        if self._previous_timestamp is not None and timestamp < self._previous_timestamp:
            raise ValueError("stream timestamps must be nondecreasing")
        document = _json(info, "frame information")
        if set(document) not in ({"metadata", "shapes", "codec"},
                                  {"metadata", "shapes", "codec", "attribute_domains"}):
            raise ValueError("invalid O4S2 frame information keys")
        if not isinstance(document["metadata"], dict) or not isinstance(document["codec"], dict):
            raise ValueError("invalid O4S2 frame metadata or codec information")
        shapes = self._shapes(document["shapes"])
        arrays = self._profile.decode(bytes(payload), document["codec"], self._fields, shapes,
                                      None if keyframe else self._reference, self._options)
        decoded_bytes = 0
        for field, shape, array in zip(self._fields, shapes, arrays):
            if (array is None) != (shape is None):
                raise ValueError(f"decoded presence of {field.name!r} disagrees with the frame")
            if array is None:
                continue
            if array.dtype != field.dtype or array.shape[1:] != field.shape[1:] or (
                    self._profile.exact_shapes and array.shape != shape):
                raise ValueError(f"decoded {field.name!r} disagrees with the stream schema")
            decoded_bytes += array.nbytes
        if decoded_bytes > self.max_frame_bytes:
            raise ValueError("decoded frame exceeds max_frame_bytes")
        values, attributes = {}, {}
        for field, array in zip(self._fields, arrays):
            if array is not None:
                if field.name.startswith(_ATTRIBUTE):
                    attributes[field.name[len(_ATTRIBUTE):]] = array
                else:
                    values[field.name] = array
        try:
            frame = Frame(frame_index, timestamp, _geometry(self._representation, values, attributes,
                          document.get("attribute_domains")),
                          document["metadata"])
        except (TypeError, ValueError) as error:
            raise ValueError(f"invalid O4S2 frame: {error}") from error
        if not self.header["independent"]:
            self._reference = [None if array is None else array.tobytes() for array in arrays]
        skipped = 0 if self._last_sequence is None else sequence - self._last_sequence - 1
        self._last_sequence = self._decoded_sequence = sequence
        self._previous_timestamp = timestamp
        self.last_packet = DecodedPacket(sequence, frame_index, timestamp, captured_at, keyframe,
                                         dependency, len(payload), wire_bytes, decoded_bytes,
                                         skipped, time.perf_counter() - started)
        return frame

    def _process(self, kind, flags, info, body):
        if kind == HEADER:
            self._read_header(body)
            return None
        if kind == END:
            self.finished = True
            return None
        descriptor = body[:DESCRIPTOR.size]
        info_end = DESCRIPTOR.size + info
        return self._read_frame(flags, descriptor, body[DESCRIPTOR.size:info_end],
                                body[info_end:], PREFIX.size + len(body))

    def _guard(self, action):
        if self._failed is not None:
            raise ValueError("the O4S2 decoder failed earlier; call reset()") from self._failed
        try:
            return action()
        except (ValueError, TypeError) as error:
            self._failed = error
            raise

    def read(self, read_exact) -> Frame | None:
        """Read packets with ``read_exact(size)`` until a frame or the stream end.

        Returns the next frame, or None once the end packet arrives. Header
        packets are consumed along the way. Every size is validated before it
        is read.
        """
        def action():
            while not self.finished:
                kind, flags, info, size = self._prefix(read_exact(PREFIX.size))
                body = read_exact(self._packet_size(kind, info, size) - PREFIX.size)
                frame = self._process(kind, flags, info, body)
                if frame is not None:
                    return frame
            return None
        return self._guard(action)

    def feed(self, data) -> list[Frame]:
        """Add bytes from the stream and return every frame they complete."""
        def action():
            self._buffer.extend(data)
            frames = []
            while len(self._buffer) >= PREFIX.size:
                kind, flags, info, size = self._prefix(self._buffer[:PREFIX.size])
                total = self._packet_size(kind, info, size)
                if len(self._buffer) < total:
                    break
                body = bytes(self._buffer[PREFIX.size:total])
                del self._buffer[:total]
                frame = self._process(kind, flags, info, body)
                if frame is not None:
                    frames.append(frame)
            return frames
        return self._guard(action)

    def decode(self, data) -> Frame | None:
        """Decode exactly one ``StreamEncoder.push`` or ``flush`` result."""
        frames = self.feed(data)
        if self._buffer:
            raise ValueError("incomplete O4S2 packet")
        if len(frames) > 1:
            raise ValueError("decode() takes one frame's packets; use feed() for a byte stream")
        return frames[0] if frames else None

    def flush(self) -> list[Frame]:
        """Confirm that no partial packet is pending; independent profiles buffer no frames."""
        if self._buffer:
            raise ValueError("incomplete O4S2 packet at the end of the input")
        return []


def splats_to_frame(splats, frame_index: int, timestamp: float, metadata=None) -> Frame:
    """Wrap GaussianSplats as a streamable GaussianCloud Frame.

    The full (N, K, 3) spherical-harmonic coefficients travel unchanged as the
    ``spherical_harmonics`` attribute; ``colors`` holds the clipped degree-0
    color so viewers that ignore SH still show the cloud. Use `frame_to_splats`
    on the receiving side.
    """
    from open4d.core import GaussianCloud

    sh = splats.spherical_harmonics
    colors = np.clip(0.5 + 0.28209479177387814 * sh[:, 0, :], 0.0, 1.0)
    cloud = GaussianCloud(splats.positions, splats.scales, splats.rotations, splats.opacities,
                          colors=colors, attributes={"spherical_harmonics": sh})
    object.__setattr__(cloud, "rotations", splats.rotations)
    return Frame(frame_index, timestamp, cloud, metadata or {})


def frame_to_splats(frame: Frame):
    """Rebuild GaussianSplats from a frame made by `splats_to_frame`."""
    from open4d.gaussians import GaussianSplats

    cloud = frame.geometry
    if cloud.representation.value != "gaussians" or "spherical_harmonics" not in cloud.attributes:
        raise ValueError("frame does not carry Gaussian spherical harmonics")
    splats = GaussianSplats(cloud.positions, cloud.scales, cloud.rotations, cloud.opacities,
                            cloud.attributes["spherical_harmonics"])
    object.__setattr__(splats, "rotations", cloud.rotations)
    return splats
