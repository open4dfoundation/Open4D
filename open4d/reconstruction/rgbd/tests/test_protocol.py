"""OBP1 framing and ACK edge cases over local socket pairs.

protocol.py is a standard-library script, so these tests need neither OpenCV
nor Open3D. Every malformed frame keeps a valid header CRC unless the CRC is
what is under test, so each check is reached on its own.
"""

import dataclasses
import importlib.util
import socket
import struct
import sys
import threading
import zlib
from pathlib import Path

import pytest

PROTOCOL = Path(__file__).resolve().parents[1] / "python" / "protocol.py"
if not PROTOCOL.is_file():
    pytest.skip("needs a source checkout with reconstruction/rgbd/python", allow_module_level=True)


def load_protocol():
    # Load by path under a private name, leaving no generic "protocol" module behind.
    spec = importlib.util.spec_from_file_location("_open4d_test_obp1_protocol", PROTOCOL)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module  # dataclasses resolve their module while executing
    try:
        spec.loader.exec_module(module)
    finally:
        del sys.modules[spec.name]
    return module


protocol = load_protocol()
HEADER = protocol.FRAME_HEADER
DESCRIPTOR = protocol.PAYLOAD_DESCRIPTOR
# Header field positions.
MAGIC, VERSION, HEADER_SIZE, PAYLOAD_COUNT, TOTAL = 0, 1, 2, 9, 10
# Descriptor field positions.
COMPRESSED, RAW, PAYLOAD_CRC = 7, 6, 8

COLOR = b"\xff\xd8 synthetic jpeg \xff\xd9"
DEPTH = b"(\xb5/\xfd synthetic zstd depth"


def payload(serial="CAMERA1", stream=protocol.STREAM_COLOR, data=COLOR, **changes):
    color = stream == protocol.STREAM_COLOR
    values = {
        "serial": serial, "stream_type": stream,
        "codec": protocol.CODEC_MJPEG if color else protocol.CODEC_ZSTD,
        "width": 1280 if color else 640, "height": 720 if color else 576,
        "format": protocol.FORMAT_COLOR_MJPG if color else protocol.FORMAT_DEPTH16_LE,
        "raw_length": len(data) if color else 640 * 576 * 2,
        "device_timestamp_us": 1_000_123, "data": data,
    }
    return protocol.Payload(**{**values, **changes})


def frame(payloads=None, **changes):
    if payloads is None:
        payloads = (payload(), payload(stream=protocol.STREAM_DEPTH, data=DEPTH),
                    payload("CAMERA2", data=COLOR[::-1]),
                    payload("CAMERA2", protocol.STREAM_DEPTH, DEPTH[::-1]))
    values = {"pair_number": 42, "sender_wallclock_ns": 1_700_000_000_123_456_789,
              "ey_timestamp_us": 1_000_123, "j3_timestamp_us": 1_000_283, "sync_error_us": -160,
              "flags": protocol.FLAG_HARDWARE_SYNC | protocol.FLAG_DEVICE_TIMESTAMPS,
              "queue_dropped_total": 3, "payloads": tuple(payloads)}
    return protocol.Frame(**{**values, **changes})


def rewrite(encoded, *, crc=True, descriptors=(), **fields):
    """Change header fields and descriptors of an encoded frame.

    fields maps header positions (as ``f<index>``) to values; descriptors is a
    sequence of (index, position, value). The header CRC is recomputed over the
    descriptors the header now declares unless crc=False.
    """
    data = bytearray(encoded)
    values = list(HEADER.unpack_from(data))
    for name, value in fields.items():
        values[int(name[1:])] = value
    for index, position, value in descriptors:
        start = HEADER.size + index * DESCRIPTOR.size
        record = list(DESCRIPTOR.unpack_from(data, start))
        record[position] = value
        data[start:start + DESCRIPTOR.size] = DESCRIPTOR.pack(*record)
    if crc:
        described = data[HEADER.size:values[HEADER_SIZE]]
        values[-1] = zlib.crc32(HEADER.pack(*values[:-1], 0) + described)
    data[:HEADER.size] = HEADER.pack(*values)
    return bytes(data)


