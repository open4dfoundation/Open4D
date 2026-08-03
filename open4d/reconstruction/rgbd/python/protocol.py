"""Versioned framed protocol for paired Orbbec RGB-D transport."""

from __future__ import annotations

import dataclasses
import socket
import struct
import time
import zlib


MAGIC = b"OBP1"
ACK_MAGIC = b"OBA1"
VERSION = 1

FLAG_HARDWARE_SYNC = 1 << 0
FLAG_DEVICE_TIMESTAMPS = 1 << 1

STREAM_COLOR = 1
STREAM_DEPTH = 2

CODEC_MJPEG = 1
CODEC_ZSTD = 2

FORMAT_COLOR_MJPG = 0
FORMAT_DEPTH16_LE = 4

MAX_PAYLOADS = 8
MAX_HEADER_SIZE = 4096
MAX_TOTAL_PAYLOAD = 32 * 1024 * 1024
MAX_SINGLE_PAYLOAD = 12 * 1024 * 1024
MAX_RAW_PAYLOAD = 16 * 1024 * 1024

# magic, version, header_size, flags, pair_number, sender_wallclock_ns,
# ey_timestamp_us, j3_timestamp_us, sync_error_us, payload_count,
# total_payload_bytes, queue_dropped_total, header_crc32
FRAME_HEADER = struct.Struct("<4sHHIQQQQqIIII")

# serial, stream_type, codec, width, height, format, raw_length,
# compressed_length, payload_crc32, reserved, device_timestamp_us
PAYLOAD_DESCRIPTOR = struct.Struct("<16sBBHHHIIIIQ")

# magic, version, ack_size, pair_number, receiver_wallclock_ns, crc32
ACK = struct.Struct("<4sHHQQI")


class ProtocolError(RuntimeError):
    pass


@dataclasses.dataclass(frozen=True)
class Payload:
    serial: str
    stream_type: int
    codec: int
    width: int
    height: int
    format: int
    raw_length: int
    device_timestamp_us: int
    data: bytes

    def descriptor_bytes(self) -> bytes:
        serial_bytes = self.serial.encode("ascii")
        if len(serial_bytes) > 15:
            raise ProtocolError(f"serial is too long: {self.serial}")
        if not self.data or len(self.data) > MAX_SINGLE_PAYLOAD:
            raise ProtocolError(f"invalid compressed payload size: {len(self.data)}")
        if self.raw_length <= 0 or self.raw_length > MAX_RAW_PAYLOAD:
            raise ProtocolError(f"invalid raw payload size: {self.raw_length}")
        return PAYLOAD_DESCRIPTOR.pack(
            serial_bytes.ljust(16, b"\0"),
            self.stream_type,
            self.codec,
            self.width,
            self.height,
            self.format,
            self.raw_length,
            len(self.data),
            zlib.crc32(self.data),
            0,
            self.device_timestamp_us,
        )


@dataclasses.dataclass(frozen=True)
class Frame:
    pair_number: int
    sender_wallclock_ns: int
    ey_timestamp_us: int
    j3_timestamp_us: int
    sync_error_us: int
    flags: int
    queue_dropped_total: int
    payloads: tuple[Payload, ...]


@dataclasses.dataclass(frozen=True)
class ReceivedPayload:
    serial: str
    stream_type: int
    codec: int
    width: int
    height: int
    format: int
    raw_length: int
    compressed_length: int
    payload_crc32: int
    device_timestamp_us: int
    data: bytes


@dataclasses.dataclass(frozen=True)
class ReceivedFrame:
    pair_number: int
    sender_wallclock_ns: int
    ey_timestamp_us: int
    j3_timestamp_us: int
    sync_error_us: int
    flags: int
    queue_dropped_total: int
    payloads: tuple[ReceivedPayload, ...]
    wire_bytes: int


def encode_frame(frame: Frame) -> bytes:
    if not 1 <= len(frame.payloads) <= MAX_PAYLOADS:
        raise ProtocolError("invalid payload count")
    descriptor_bytes = b"".join(
        payload.descriptor_bytes() for payload in frame.payloads
    )
    payload_bytes = b"".join(payload.data for payload in frame.payloads)
    if len(payload_bytes) > MAX_TOTAL_PAYLOAD:
        raise ProtocolError("frame exceeds maximum total payload")

    header_size = FRAME_HEADER.size + len(descriptor_bytes)
    fixed_without_crc = FRAME_HEADER.pack(
        MAGIC,
        VERSION,
        header_size,
        frame.flags,
        frame.pair_number,
        frame.sender_wallclock_ns,
        frame.ey_timestamp_us,
        frame.j3_timestamp_us,
        frame.sync_error_us,
        len(frame.payloads),
        len(payload_bytes),
        frame.queue_dropped_total,
        0,
    )
    header_crc = zlib.crc32(fixed_without_crc + descriptor_bytes)
    fixed = FRAME_HEADER.pack(
        MAGIC,
        VERSION,
        header_size,
        frame.flags,
        frame.pair_number,
        frame.sender_wallclock_ns,
        frame.ey_timestamp_us,
        frame.j3_timestamp_us,
        frame.sync_error_us,
        len(frame.payloads),
        len(payload_bytes),
        frame.queue_dropped_total,
        header_crc,
    )
    return fixed + descriptor_bytes + payload_bytes


