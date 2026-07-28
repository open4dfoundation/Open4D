#!/usr/bin/env python3
"""Receive and validate continuous MRD3 Draco/JPEG mesh frames."""

import argparse
import json
import socket
import struct
import time


HEADER = struct.Struct("!14I")
MAGIC = 0x4D524433


def receive_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("stream closed during a frame")
        data.extend(chunk)
    return bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=33669)
    parser.add_argument("--frames", type=int, default=10)
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()

    summaries = []
    started = time.monotonic()
    with socket.create_connection(
        (args.host, args.port), timeout=args.timeout
    ) as sock:
        for _ in range(args.frames):
            values = HEADER.unpack(receive_exact(sock, HEADER.size))
            (
                magic,
                version,
                message_type,
                frame_id,
                timestamp_hi,
                timestamp_lo,
                vertices,
                faces,
                geometry_bytes,
                image_bytes,
                width,
                height,
                dropped,
                flags,
            ) = values
            if magic != MAGIC or version != 1 or message_type != 1:
                raise ValueError(f"bad MRD3 header: {values[:3]}")
            geometry_codec = flags >> 16
            image_codec = flags & 0xFFFF
            if geometry_codec != 1 or image_codec != 1:
                raise ValueError("expected Draco geometry and JPEG image")
            geometry = receive_exact(sock, geometry_bytes)
            image = receive_exact(sock, image_bytes)
            if not geometry or not image.startswith(b"\xff\xd8"):
                raise ValueError("invalid Draco/JPEG payload")
            summary = {
                "frame_id": frame_id,
                "timestamp_usec": (timestamp_hi << 32) | timestamp_lo,
                "vertices": vertices,
                "faces": faces,
                "draco_bytes": geometry_bytes,
                "jpeg_bytes": image_bytes,
                "width": width,
                "height": height,
                "dropped_before_encode": dropped,
            }
            summaries.append(summary)
            print(json.dumps(summary))

    elapsed = time.monotonic() - started
    total_bytes = sum(
        item["draco_bytes"] + item["jpeg_bytes"] + HEADER.size
        for item in summaries
    )
    print(
        json.dumps(
            {
                "received_frames": len(summaries),
                "elapsed_seconds": elapsed,
                "receive_fps": len(summaries) / elapsed if elapsed else 0,
                "average_frame_bytes": total_bytes / len(summaries),
                "megabits_per_second": total_bytes * 8 / elapsed / 1_000_000,
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