@pytest.fixture
def link():
    """Return send(data, close=True) -> receiving socket; a thread does the sending.

    A sender thread keeps large frames from deadlocking on the socket buffer.
    close=False leaves the connection open after the data, as a stalled peer.
    """
    sockets, threads = [], []

    def send(data, close=True, timeout=5.0):
        receiver, sender = socket.socketpair()
        receiver.settimeout(timeout)
        sockets.extend((receiver, sender))

        def run():
            try:
                sender.sendall(data)
                if close:
                    sender.shutdown(socket.SHUT_WR)
            except OSError:
                pass  # The receiver rejected the frame and closed early.

        thread = threading.Thread(target=run, daemon=True)
        thread.start()
        threads.append(thread)
        return receiver

    yield send
    for item in sockets:
        item.close()
    for thread in threads:
        thread.join(5)
        assert not thread.is_alive()


def assert_received(received, sent, wire):
    assert received.wire_bytes == len(wire)
    for name in ("pair_number", "sender_wallclock_ns", "ey_timestamp_us", "j3_timestamp_us",
                 "sync_error_us", "flags", "queue_dropped_total"):
        assert getattr(received, name) == getattr(sent, name), name
    assert len(received.payloads) == len(sent.payloads)
    for got, expected in zip(received.payloads, sent.payloads):
        for name in ("serial", "stream_type", "codec", "width", "height", "format",
                     "raw_length", "device_timestamp_us", "data"):
            assert getattr(got, name) == getattr(expected, name), name
        assert got.compressed_length == len(expected.data)
        assert got.payload_crc32 == zlib.crc32(expected.data)


def test_frame_round_trip_with_colour_and_depth_payloads(link):
    sent = frame()
    wire = protocol.encode_frame(sent)
    assert len(wire) == HEADER.size + 4 * DESCRIPTOR.size + sum(len(p.data) for p in sent.payloads)
    receiver = link(wire)
    assert_received(protocol.receive_frame(receiver), sent, wire)
    # The frame ends exactly where the next one would start.
    with pytest.raises(EOFError):
        protocol.receive_frame(receiver)


def test_back_to_back_frames_stay_in_step(link):
    frames = [frame(pair_number=number) for number in range(3)]
    receiver = link(b"".join(protocol.encode_frame(item) for item in frames))
    assert [protocol.receive_frame(receiver).pair_number for _ in frames] == [0, 1, 2]


