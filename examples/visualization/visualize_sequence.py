"""Load your 4D sequence, report what it contains, and play it.

    python examples/visualization/visualize_sequence.py my_capture/
    python examples/visualization/visualize_sequence.py my_capture/ --info
    python examples/visualization/visualize_sequence.py my_capture/ --up y --save out.gif
    python examples/visualization/visualize_sequence.py my_capture.usdc

Point it at your own data. A source is normally a whole-sequence file such as
`.o4d`, a time-sampled `.usdc`, or a raw V-DMC `.vmesh` bitstream. Raw V-DMC
input needs the native decoder named by `OPEN4D_VDMC_DECODER`. A folder holding
one mesh per frame and a standalone mesh remain useful import paths. Meshes and
point clouds are both handled: a frame with no faces is drawn as a point cloud.

Start with `--info`. It reports frame count, timing, and topology without
parsing frame geometry or opening a window, which is the quickest way to see
whether a dataset loads and what the loader made of it.

In the window: drag to orbit, scroll to zoom, drag the slider to scrub, space to
pause, left/right to step, q to quit. Frame number, timestamp and vertex/triangle
counts sit in the top-left corner; `--no-metrics` hides them.

Playback is our own viewer, built on PyQt6 and pyqtgraph. It needs no Open3D,
which matters because Open3D publishes no wheels for Python 3.13. The window and
`--save` share one renderer, so a saved GIF looks like what you saw.
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path
from typing import Any

import numpy as np

# Import first: this puts the repository on sys.path for uninstalled clones.
from _common import existing_source
from frame_sources import (
    DEFAULT_FPS,
    describe_source,
    supported_formats,
)
from open4d import load as open_sequence, save as save_sequence
from open4d.codec import CodecError
from open4d.io import Open4DError
from open4d.visualization._frames import (
    LazyRenderSequence,
    UP_AXES,
    UP_TO_Z,
    bounds,
)

# pyqtgraph draws with +Z up, so the source's up axis is rotated onto Z and the
# axis pointing up in the view is index 2.
PLOT_UP = 2


def report(sequence, path: Path, fps: float) -> None:
    """Print what the sequence declares, before decoding any geometry."""
    print(f"\n{path}")
    print(f"  {describe_source(path, sequence)}")
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


def report_geometry(frame, frame_count: int, stride: int) -> None:
    """Describe the first display frame without forcing an eager bounds scan."""
    lower, upper = bounds([frame])

    print(f"\nfirst decoded frame of {frame_count} (stride {stride})")
    print(f"  geometry   : {'triangle mesh' if frame.is_mesh else 'point cloud'}")
    print(f"  vertices   : {len(frame.positions)}")
    if frame.is_mesh:
        print(f"  triangles  : {len(frame.triangles)}")
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
            print("  note: the first frame is longest across the view, not "
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
        help="your sequence: an .o4d, USD, or raw .vmesh file; a frame "
        "directory; or a standalone mesh import",
    )
    parser.add_argument(
        "--stride", type=int, default=1, help="keep every Nth frame"
    )
    parser.add_argument(
        "--fps",
        type=float,
        default=None,
        help="override playback; for a manifest-free frame directory or raw "
        ".vmesh this also defines imported timestamps (default: 30 fps)",
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
        import_fps = (
            args.fps
            if path.is_dir() or path.suffix.lower() == ".vmesh"
            else None
        )
        with open_sequence(path, fps=import_fps) as sequence:
            # One rate, resolved once, used for reporting, playback, GIF timing
            # and any container we write.
            fps = resolve_fps(sequence, args.fps)
            args.fps = fps
            report(sequence, path, fps)
            if not len(sequence):
                raise SystemExit(f"{path} contains no frames")

            if args.pack_usd:
                written = save_sequence(
                    sequence,
                    fps=fps,
                    up_axis=resolve_up(sequence, args.up),
                    destination=args.pack_usd,
                )
                print(f"\nwrote {written} "
                      f"({written.stat().st_size / 1e6:.2f} MB)")

            if args.info:
                return

            up = resolve_up(sequence, args.up)
            frames = LazyRenderSequence(
                sequence,
                stride=args.stride,
                order=UP_TO_Z[up],
            )
            report_geometry(frames[0], len(frames), args.stride)

            from open4d.visualization import _qt as viewer_qt

            if args.save:
                viewer_qt.record(frames, args, args.save)
            else:
                viewer_qt.play(frames, args)
    except (Open4DError, CodecError, ValueError, TypeError, OSError) as error:
        raise SystemExit(f"\nfailed to read the sequence:\n  {error}") from None


if __name__ == "__main__":
    main()
