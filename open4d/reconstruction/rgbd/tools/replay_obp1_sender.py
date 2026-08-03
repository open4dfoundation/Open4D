#!/usr/bin/env python3
"""Replay saved synchronized capture pairs to a live receiver as OBP1 frames.

This stands in for the Windows capture host, so the live two-camera path can be
exercised end to end with no cameras and no second machine. Payloads are sent
byte-for-byte as they were captured, so the receiver sees exactly what the real
sender produced, including original CRCs.

Start the receiver first (it listens), then run this (it connects):

    # terminal 1 -- receiver
    export FOURD_CAPTURE_ROOT=/path/to/captures
    python python/live_two_camera_fusion.py --headless --max-pairs 7

    # terminal 2 -- replay sender
    ./tools/replay_obp1_sender.py --captures-root /path/to/captures/captures_<date>

Each saved pair directory must contain metadata.json plus the payload files it
references (the .jpg colour frames and .zst depth frames).
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from pathlib import Path

MODULE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MODULE_ROOT / "python"))

import protocol  # noqa: E402  (needs the path insert above)


def build_frame(pair_dir: Path, delay_usec: int) -> protocol.Frame:
    metadata = json.loads((pair_dir / "metadata.json").read_text())
    payloads = []
    for entry in metadata["payloads"]:
        data = (pair_dir / entry["file"]).read_bytes()
        if len(data) != entry["compressed_length"]:
            raise SystemExit(
                f"{pair_dir.name}: {entry['file']} is {len(data)} bytes, "
                f"metadata says {entry['compressed_length']}"
            )
        payloads.append(
            protocol.Payload(
                serial=entry["serial"],
                stream_type=entry["stream_type"],
                codec=entry["codec"],
                width=entry["width"],
                height=entry["height"],
                format=entry["format"],
                raw_length=entry["raw_length"],
                device_timestamp_us=entry["device_timestamp_us"],
                data=data,
            )
        )

    # The receiver recomputes this and rejects the frame if it disagrees.
    calculated = (
        metadata["j3_timestamp_us"] - metadata["ey_timestamp_us"] - delay_usec
    )
    if calculated != metadata["sync_error_us"]:
        raise SystemExit(
            f"{pair_dir.name}: sync metadata inconsistent for "
            f"--delay-usec {delay_usec} "
            f"(computed {calculated}, metadata {metadata['sync_error_us']}). "
            "Pass the --delay-usec the capture was made with."
        )

    return protocol.Frame(
        pair_number=metadata["pair_number"],
        sender_wallclock_ns=metadata["sender_wallclock_ns"],
        ey_timestamp_us=metadata["ey_timestamp_us"],
        j3_timestamp_us=metadata["j3_timestamp_us"],
        sync_error_us=metadata["sync_error_us"],
        flags=protocol.FLAG_HARDWARE_SYNC | protocol.FLAG_DEVICE_TIMESTAMPS,
        queue_dropped_total=metadata.get("queue_dropped_total", 0),
        payloads=tuple(payloads),
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--captures-root", type=Path, required=True,
                        help="directory containing pair_<012d>/ subdirectories")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17000)
    parser.add_argument("--start", type=int, default=None)
    parser.add_argument("--end", type=int, default=None)
    parser.add_argument("--fps", type=float, default=15.0,
                        help="send rate; 0 sends as fast as possible")
    parser.add_argument("--loop", action="store_true",
                        help="repeat the sequence until interrupted")
    parser.add_argument("--delay-usec", type=int, default=160,
                        help="subordinate delay the capture was made with")
    parser.add_argument("--connect-timeout", type=float, default=30.0)
    parser.add_argument("--no-ack", action="store_true",
                        help="do not wait for the receiver's OBA1 ack")
    args = parser.parse_args()

    pair_dirs = sorted(args.captures_root.glob("pair_*"))
    if not pair_dirs:
        raise SystemExit(f"no pair_* directories under {args.captures_root}")
    if args.start is not None or args.end is not None:
        low = args.start if args.start is not None else -1
        high = args.end if args.end is not None else 1 << 62
        pair_dirs = [
            d for d in pair_dirs if low <= int(d.name.split("_")[1]) <= high
        ]
        if not pair_dirs:
            raise SystemExit("no pairs in the requested range")

    print(f"replaying {len(pair_dirs)} pairs to {args.host}:{args.port}")

    deadline = time.monotonic() + args.connect_timeout
    while True:
        try:
            sock = socket.create_connection((args.host, args.port), timeout=10)
            break
        except OSError as error:
            if time.monotonic() >= deadline:
                raise SystemExit(
                    f"could not connect to {args.host}:{args.port}: {error}\n"
                    "Start the receiver first."
                )
            time.sleep(0.2)

    interval = 1.0 / args.fps if args.fps > 0 else 0.0
    sent = 0
    wire_bytes = 0
    started = time.monotonic()
    try:
        while True:
            for pair_dir in pair_dirs:
                frame = build_frame(pair_dir, args.delay_usec)
                encoded = protocol.encode_frame(frame)
                sock.sendall(encoded)
                wire_bytes += len(encoded)
                if not args.no_ack:
                    protocol.receive_ack(sock, frame.pair_number)
                sent += 1
                print(f"sent pair {frame.pair_number} "
                      f"({len(encoded)} bytes, sync_error={frame.sync_error_us}us)")
                if interval:
                    time.sleep(interval)
            if not args.loop:
                break
    except (BrokenPipeError, ConnectionResetError, EOFError):
        # Normal when the receiver reaches its own --max-pairs limit first.
        print(f"receiver closed the connection after {sent} pairs")
    except KeyboardInterrupt:
        print("interrupted")
    finally:
        sock.close()

    elapsed = time.monotonic() - started
    rate = sent / elapsed if elapsed > 0 else 0.0
    print(f"\nsent {sent} pairs, {wire_bytes / 1e6:.1f} MB, "
          f"{elapsed:.1f}s, {rate:.1f} pairs/s")


if __name__ == "__main__":
    main()