def test_frame_at_size_limits_round_trips(link):
    # The most payloads, a 15-character serial and a payload of the largest size.
    largest = payload("ABCDEFGHIJKLMNO", protocol.STREAM_DEPTH,
                      bytes(range(256)) * (protocol.MAX_SINGLE_PAYLOAD // 256),
                      raw_length=protocol.MAX_RAW_PAYLOAD)
    payloads = [largest] + [payload(f"CAM{index}") for index in range(protocol.MAX_PAYLOADS - 1)]
    sent = frame(payloads)
    wire = protocol.encode_frame(sent)
    assert_received(protocol.receive_frame(link(wire)), sent, wire)


def test_receive_frame_strips_serial_padding(link):
    wire = protocol.encode_frame(frame([payload("A")]))
    serial = DESCRIPTOR.unpack_from(wire, HEADER.size)[0]
    assert serial == b"A" + b"\0" * 15
    assert protocol.receive_frame(link(wire)).payloads[0].serial == "A"


def cut_points():
    wire = protocol.encode_frame(frame())
    header_size = HEADER.unpack_from(wire)[HEADER_SIZE]
    return {
        "nothing": 0, "inside header": 7, "header only": HEADER.size,
        "inside descriptor": HEADER.size + DESCRIPTOR.size // 2,
        "between descriptors": HEADER.size + DESCRIPTOR.size,
        "descriptors only": header_size, "inside first payload": header_size + 3,
        "one byte short": len(wire) - 1,
    }


@pytest.mark.parametrize("cut", cut_points().values(), ids=cut_points().keys())
def test_disconnect_mid_frame_raises_eof(link, cut):
    wire = protocol.encode_frame(frame())
    with pytest.raises(EOFError, match="peer disconnected"):
        protocol.receive_frame(link(wire[:cut]))


@pytest.mark.parametrize("fields", [
    {f"f{MAGIC}": b"OBP2"},
    {f"f{MAGIC}": protocol.ACK_MAGIC},
    {f"f{VERSION}": protocol.VERSION + 1},
    {f"f{VERSION}": 0},
])
def test_bad_magic_or_version_is_rejected_before_the_body(link, fields):
    wire = rewrite(protocol.encode_frame(frame()), **fields)
    # Only the fixed header arrives and the peer stays connected: the receiver
    # must decide from those bytes alone instead of waiting for more.
    with pytest.raises(protocol.ProtocolError, match="bad frame magic or version"):
        protocol.receive_frame(link(wire[:HEADER.size], close=False, timeout=2))


def test_header_crc_covers_fixed_fields_and_descriptors(link):
    wire = bytearray(protocol.encode_frame(frame()))
    for offset in (12, 30, HEADER.size - 5, HEADER.size + 2, HEADER.size + 2 * DESCRIPTOR.size - 1):
        corrupt = bytearray(wire)
        corrupt[offset] ^= 0x01
        with pytest.raises(protocol.ProtocolError, match="header CRC mismatch"):
            protocol.receive_frame(link(bytes(corrupt)))
    corrupt = rewrite(bytes(wire), crc=False, f12=0xDEADBEEF)
    with pytest.raises(protocol.ProtocolError, match="header CRC mismatch"):
        protocol.receive_frame(link(corrupt))


@pytest.mark.parametrize("which", [0, 3])
def test_payload_crc_mismatch(link, which):
    sent = frame()
    wire = bytearray(protocol.encode_frame(sent))
    header_size = HEADER.unpack_from(wire)[HEADER_SIZE]
    offset = header_size + sum(len(p.data) for p in sent.payloads[:which])
    wire[offset] ^= 0x80
    with pytest.raises(protocol.ProtocolError, match="payload CRC mismatch"):
        protocol.receive_frame(link(bytes(wire)))
    wire = rewrite(protocol.encode_frame(sent), descriptors=[(which, PAYLOAD_CRC, 0)])
    with pytest.raises(protocol.ProtocolError, match="payload CRC mismatch"):
        protocol.receive_frame(link(wire))


@pytest.mark.parametrize("count,header_size", [
    (0, HEADER.size),
    (protocol.MAX_PAYLOADS + 1, HEADER.size + (protocol.MAX_PAYLOADS + 1) * DESCRIPTOR.size),
    (2 ** 32 - 1, HEADER.size),
    (4, HEADER.size + 3 * DESCRIPTOR.size),       # declares one descriptor too few
    (4, HEADER.size + 5 * DESCRIPTOR.size),       # and one too many
    (4, protocol.MAX_HEADER_SIZE + 1),            # oversized header
    (4, 2 ** 16 - 1),
])
def test_payload_count_and_header_size_are_bounded(link, count, header_size):
    wire = rewrite(protocol.encode_frame(frame()), crc=False,
                   **{f"f{PAYLOAD_COUNT}": count, f"f{HEADER_SIZE}": header_size})
    with pytest.raises(protocol.ProtocolError, match="invalid frame header size or payload count"):
        protocol.receive_frame(link(wire[:HEADER.size], close=False, timeout=2))


def test_total_payload_bound_is_checked_before_reading_descriptors(link):
    wire = rewrite(protocol.encode_frame(frame()), crc=False,
                   **{f"f{TOTAL}": protocol.MAX_TOTAL_PAYLOAD + 1})
    with pytest.raises(protocol.ProtocolError, match="frame payload exceeds bound"):
        protocol.receive_frame(link(wire[:HEADER.size], close=False, timeout=2))


@pytest.mark.parametrize("position,value", [
    (COMPRESSED, 0),
    (COMPRESSED, protocol.MAX_SINGLE_PAYLOAD + 1),
    (RAW, 0),
    (RAW, protocol.MAX_RAW_PAYLOAD + 1),
])
def test_single_payload_bounds_are_checked_before_reading_payloads(link, position, value):
    sent = frame([payload()])
    original = protocol.encode_frame(sent)
    total = value if position == COMPRESSED else len(COLOR)
    wire = rewrite(original, descriptors=[(0, position, value)], **{f"f{TOTAL}": total})
    header_size = HEADER.unpack_from(wire)[HEADER_SIZE]
    with pytest.raises(protocol.ProtocolError, match="payload length exceeds bound"):
        protocol.receive_frame(link(wire[:header_size], close=False, timeout=2))


@pytest.mark.parametrize("delta", [-1, 1])
def test_descriptor_lengths_must_sum_to_total_payload(link, delta):
    original = protocol.encode_frame(frame())
    total = HEADER.unpack_from(original)[TOTAL]
    wire = rewrite(original, **{f"f{TOTAL}": total + delta})
    header_size = HEADER.unpack_from(wire)[HEADER_SIZE]
    with pytest.raises(protocol.ProtocolError, match="descriptor lengths do not match"):
        protocol.receive_frame(link(wire[:header_size], close=False, timeout=2))
    # Moving bytes between payloads keeps the sum, so the payload CRCs catch it.
    first = DESCRIPTOR.unpack_from(original, HEADER.size)[COMPRESSED]
    second = DESCRIPTOR.unpack_from(original, HEADER.size + DESCRIPTOR.size)[COMPRESSED]
    wire = rewrite(original, descriptors=[(0, COMPRESSED, first + delta),
                                          (1, COMPRESSED, second - delta)])
    with pytest.raises(protocol.ProtocolError, match="payload CRC mismatch"):
        protocol.receive_frame(link(wire))


def test_non_ascii_serial_on_the_wire_is_a_protocol_error(link):
    wire = rewrite(protocol.encode_frame(frame([payload()])),
                   descriptors=[(0, 0, b"CAM\xe9RA".ljust(16, b"\0"))])
    with pytest.raises(protocol.ProtocolError, match="invalid camera serial"):
        protocol.receive_frame(link(wire))


@pytest.mark.parametrize("payloads,match", [
    ((), "invalid payload count"),
    ((payload(),) * (protocol.MAX_PAYLOADS + 1), "invalid payload count"),
    ((payload(data=b""),), "invalid compressed payload size: 0"),
    ((payload(data=bytes(protocol.MAX_SINGLE_PAYLOAD + 1)),), "invalid compressed payload size"),
    ((payload(raw_length=0),), "invalid raw payload size"),
    ((payload(raw_length=protocol.MAX_RAW_PAYLOAD + 1),), "invalid raw payload size"),
    ((payload("ABCDEFGHIJKLMNOP"),), "serial is too long"),
    ((payload("CAMÉRA"),), "serial is not ASCII"),
    ((payload(data=bytes(protocol.MAX_SINGLE_PAYLOAD)),) * 3, "exceeds maximum total payload"),
])
def test_encode_frame_rejects_invalid_payloads(payloads, match):
    with pytest.raises(protocol.ProtocolError, match=match):
        protocol.encode_frame(frame(payloads))


@pytest.mark.parametrize("changes", [
    {"pair_number": -1},
    {"pair_number": 2 ** 64},
    {"flags": 2 ** 32},
    {"sync_error_us": 2 ** 63},
    {"queue_dropped_total": -1},
    {"payloads": (payload(width=2 ** 16),)},
    {"payloads": (payload(stream_type=256),)},
    {"payloads": (payload(device_timestamp_us=-1),)},
])
def test_encode_frame_reports_out_of_range_fields_as_protocol_errors(changes):
    # These used to escape as struct.error, which the sender does not handle.
    with pytest.raises(protocol.ProtocolError, match="out of range") as caught:
        protocol.encode_frame(frame(**changes))
    assert isinstance(caught.value.__cause__, struct.error)


def test_ack_round_trip(link):
    wire = protocol.encode_ack(42, 1_700_000_000_000_000_001)
    assert len(wire) == protocol.ACK.size
    assert protocol.decode_ack(wire) == (42, 1_700_000_000_000_000_001)
    assert protocol.receive_ack(link(wire), 42) == 1_700_000_000_000_000_001


def test_ack_keeps_an_explicit_zero_timestamp(monkeypatch):
    monkeypatch.setattr(protocol.time, "time_ns", lambda: 123)
    assert protocol.decode_ack(protocol.encode_ack(1)) == (1, 123)
    # 0 is a valid receiver clock value, not "use the current time".
    assert protocol.decode_ack(protocol.encode_ack(1, 0)) == (1, 0)


def flip(data, offset):
    return data[:offset] + bytes([data[offset] ^ 0x01]) + data[offset + 1:]


def ack_with(**fields):
    names = ("magic", "version", "size", "pair_number", "receiver_wallclock_ns")
    values = dict(zip(names, protocol.ACK.unpack(protocol.encode_ack(7, 99))[:5]))
    values.update(fields)
    body = protocol.ACK.pack(*values.values(), 0)
    return protocol.ACK.pack(*values.values(), zlib.crc32(body))


@pytest.mark.parametrize("data,match", [
    (ack_with(magic=protocol.MAGIC), "invalid ACK"),
    (ack_with(magic=b"OBA2"), "invalid ACK"),
    (ack_with(version=protocol.VERSION + 1), "invalid ACK"),
    (ack_with(size=protocol.ACK.size + 1), "invalid ACK"),
    (flip(protocol.encode_ack(7, 99), 10), "ACK CRC mismatch"),   # pair number
    (flip(protocol.encode_ack(7, 99), 20), "ACK CRC mismatch"),   # receiver clock
    (flip(protocol.encode_ack(7, 99), 25), "ACK CRC mismatch"),   # the CRC itself
])
def test_bad_ack_is_rejected(link, data, match):
    with pytest.raises(protocol.ProtocolError, match=match):
        protocol.decode_ack(data)
    with pytest.raises(protocol.ProtocolError, match=match):
        protocol.receive_ack(link(data), 7)


@pytest.mark.parametrize("size", [0, protocol.ACK.size - 1, protocol.ACK.size + 1])
def test_decode_ack_rejects_wrong_length(size):
    data = (protocol.encode_ack(7, 99) * 2)[:size]
    with pytest.raises(protocol.ProtocolError, match="invalid ACK size"):
        protocol.decode_ack(data)


def test_ack_for_another_pair_is_rejected(link):
    with pytest.raises(protocol.ProtocolError, match="ACK pair number does not match"):
        protocol.receive_ack(link(protocol.encode_ack(8, 99)), 7)


def test_truncated_ack_raises_eof(link):
    with pytest.raises(EOFError):
        protocol.receive_ack(link(protocol.encode_ack(7, 99)[:-1]), 7)


def test_encode_ack_reports_out_of_range_pair_as_protocol_error():
    for pair_number in (-1, 2 ** 64):
        with pytest.raises(protocol.ProtocolError, match="out of range"):
            protocol.encode_ack(pair_number, 1)


def test_idle_peer_times_out(link):
    with pytest.raises(TimeoutError):
        protocol.receive_frame(link(b"", close=False, timeout=0.05))
    with pytest.raises(TimeoutError):
        protocol.receive_ack(link(b"", close=False, timeout=0.05), 1)


def test_timeout_mid_frame_leaves_the_stream_unusable():
    # recv_exact consumes what arrived before the timeout, so the connection
    # cannot resynchronize; the live receiver drops it, and so must callers.
    wire = protocol.encode_frame(frame())
    split = HEADER.size + 10
    receiver, sender = socket.socketpair()
    with receiver, sender:
        receiver.settimeout(0.05)
        sender.sendall(wire[:split])
        with pytest.raises(TimeoutError):
            protocol.receive_frame(receiver)
        sender.sendall(wire[split:])  # the late remainder fits the socket buffer
        receiver.settimeout(2)
        with pytest.raises(protocol.ProtocolError, match="bad frame magic"):
            protocol.receive_frame(receiver)


def test_recv_exact_sizes(link):
    receiver = link(b"abcdef")
    assert protocol.recv_exact(receiver, 0) == b""
    assert protocol.recv_exact(receiver, 4) == b"abcd"
    with pytest.raises(protocol.ProtocolError, match="negative receive size"):
        protocol.recv_exact(receiver, -1)
    with pytest.raises(EOFError):
        protocol.recv_exact(receiver, 3)


def test_received_frame_dataclasses_are_frozen(link):
    received = protocol.receive_frame(link(protocol.encode_frame(frame())))
    with pytest.raises(dataclasses.FrozenInstanceError):
        received.pair_number = 1


def test_packaged_capture_constants_match_protocol():
    # load_capture reads saved OBP1 payload metadata with a packaged copy of
    # these values, because protocol.py is not installed with the wheel.
    from open4d.reconstruction.rgbd import _receiver

    for name in ("STREAM_COLOR", "STREAM_DEPTH", "CODEC_MJPEG", "CODEC_ZSTD",
                 "FORMAT_DEPTH16_LE", "MAX_SINGLE_PAYLOAD"):
        assert getattr(_receiver, name) == getattr(protocol, name), name
