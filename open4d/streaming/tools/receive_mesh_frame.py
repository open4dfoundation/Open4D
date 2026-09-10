#!/usr/bin/env python3
"""Validate one MRD1 raw or MRD2 Draco mesh/texture TCP frame."""

import argparse
import json
import socket
import struct
import time
from pathlib import Path


PREFIX = struct.Struct("!2I")
MRD1_REMAINDER = struct.Struct("!9I")
MRD2_REMAINDER = struct.Struct("!5I")
MRD1_MAGIC = 0x4D524431
MRD2_MAGIC = 0x4D524432


def receive_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed before complete frame")
        data.extend(chunk)
    return bytes(data)


def connect_with_retry(host: str, port: int, seconds: float) -> socket.socket:
    deadline = time.monotonic() + seconds
    while True:
        try:
            return socket.create_connection((host, port), timeout=5)
        except OSError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.2)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=33669)
    parser.add_argument("--wait", type=float, default=60)
    parser.add_argument("--summary")
    parser.add_argument("--save-draco")
    args = parser.parse_args()

    with connect_with_retry(args.host, args.port, args.wait) as sock:
        magic, version = PREFIX.unpack(receive_exact(sock, PREFIX.size))
        if version != 1:
            raise ValueError(f"unsupported protocol version {version}")
        if magic == MRD1_MAGIC:
            values = MRD1_REMAINDER.unpack(
                receive_exact(sock, MRD1_REMAINDER.size))
            (vertices, faces, position_bytes, normal_bytes, index_bytes,
             uv_bytes, texture_width, texture_height, texture_bytes) = values
            expected = {
                "positions": vertices * 3 * 4,
                "normals": vertices * 3 * 4,
                "indices": faces * 3 * 4,
                "uvs": faces * 3 * 2 * 4,
                "texture": texture_width * texture_height * 3,
            }
            actual = {
                "positions": position_bytes,
                "normals": normal_bytes,
                "indices": index_bytes,
                "uvs": uv_bytes,
                "texture": texture_bytes,
            }
            if actual != expected:
                raise ValueError(
                    f"payload sizes do not match: {actual} != {expected}")
            for size in actual.values():
                receive_exact(sock, size)
            summary = {
                "protocol": "MRD1",
                "version": version,
                "vertices": vertices,
                "faces": faces,
                "texture_width": texture_width,
                "texture_height": texture_height,
                "payload_bytes": sum(actual.values()),
            }
        elif magic == MRD2_MAGIC:
            (draco_bytes, texture_width, texture_height, texture_bytes,
             texture_encoding) = MRD2_REMAINDER.unpack(
                 receive_exact(sock, MRD2_REMAINDER.size))
            if texture_encoding != 1:
                raise ValueError(f"unsupported texture encoding {texture_encoding}")
            if texture_bytes != texture_width * texture_height * 3:
                raise ValueError("MRD2 RGB8 texture size does not match dimensions")
            draco_data = receive_exact(sock, draco_bytes)
            receive_exact(sock, texture_bytes)
            if args.save_draco:
                Path(args.save_draco).write_bytes(draco_data)
            summary = {
                "protocol": "MRD2",
                "version": version,
                "mesh_encoding": "Draco",
                "draco_bytes": draco_bytes,
                "texture_encoding": "RGB8",
                "texture_width": texture_width,
                "texture_height": texture_height,
                "payload_bytes": draco_bytes + texture_bytes,
            }
        else:
            raise ValueError(f"bad magic 0x{magic:08x}")
    print(json.dumps(summary, indent=2))
    if args.summary:
        Path(args.summary).write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