def recv_exact(sock: socket.socket, size: int) -> bytes:
    if size < 0:
        raise ProtocolError("negative receive size")
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise EOFError("peer disconnected")
        chunks.extend(chunk)
    return bytes(chunks)


def receive_frame(sock: socket.socket) -> ReceivedFrame:
    fixed = recv_exact(sock, FRAME_HEADER.size)
    fields = FRAME_HEADER.unpack(fixed)
    (
        magic,
        version,
        header_size,
        flags,
        pair_number,
        sender_wallclock_ns,
        ey_timestamp_us,
        j3_timestamp_us,
        sync_error_us,
        payload_count,
        total_payload,
        queue_dropped_total,
        header_crc,
    ) = fields

    if magic != MAGIC or version != VERSION:
        raise ProtocolError("bad frame magic or version")
    expected_header_size = (
        FRAME_HEADER.size + payload_count * PAYLOAD_DESCRIPTOR.size
    )
    if (
        payload_count < 1
        or payload_count > MAX_PAYLOADS
        or header_size != expected_header_size
        or header_size > MAX_HEADER_SIZE
    ):
        raise ProtocolError("invalid frame header size or payload count")
    if total_payload > MAX_TOTAL_PAYLOAD:
        raise ProtocolError("frame payload exceeds bound")

    descriptor_bytes = recv_exact(sock, header_size - FRAME_HEADER.size)
    fixed_without_crc = FRAME_HEADER.pack(*fields[:-1], 0)
    if zlib.crc32(fixed_without_crc + descriptor_bytes) != header_crc:
        raise ProtocolError("header CRC mismatch")

    descriptors = []
    compressed_sum = 0
    for index in range(payload_count):
        start = index * PAYLOAD_DESCRIPTOR.size
        values = PAYLOAD_DESCRIPTOR.unpack_from(descriptor_bytes, start)
        (
            serial_bytes,
            stream_type,
            codec,
            width,
            height,
            format_code,
            raw_length,
            compressed_length,
            payload_crc,
            _reserved,
            device_timestamp_us,
        ) = values
        if (
            compressed_length <= 0
            or compressed_length > MAX_SINGLE_PAYLOAD
            or raw_length <= 0
            or raw_length > MAX_RAW_PAYLOAD
        ):
            raise ProtocolError("payload length exceeds bound")
        compressed_sum += compressed_length
        descriptors.append(
            (
                serial_bytes.split(b"\0", 1)[0].decode("ascii"),
                stream_type,
                codec,
                width,
                height,
                format_code,
                raw_length,
                compressed_length,
                payload_crc,
                device_timestamp_us,
            )
        )
    if compressed_sum != total_payload:
        raise ProtocolError("descriptor lengths do not match frame payload")

    payload_blob = recv_exact(sock, total_payload)
    payloads = []
    offset = 0
    for descriptor in descriptors:
        compressed_length = descriptor[7]
        data = payload_blob[offset : offset + compressed_length]
        offset += compressed_length
        if zlib.crc32(data) != descriptor[8]:
            raise ProtocolError("payload CRC mismatch")
        payloads.append(
            ReceivedPayload(
                serial=descriptor[0],
                stream_type=descriptor[1],
                codec=descriptor[2],
                width=descriptor[3],
                height=descriptor[4],
                format=descriptor[5],
                raw_length=descriptor[6],
                compressed_length=descriptor[7],
                payload_crc32=descriptor[8],
                device_timestamp_us=descriptor[9],
                data=data,
            )
        )

    return ReceivedFrame(
        pair_number=pair_number,
        sender_wallclock_ns=sender_wallclock_ns,
        ey_timestamp_us=ey_timestamp_us,
        j3_timestamp_us=j3_timestamp_us,
        sync_error_us=sync_error_us,
        flags=flags,
        queue_dropped_total=queue_dropped_total,
        payloads=tuple(payloads),
        wire_bytes=header_size + total_payload,
    )


def encode_ack(pair_number: int, receiver_wallclock_ns: int | None = None) -> bytes:
    receiver_wallclock_ns = receiver_wallclock_ns or time.time_ns()
    without_crc = ACK.pack(
        ACK_MAGIC,
        VERSION,
        ACK.size,
        pair_number,
        receiver_wallclock_ns,
        0,
    )
    crc = zlib.crc32(without_crc)
    return ACK.pack(
        ACK_MAGIC,
        VERSION,
        ACK.size,
        pair_number,
        receiver_wallclock_ns,
        crc,
    )


def receive_ack(sock: socket.socket, expected_pair_number: int) -> int:
    data = recv_exact(sock, ACK.size)
    pair_number, receiver_wallclock_ns = decode_ack(data)
    if pair_number != expected_pair_number:
        raise ProtocolError("ACK pair number does not match")
    return receiver_wallclock_ns


def decode_ack(data: bytes) -> tuple[int, int]:
    if len(data) != ACK.size:
        raise ProtocolError("invalid ACK size")
    magic, version, size, pair_number, receiver_wallclock_ns, crc = ACK.unpack(
        data
    )
    if (
        magic != ACK_MAGIC
        or version != VERSION
        or size != ACK.size
    ):
        raise ProtocolError("invalid ACK")
    without_crc = ACK.pack(
        magic, version, size, pair_number, receiver_wallclock_ns, 0
    )
    if zlib.crc32(without_crc) != crc:
        raise ProtocolError("ACK CRC mismatch")
    return pair_number, receiver_wallclock_ns
