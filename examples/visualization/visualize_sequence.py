"""Load your 4D sequence, report what it contains, and play it.

    python examples/visualization/visualize_sequence.py my_capture/
    python examples/visualization/visualize_sequence.py my_capture/ --info
    python examples/visualization/visualize_sequence.py my_capture/ --up y --save out.gif
    python examples/visualization/visualize_sequence.py my_capture.usdc

Point it at your own data. A source is either a folder holding one mesh file per
frame (`.obj`, `.ply`, `.usd`, or anything trimesh reads) or a single
time-sampled USD file. Meshes and point clouds are both handled: a frame with no
faces is drawn as a point cloud.

Start with `--info`. It reports frame count, duration, topology and bounds
without decoding geometry or opening a window, which is the quickest way to see
whether a dataset loads and what the loader made of it.

In the window: drag to orbit, scroll to zoom, drag the slider to scrub, space to
pause, left/right to step, q to quit. Frame number, timestamp and vertex/triangle
counts sit in the top-left corner; `--no-metrics` hides them.

Playback is our own viewer, built on PyQt6 and pyqtgraph — the same stack as
`open4d.player`. It needs no Open3D, which matters because Open3D publishes no
wheels for Python 3.13. The window and `--save` share one renderer, so a saved
GIF looks like what you saw.
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path
from typing import Any

import numpy as np

# Import first: this puts the repository on sys.path for uninstalled clones.
from _common import existing_source, require
from frame_sources import (
    DEFAULT_FPS,
    describe_source,
    open_sequence,
    supported_formats,
)
from render_frames import UP_AXES, UP_TO_Z, bounds, decode_all

# pyqtgraph draws with +Z up, so the source's up axis is rotated onto Z and the
# axis pointing up in the view is index 2.
PLOT_UP = 2


def report(sequence, path: Path, fps: float) -> None:
    """Print what the sequence declares, before decoding any geometry."""
    print(f"\n{path}")
    print(f"  {describe_source(path)}")
    print(f"  frames     : {len(sequence)}")
    print(f"  duration   : {sequence.duration:.3f} s at "
          f"{sequence.fps or 0:.2f} fps")
    print(f"  playing at : {fps:.2f} fps")
    print(f"  topology   : {sequence.topology.value}")

    # A USD container records its own frame rate, up axis and key frames; a
    # frame folder has none of that, so only print what is actually there.
    for key in ("up_axis", "prim", "prim_type", "key_frame_indices", "format"):
        if key in sequence.metadata:
            value = sequence.metadata[key]
            if key == "key_frame_indices" and len(value) > 12:
                value = f"{list(value[:12])} ... ({len(value)} total)"
            print(f"  {key:<17}: {value}")


def resolve_fps(sequence, requested: float | None) -> float:
    """Pick the playback rate: the flag wins, then whatever the source declares.

    `Sequence.fps` is derived from the timestamps, so it carries float noise
    (30.000000000000004); the provider's declared rate is exact when present.
    """
    if requested:
        return float(requested)
    declared = sequence.metadata.get("fps")
    if declared:
        return float(declared)
    return float(sequence.fps or DEFAULT_FPS)


def resolve_up(sequence, requested: str | None) -> str:
    """Pick the up axis: the flag wins, then whatever the source recorded."""
    if requested:
        return requested
    recorded = str(sequence.metadata.get("up_axis", "")).lower()
    return recorded if recorded in UP_TO_Z else "z"


def report_geometry(frames: list, stride: int) -> None:
    """Print what the decoded frames turned out to be.

    Each frame is tested individually: a folder of `.ply` where some frames have
    faces and some do not yields a mix of meshes and point clouds, and assuming
    the whole sequence matches frame 0 raises AttributeError on the first
    mismatch.
    """
    kinds = {frame.is_mesh for frame in frames}
    counts = [len(frame.positions) for frame in frames]
    faces = [len(frame.triangles) for frame in frames]
    lower, upper = bounds(frames)

    def summarize(values: list[int]) -> str:
        unique = sorted(set(values))
        if len(unique) == 1:
            return str(unique[0])
        return f"{unique[0]}..{unique[-1]} ({len(unique)} distinct)"

    if kinds == {True}:
        description = "triangle mesh"
    elif kinds == {False}:
        description = "point cloud"
    else:
        description = "mixed: some frames have faces, some do not"

    print(f"\ndecoded {len(frames)} frames (stride {stride})")
    print(f"  geometry   : {description}")
    print(f"  vertices   : {summarize(counts)}")
    if True in kinds:
        print(f"  triangles  : {summarize(faces)}")
    print(f"  bounds     : {lower.round(2)} .. {upper.round(2)}")

    # After reordering the up axis is PLOT_UP. A subject much longer along a
    # different axis usually means the source up axis was guessed wrong; a
    # genuinely wide flat subject is excluded by comparing against the second
    # longest rather than against up.
    extents = upper - lower
    longest = int(np.argmax(extents))
    if longest != PLOT_UP:
        runner_up = float(np.partition(extents, -2)[-2])
        if extents[longest] > 1.5 * max(runner_up, 1e-9):
            print("  note: the sequence is longest across the view, not "
                  "upright — the up axis may be wrong, try --up x/y/z")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=supported_formats(),
    )
    parser.add_argument(
        "path",
        type=Path,
        nargs="?",
        help="your sequence: a folder of per-frame mesh files, or a USD "
        "container",
    )
    parser.add_argument(
        "--stride", type=int, default=1, help="keep every Nth frame"
    )
    parser.add_argument(
        "--fps",
        type=float,
        default=None,
        help="override the frame rate; by default a USD file's own stage rate "
        "is used, and a folder gets 30",
    )
    parser.add_argument(
        "--up",
        choices=UP_AXES,
        default=None,
        help="which data axis points up; defaults to what a USD container "
        "records, otherwise z",
    )
    parser.add_argument(
        "--point-size", type=float, default=3.0, help="point-cloud marker size"
    )
    parser.add_argument("--width", type=int, default=960, help="window width")
    parser.add_argument("--height", type=int, default=960, help="window height")
    parser.add_argument(
        "--color",
        type=float,
        nargs=3,
        metavar=("R", "G", "B"),
        default=(0.95, 0.95, 0.97),
        help="surface colour in 0-1, for frames without their own",
    )
    parser.add_argument(
        "--ambient",
        type=float,
        default=0.32,
        help="how lit the faces turned away from the light are, 0-1",
    )
    parser.add_argument(
        "--background",
        type=float,
        nargs=3,
        metavar=("R", "G", "B"),
        default=(1.0, 1.0, 1.0),
        help="background colour in 0-1",
    )
    parser.add_argument(
        "--wireframe", action="store_true", help="draw mesh edges as well"
    )
    parser.add_argument(
        "--no-metrics",
        action="store_true",
        help="hide the metrics overlay in the corner of the view",
    )
    parser.add_argument(
        "--distance",
        type=float,
        default=1.15,
        help="camera distance as a multiple of the subject size",
    )
    parser.add_argument(
        "--elevation", type=float, default=14.0, help="camera elevation, degrees"
    )
    parser.add_argument(
        "--azimuth", type=float, default=-62.0, help="camera azimuth, degrees"
    )
    parser.add_argument(
        "--save", type=Path, help="render offscreen to an animated .gif"
    )
    parser.add_argument(
        "--info", action="store_true", help="report the sequence and stop"
    )
    parser.add_argument(
        "--pack-usd",
        type=Path,
        help="also write the sequence to a .usdc OpenUSD container",
    )
    args = parser.parse_args()
    if args.path is None:
        # Full help rather than a one-line usage error: the epilog lists every
        # format, which is what someone pointing this at new data needs to see.
        parser.print_help()
        raise SystemExit(2)
    if args.stride < 1:
        parser.error("--stride must be at least 1")
    if args.fps is not None and args.fps <= 0:
        parser.error("--fps must be greater than zero")

    path = existing_source(args.path)

    # A malformed frame in someone else's dataset is ordinary, not a crash, so
    # report it as an error naming the file rather than a traceback.
    try:
        with open_sequence(path, fps=args.fps) as sequence:
            # One rate, resolved once, used for reporting, playback, GIF timing
            # and any container we write.
            fps = resolve_fps(sequence, args.fps)
            args.fps = fps
            report(sequence, path, fps)
            if not len(sequence):
                raise SystemExit(f"{path} contains no frames")

            if args.pack_usd:
                from formats_usd import write_usd_container

                # A folder reports its formats as a list, a container as one
                # string; record either as a plain string.
                source_format = sequence.metadata.get("format", "")
                if isinstance(source_format, (list, tuple)):
                    source_format = ", ".join(source_format)

                written = write_usd_container(
                    args.pack_usd,
                    sequence,
                    fps=fps,
                    up_axis=resolve_up(sequence, args.up),
                    source=str(path),
                    source_format=str(source_format),
                    generator="examples/visualization/visualize_sequence.py",
                )
                print(f"\nwrote {written} "
                      f"({written.stat().st_size / 1e6:.2f} MB)")

            if args.info:
                return

            up = resolve_up(sequence, args.up)
            frames = decode_all(sequence, args.stride, UP_TO_Z[up])
    except (ValueError, TypeError, OSError) as error:
        raise SystemExit(f"\nfailed to read the sequence:\n  {error}") from None

    report_geometry(frames, args.stride)

    import viewer_qt

    if args.save:
        viewer_qt.record(frames, args, args.save)
    else:
        viewer_qt.play(frames, args)


if __name__ == "__main__":
    main()
