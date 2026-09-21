import argparse
import dataclasses
import importlib
import socket
import sys
import time
import zlib
from pathlib import Path

import pytest


@pytest.fixture
def protocol(monkeypatch):
    root = Path(__file__).resolve().parents[1] / "python"
    monkeypatch.syspath_prepend(str(root))
    module = importlib.import_module("protocol")
    yield module
    sys.modules.pop("protocol", None)


@pytest.fixture
def fusion(protocol):
    pytest.importorskip("cv2")
    pytest.importorskip("open3d")
    pytest.importorskip("zstandard")
    module = importlib.import_module("live_two_camera_fusion")
    yield module
    sys.modules.pop("live_two_camera_fusion", None)


def server_args(port=0):
    return argparse.Namespace(bind="127.0.0.1", port=port, allow_nonloopback=False,
                              socket_timeout=30, delay_usec=0, sync_tolerance_usec=100,
                              max_pairs=0)


def test_capture_close_interrupts_partial_frame_and_releases_listener(fusion):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        address = listener.getsockname()
    server = fusion.FrameServer(server_args(address[1]))
    server.start()
    # Wait until accept has entered the receiver's blocking read.
    with socket.create_connection(address, timeout=3) as sender:
        sender.sendall(b"OB")
        deadline = time.monotonic() + 3
        while not server.connections and time.monotonic() < deadline:
            time.sleep(0.005)
        started = time.monotonic()
        server.close()
        assert not server.thread.is_alive()
        assert time.monotonic() - started < 1
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(address)


def test_capture_rejects_oversized_zstd_before_decompression(fusion, protocol):
    import zstandard

    frame = protocol.ReceivedFrame(1, 0, 0, 0, 0, 3, 0, (), 0)
    payloads = []
    for serial in (fusion.EY_SERIAL, fusion.J3_SERIAL):
        payloads.append(protocol.ReceivedPayload(serial, protocol.STREAM_COLOR, protocol.CODEC_MJPEG,
                         1280, 720, protocol.FORMAT_COLOR_MJPG, 4, 4, 0, 0, b"\xff\xd8xx"))
        data = zstandard.ZstdCompressor().compress(bytes(fusion.DEPTH_BYTES + 1))
        payloads.append(protocol.ReceivedPayload(serial, protocol.STREAM_DEPTH, protocol.CODEC_ZSTD,
                         fusion.WIDTH, fusion.HEIGHT, protocol.FORMAT_DEPTH16_LE,
                         fusion.DEPTH_BYTES, len(data), 0, 0, data))
    server = fusion.FrameServer(server_args())

    class UnexpectedDecode:
        def decompress(self, *args, **kwargs):
            pytest.fail("oversized content reached the decompressor")

    server.decompressor = UnexpectedDecode()
    with pytest.raises(protocol.ProtocolError, match="depth size"):
        server._validate_and_decode(dataclasses.replace(frame, payloads=tuple(payloads)))


def test_protocol_reports_non_ascii_serial_as_frame_error(protocol):
    payload = protocol.Payload("camera", protocol.STREAM_DEPTH, protocol.CODEC_ZSTD,
                               1, 1, protocol.FORMAT_DEPTH16_LE, 2, 0, b"xx")
    encoded = bytearray(protocol.encode_frame(protocol.Frame(1, 0, 0, 0, 0, 0, 0, (payload,))))
    encoded[protocol.FRAME_HEADER.size] = 255
    fields = protocol.FRAME_HEADER.unpack_from(encoded)
    header_size = fields[2]
    encoded[:protocol.FRAME_HEADER.size] = protocol.FRAME_HEADER.pack(*fields[:-1], 0)
    crc = zlib.crc32(encoded[:header_size])
    encoded[:protocol.FRAME_HEADER.size] = protocol.FRAME_HEADER.pack(*fields[:-1], crc)
    receiver, sender = socket.socketpair()
    with receiver, sender:
        sender.sendall(encoded)
        with pytest.raises(protocol.ProtocolError, match="serial"):
            protocol.receive_frame(receiver)


@pytest.mark.parametrize("filename", ["../outside.zst", "/tmp/outside.zst"])
def test_capture_replay_rejects_payload_paths_outside_pair(tmp_path, protocol, monkeypatch, filename):
    import json

    root = Path(__file__).resolve().parents[1] / "tools"
    monkeypatch.syspath_prepend(str(root))
    replay = importlib.import_module("replay_obp1_sender")
    (tmp_path / "metadata.json").write_text(json.dumps({"payloads": [{"file": filename}]}))
    try:
        with pytest.raises(ValueError, match="payload path"):
            replay.build_frame(tmp_path, 0)
    finally:
        sys.modules.pop("replay_obp1_sender", None)
