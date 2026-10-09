"""The open4d command line interface."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys

from . import load
from .codec import CodecError
from .codec._o4d_format import is_mesh_profile, probe_codec
from .core import Sequence
from .demo import write_demo
from .io import Open4DError
from .native import NativeSequence


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
        "inspect", help="report timing and geometry, or an O4D header without decoding",
        description="Report timing, topology, and the first decoded frame. "
        "O4D files and O4D USD prims are summarized from their header without decoding; "
        "other encoded formats decode the whole sequence when opened.",
    )
    _source_arguments(inspect)
    inspect.add_argument("--json", action="store_true", help="print a JSON summary")
    inspect.add_argument(
        "--decode", action="store_true",
        help="also decode an O4D mesh profile for topology and first-frame geometry "
        "(needs the codec's native backend)",
    )

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


def _container(source: Path) -> NativeSequence | None:
    """Open a standalone O4D or an O4D USD prim without decoding its payloads."""
    suffix = source.suffix.lower()
    if not source.is_file():
        return None
    if suffix == ".o4d" and probe_codec(source) is not None:
        return NativeSequence(source)
    if suffix in (".usd", ".usda", ".usdc"):
        from .io._native_usd import is_native_usd, read_native_usd

        if is_native_usd(source):
            return read_native_usd(source)
    return None


def _summary(native: NativeSequence, source: Path) -> dict:
    manifest, files = native.manifest, native.manifest["files"]
    summary = {
        "format": "o4d" if source.suffix.lower() == ".o4d" else "usd",
        "schema": manifest["schema"],
        "codec": native.codec,
        "representation": native.representation,
        "dependency_mode": manifest["dependency_mode"],
        "decodes_to_mesh": is_mesh_profile(native.path),
        "payload_files": len(files),
        "payload_bytes": sum(record["size"] for record in files),
        "stream_bytes": native.path.stat().st_size,
        "file_bytes": source.stat().st_size,
    }
    if native.codec == "frames":
        summary["frame_representation"] = manifest["native"]["representation"]
    return summary


def _stores(container: dict) -> str:
    representation = container["representation"]
    if "frame_representation" in container:
        representation += f" ({container['frame_representation']})"
    return representation


def _header(native: NativeSequence, source: Path, container: dict) -> dict:
    # Timing follows Sequence.fps and Sequence.duration.
    timestamps, count = native.timestamps, len(native)
    span = abs(timestamps[-1] - timestamps[0]) if count > 1 else 0.0
    return {
        "source": str(source.absolute()),
        "frame_count": count,
        "fps": (count - 1) / span if count > 1 and span > 0 else None,
        "first_timestamp": timestamps[0],
        "last_timestamp": timestamps[-1],
        "timestamp_span_seconds": span,
        "container": container,
    }


def _size(value: float) -> str:
    if value < 1000:
        return f"{value} bytes"
    for unit in ("kB", "MB", "GB", "TB"):
        value /= 1000
        if value < 1000 or unit == "TB":
            return f"{value:.1f} {unit}"


def _report(info: dict) -> None:
    print(f"Source: {info['source']}")
    container = info.get("container")
    if container is not None:
        kind = "O4D" if container["format"] == "o4d" else "O4D in USD"
        print(f"Container: {kind}, codec {container['codec']}, {_stores(container)}")
        print(f"Dependencies: {container['dependency_mode']}")
        print(f"Payload: {container['payload_files']} files, {_size(container['payload_bytes'])} "
              f"(file {_size(container['file_bytes'])})")
    print(f"Frames: {info['frame_count']}")
    fps = info["fps"]
    print(f"FPS (average): {fps:g}" if fps is not None else "FPS: unknown or static")
    print(f"Timestamp span: {info['timestamp_span_seconds']:g} seconds (first to last frame)")
    if "topology" not in info:
        if container["decodes_to_mesh"]:
            print("Geometry: not decoded; pass --decode for topology and the first frame")
        return
    print(f"Topology: {info['topology']}")
    correspondence = info["has_vertex_correspondence"]
    print(f"Vertex correspondence: {correspondence if correspondence is not None else 'unknown'}")
    first = info["first_frame"]
    if first is not None:
        print(f"First frame #{first['index']}: {first['vertices']} vertices, {first['triangles']} triangles")
        print(f"Attributes: {', '.join(first['attributes'])}")


def _print(info: dict, as_json: bool) -> None:
    if as_json:
        print(json.dumps(info, indent=2, allow_nan=False))
    else:
        _report(info)


def _run(args: argparse.Namespace, source: Path, native: NativeSequence | None) -> int:
    decode = args.command == "view" or args.decode
    container = None if native is None else _summary(native, source)
    if container is not None:
        if args.format is not None or args.input_fps is not None:
            raise TypeError("O4D stores its own representation and timestamps; omit --format and --input-fps")
        if decode and not container["decodes_to_mesh"]:
            reader = "the viewer shows" if args.command == "view" else "--decode reports"
            raise ValueError(f"{native.codec} stores {_stores(container)}; {reader} triangle meshes")
        if not decode:
            _print(_header(native, source, container), args.json)
            return 0
    if args.command == "view":
        # Check optional dependencies before an expensive codec decode.
        from .visualization import _qt, visualize

        _qt.check_available()
    target = source if native is None else native.path
    with load(target, format=args.format, fps=args.input_fps) as sequence:
        if not isinstance(sequence, Sequence):
            raise ValueError("this command takes mesh sequences; use the native renderer for Gaussian or field data")
        if args.command == "inspect":
            info = _inspect(sequence, source)
            if container is not None:
                info["container"] = container
            _print(info, args.json)
        else:
            visualize(
                sequence, fps=args.fps, up=args.up, stride=args.stride,
                width=args.width, height=args.height, wireframe=args.wireframe,
            )
    return 0


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
        native = _container(source)
        try:
            return _run(args, source, native)
        finally:
            if native is not None:
                native.close()
    except (Open4DError, CodecError, ImportError, OSError, ValueError, TypeError) as error:
        print(f"open4d: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
