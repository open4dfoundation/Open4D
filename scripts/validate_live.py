"""Validate capture-time compression, TCP decoding and optional Qt playback.

Run ``receive`` on the display host and ``send`` on the dataset host. Reports
include exact array hashes for lossless profiles and independent stage timings.
"""

import argparse
from dataclasses import asdict, replace
import hashlib
import json
from pathlib import Path
import time

import numpy as np

from open4d.core import Frame, GaussianCloud, PointCloud, TriangleMesh
from open4d.io import open_sequence
from open4d.transport import LiveSender, receive


def fingerprint(frame):
    mesh = frame.geometry
    fields = {name: getattr(mesh, name, None) for name in
              ("positions", "triangles", "colors", "normals", "texture_coordinates",
               "scales", "rotations", "opacities")}
    fields.update({"attribute:" + k: v for k, v in mesh.attributes.items()})
    value = hashlib.sha256()
    for name, array in sorted(fields.items()):
        if array is not None:
            array = np.ascontiguousarray(array)
            value.update(json.dumps([name, array.dtype.str, array.shape]).encode())
            value.update(array.tobytes())
    return value.hexdigest()


def source_frames(args):
    if args.kind == "gaussians":
        paths = sorted(args.source.glob("frame_*.npz"))[:args.frames]
        if not paths:
            raise ValueError("Gaussian input must contain frame_*.npz arrays")
        for i, path in enumerate(paths):
            with np.load(path, allow_pickle=False) as data:
                geometry = GaussianCloud(**{key: data[key] for key in
                    ("positions", "scales", "rotations", "opacities")},
                    attributes={"spherical_harmonics": data["spherical_harmonics"]})
            yield Frame(i, i / args.fps, geometry)
    else:
        with open_sequence(args.source, fps=args.fps) as sequence:
            for frame in sequence[:args.frames]:
                # Mesh profiles in this validation intentionally measure geometry.
                mesh = frame.geometry
                geometry = (PointCloud(mesh.positions, colors=mesh.colors) if args.kind == "points"
                            else TriangleMesh(mesh.positions, mesh.triangles))
                yield replace(frame, geometry=geometry)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="operation", required=True)
    for operation in ("send", "receive"):
        sub = commands.add_parser(operation)
        sub.add_argument("--host", default="127.0.0.1")
        sub.add_argument("--port", type=int, default=47004)
        sub.add_argument("--report", type=Path, required=True)
        sub.add_argument("--frames", type=int, default=30)
        if operation == "send":
            sub.add_argument("--source", type=Path, required=True)
            sub.add_argument("--kind", choices=("mesh", "points", "gaussians"), required=True)
            sub.add_argument("--profile", default="zlib")
            sub.add_argument("--fps", type=float, default=30)
        else:
            sub.add_argument("--viewer", action="store_true")
            sub.add_argument("--screenshot", type=Path)
    args = parser.parse_args(argv)
    if args.frames < 1 or (args.operation == "send" and args.fps <= 0):
        parser.error("frames and fps must be positive")
    if args.operation == "send":
        exact = args.profile in ("raw", "zlib", "zstd", "lossless")
        with LiveSender(args.host, args.port, profile=args.profile, latest=False) as sender:
            started = time.monotonic()
            for i, frame in enumerate(source_frames(args)):
                delay = started + i / args.fps - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                metadata = {**frame.metadata, "validation_sha256": fingerprint(frame) if exact else None}
                sender.submit(replace(frame, metadata=metadata))
        report = {**asdict(sender.stats), "fps": sender.stats.fps,
                  "compression_ratio": sender.stats.compression_ratio, "profile": args.profile,
                  "source": str(args.source.resolve()), "representation": args.kind}
    else:
        seen = []
        def validated(receiver):
            for frame in receiver:
                expected = frame.metadata.get("validation_sha256")
                if expected is not None and fingerprint(frame) != expected:
                    raise ValueError(f"decoded arrays differ at frame {frame.frame_index}")
                if seen and frame.timestamp <= seen[-1][1]:
                    raise ValueError("received timestamps are not increasing")
                seen.append((frame.frame_index, frame.timestamp))
                yield frame
        with receive(args.host, args.port, timeout=120) as receiver:
            print(json.dumps({"listening": receiver.address}), flush=True)
            viewer = None
            try:
                if args.viewer or args.screenshot:
                    from open4d.visualization import Visualizer
                    viewer = Visualizer(up="y", width=1000, height=750, distance=2.0)
                    viewer.create_window()
                    displayed = viewer.stream(validated(receiver))
                    if args.screenshot:
                        from PIL import Image
                        Image.fromarray(viewer.capture_screen()).save(args.screenshot)
                else:
                    displayed = sum(1 for _ in validated(receiver))
                report = {**asdict(receiver.stats), "fps": receiver.stats.fps,
                          "displayed": displayed, "received_indices": seen,
                          "hash_validation": "exact for frames with sender hashes"}
            finally:
                if viewer is not None:
                    viewer.close()
        if len(seen) != args.frames:
            raise ValueError(f"received {len(seen)} frames; expected {args.frames}")
    args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(args.report, flush=True)


if __name__ == "__main__":
    main()
