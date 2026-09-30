"""The open4d command line interface."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys

from . import load
from .codec import CodecError, available_codecs
from .core import Sequence
from .demo import write_demo
from .io import Open4DError


def _positive_integer(value: str) -> int:
    try:
        result = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a positive integer") from error
    if result < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return result


def _side(value: str) -> int:
    result = _positive_integer(value)
    if result < 2:
        raise argparse.ArgumentTypeError("must be at least 2")
    return result


def _positive_float(value: str) -> float:
    try:
        result = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a finite positive number") from error
    if not math.isfinite(result) or result <= 0:
        raise argparse.ArgumentTypeError("must be a finite positive number")
    return result


def _source_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("source", type=Path, help="mesh file, frame directory, or supported sequence file")
    parser.add_argument("--format", help="select an input mesh format, for example ply or obj")
    parser.add_argument(
        "--input-fps", type=_positive_float,
        help="set imported timing for sources without stored timestamps (default: 30)",
    )


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="open4d", description="Generate, inspect, and view triangular mesh sequences.",
    )
    commands = parser.add_subparsers(dest="command", required=True)
    demo = commands.add_parser("demo", help="generate a shareable PLY wave animation")
    demo.add_argument(
        "destination", nargs="?", type=Path, default=Path("open4d-demo"),
        help="new output folder (default: open4d-demo)",
    )
    demo.add_argument("--side", type=_side, default=24, help="vertices per grid edge (default: 24)")
    demo.add_argument("--frames", type=_positive_integer, default=60, help="frame count (default: 60)")
    demo.add_argument("--fps", type=_positive_float, default=30.0, help="sample frame rate (default: 30)")

    inspect = commands.add_parser(
        "inspect", help="report sequence timing and the first frame's geometry",
        description="Report timing, topology, and the first decoded frame. "
        "Some encoded formats decode the whole sequence when opened.",
    )
    _source_arguments(inspect)
    inspect.add_argument("--json", action="store_true", help="print a JSON summary")

    view = commands.add_parser(
        "view", help="open the interactive viewer (requires open4d[player])",
        description="Play a sequence. Install open4d[player] first. "
        "Drag to orbit, scroll to zoom, space to pause, arrows to step, and q to quit.",
    )
    _source_arguments(view)
    view.add_argument("--fps", type=_positive_float, help="playback frame rate")
    view.add_argument("--up", choices=("x", "y", "z"), help="source up axis; uses recorded metadata, otherwise z")
    view.add_argument("--stride", type=_positive_integer, default=1, help="display every Nth frame")
    view.add_argument("--width", type=_positive_integer, default=960, help="window width in pixels")
    view.add_argument("--height", type=_positive_integer, default=960, help="window height in pixels")
    view.add_argument("--wireframe", action="store_true", help="also draw triangle edges")
    return parser


def _inspect(sequence: Sequence, source: Path) -> dict:
    timestamps = sequence.timestamps
    info = {
        "source": str(source.absolute()),
        "frame_count": len(sequence),
        "fps": sequence.fps,
        "first_timestamp": timestamps[0] if timestamps else None,
        "last_timestamp": timestamps[-1] if timestamps else None,
        "timestamp_span_seconds": sequence.duration,
        "topology": sequence.topology.value,
        "has_constant_vertex_count": sequence.has_constant_vertex_count,
        "has_vertex_correspondence": sequence.has_vertex_correspondence,
        "first_frame": None,
    }
    if len(sequence):
        first = sequence[0]
        geometry = first.geometry
        info["first_frame"] = {
            "index": first.frame_index,
            "vertices": len(geometry.positions),
            "triangles": len(geometry.triangles),
            "attributes": ["positions", "triangles"] + [
                name for name in ("normals", "colors", "texture_coordinates")
                if getattr(geometry, name) is not None
            ] + sorted(geometry.attributes),
        }
    return info


def _report(info: dict) -> None:
    print(f"Source: {info['source']}")
    print(f"Frames: {info['frame_count']}")
    fps = info["fps"]
    print(f"FPS (average): {fps:g}" if fps is not None else "FPS: unknown or static")
    print(f"Timestamp span: {info['timestamp_span_seconds']:g} seconds (first to last frame)")
    print(f"Topology: {info['topology']}")
    correspondence = info["has_vertex_correspondence"]
    print(f"Vertex correspondence: {correspondence if correspondence is not None else 'unknown'}")
    first = info["first_frame"]
    if first is not None:
        print(f"First frame #{first['index']}: {first['vertices']} vertices, {first['triangles']} triangles")
        print(f"Attributes: {', '.join(first['attributes'])}")


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "demo":
            path = write_demo(args.destination, side=args.side, frames=args.frames, fps=args.fps)
            print(f"Created {args.frames} PLY frames at {args.fps:g} fps: {path}")
            print(f'Inspect: open4d inspect "{path}"')
            print(f'Play:    open4d view "{path}"  (requires open4d[player])')
            return 0

        source = args.source.expanduser()
        if not source.exists():
            raise FileNotFoundError(f"sequence source does not exist: {source}")
        if any(source.suffix.lower() in info.suffixes and info.representation != "triangle_mesh"
               for info in available_codecs()):
            raise ValueError("this command takes mesh sequences; use open4d.decode for Gaussian artifacts")
        if args.command == "view":
            # Check optional dependencies before an expensive codec decode.
            from .visualization import _qt, visualize

            _qt.check_available()
        with load(source, format=args.format, fps=args.input_fps) as sequence:
            if args.command == "inspect":
                info = _inspect(sequence, source)
                if args.json:
                    print(json.dumps(info, indent=2, allow_nan=False))
                else:
                    _report(info)
            else:
                visualize(
                    sequence, fps=args.fps, up=args.up, stride=args.stride,
                    width=args.width, height=args.height, wireframe=args.wireframe,
                )
        return 0
    except (Open4DError, CodecError, ImportError, OSError, ValueError, TypeError) as error:
        print(f"open4d: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
