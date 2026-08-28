"""Compare a decoded 4D sequence against its reference, side by side.

    python examples/visualization/compare_sequences.py reference/ decoded/
    python examples/visualization/compare_sequences.py reference/ decoded/ --info
    python examples/visualization/compare_sequences.py reference/ decoded/ \
        --metric plane --csv error.csv --save compare.gif

This is the comparison viewer; `visualize_sequence.py` is the plain one. Both
read the same sources — `.o4d` codec artifacts, time-sampled USD files, frame
directories, raw V-DMC `.vmesh` bitstreams, or standalone mesh imports —
through the same loader.

Two synchronized panes: the reference as geometry, the decoded mesh coloured by
its distance to that reference. One camera drives both, because comparing two
independently posed views tells you nothing.

A decoded mesh has its own vertex count and connectivity, so error is a
nearest-neighbour distance rather than a per-vertex difference: point-to-point by
default, `--metric plane` for the MPEG point-to-plane definition. Every figure is
reported in both directions, since a codec that deletes a limb scores well in one
of them.

Start with `--info`. It prints the per-frame error table and the sequence
summary, and needs no window, no GL, and no display — which also makes it the
form to use over ssh or in a batch job. `--csv` writes the same table for a
paper or a regression run.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

# Import first: this puts the repository on sys.path for uninstalled clones.
from _common import existing_source
import compare_frames
from frame_sources import DEFAULT_FPS, describe_source, open_sequence, supported_formats
from open4d.codec import CodecError
from open4d.io import Open4DError
from open4d.visualization._frames import UP_AXES, UP_TO_Z

CSV_COLUMNS = (
    "frame",
    "source_index",
    "timestamp",
    "reference_vertices",
    "reference_triangles",
    "decoded_vertices",
    "decoded_triangles",
    "rms_decoded_to_reference",
    "rms_reference_to_decoded",
    "symmetric_rms",
    "hausdorff",
    "psnr_decoded_to_reference_db",
    "psnr_reference_to_decoded_db",
    "symmetric_psnr_db",
)


def resolve_fps(sequence, requested: float | None) -> float:
    """Pick the playback rate: the flag wins, then whatever the source declares."""
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


def report_sources(reference, decoded, reference_path, decoded_path, fps) -> None:
    """Print what the two sources declare, before measuring anything."""
    for label, sequence, path in (
        ("reference", reference, reference_path),
        ("decoded", decoded, decoded_path),
    ):
        print(f"\n{label}: {path}")
        print(f"  {describe_source(path, sequence)}")
        print(f"  frames     : {len(sequence)}")
        print(f"  topology   : {sequence.topology.value}")
    print(f"\nplaying at   : {fps:.2f} fps")


def rows(comparison) -> list[dict]:
    """The per-frame error table, as one dict per frame."""
    table = []
    for position, frame in enumerate(comparison.frames):
        error = frame.error
        table.append(
            {
                "frame": position,
                "source_index": frame.decoded.frame_index,
                "timestamp": round(frame.decoded.timestamp, 6),
                "reference_vertices": len(frame.reference.positions),
                "reference_triangles": len(frame.reference.triangles),
                "decoded_vertices": len(frame.decoded.positions),
                "decoded_triangles": len(frame.decoded.triangles),
                "rms_decoded_to_reference": error.forward.rms,
                "rms_reference_to_decoded": error.backward.rms,
                "symmetric_rms": error.symmetric_rms,
                "hausdorff": error.hausdorff,
                "psnr_decoded_to_reference_db": error.forward.psnr_db,
                "psnr_reference_to_decoded_db": error.backward.psnr_db,
                "symmetric_psnr_db": error.symmetric_psnr_db,
            }
        )
    return table


def report_error(comparison) -> None:
    """Print the per-frame table and the sequence summary."""
    metric = (
        "point-to-point" if comparison.metric == "point" else "point-to-plane"
    )
    print(f"\n{metric} error, {len(comparison)} frames")
    if comparison.truncated_from:
        reference_count, decoded_count = comparison.truncated_from
        print(
            f"  note: lengths differ ({reference_count} reference, "
            f"{decoded_count} decoded); compared the first "
            f"{min(reference_count, decoded_count)}"
        )
    print(f"  psnr peak  : {comparison.peak:.6g} (reference bbox diagonal)")
    scale = (
        f"{comparison.clamp:.6g}"
        if comparison.percentile is None
        else f"{comparison.clamp:.6g} (p{comparison.percentile:g})"
    )
    print(f"  colour top : {scale}")

    header = (
        f"  {'frame':>5}  {'rms d→r':>11}  {'rms r→d':>11}  "
        f"{'sym rms':>11}  {'hausdorff':>11}  {'sym psnr':>9}"
    )
    print(f"\n{header}")
    print(f"  {'-' * (len(header) - 2)}")
    for row in rows(comparison):
        print(
            f"  {row['frame']:>5}  {row['rms_decoded_to_reference']:>11.6g}  "
            f"{row['rms_reference_to_decoded']:>11.6g}  "
            f"{row['symmetric_rms']:>11.6g}  {row['hausdorff']:>11.6g}  "
            f"{row['symmetric_psnr_db']:>9.2f}"
        )

    summary = comparison.summary()
    print(f"\n  sequence symmetric RMS : {summary.symmetric_rms:.6g}")
    print(f"  sequence Hausdorff     : {summary.hausdorff:.6g}")
    print(f"  mean symmetric PSNR    : {summary.mean_psnr_db:.2f} dB")
    print(f"  worst frame            : {summary.worst_frame}")


def write_csv(path: Path, comparison) -> Path:
    """Write the per-frame table, for a paper or a regression run."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(CSV_COLUMNS))
        writer.writeheader()
        writer.writerows(rows(comparison))
    return path


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=supported_formats(),
    )
    parser.add_argument(
        "reference",
        type=Path,
        nargs="?",
        help="the ground-truth sequence: a folder of per-frame meshes, or a "
        "USD container",
    )
    parser.add_argument(
        "decoded",
        type=Path,
        nargs="?",
        help="the sequence to measure against it, in any supported format",
    )
    parser.add_argument(
        "--metric",
        choices=("point", "plane"),
        default="point",
        help="point-to-point (default) or MPEG point-to-plane",
    )
    parser.add_argument(
        "--max-error",
        type=float,
        default=None,
        help="fix the top of the colour scale, in source units; by default the "
        "percentile below is used",
    )
    parser.add_argument(
        "--percentile",
        type=float,
        default=compare_frames.DEFAULT_PERCENTILE,
        help="percentile of all measured distances to put at the top of the "
        "colour scale; 100 uses the true maximum",
    )
    parser.add_argument(
        "--error-shading",
        type=float,
        default=0.25,
        help="how far the light may darken the error colours, 0-1; low by "
        "default so brightness reads as error, not as shadow",
    )
    parser.add_argument("--stride", type=int, default=1, help="keep every Nth frame")
    parser.add_argument(
        "--fps",
        type=float,
        default=None,
        help="override the frame rate for playback and for the saved GIF",
    )
    parser.add_argument(
        "--up",
        choices=UP_AXES,
        default=None,
        help="which data axis points up, applied to both sequences",
    )
    parser.add_argument(
        "--point-size", type=float, default=3.0, help="point-cloud marker size"
    )
    parser.add_argument(
        "--width", type=int, default=680, help="width of one pane"
    )
    parser.add_argument("--height", type=int, default=760, help="pane height")
    parser.add_argument(
        "--color",
        type=float,
        nargs=3,
        metavar=("R", "G", "B"),
        default=(0.95, 0.95, 0.97),
        help="surface colour in 0-1 for the panes that are not error maps",
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
        default=(0.1, 0.1, 0.12),
        help="background colour in 0-1; dark by default so the bright end of "
        "the error ramp stays visible",
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
        "--csv", type=Path, help="write the per-frame error table to a .csv file"
    )
    parser.add_argument(
        "--info",
        action="store_true",
        help="report the error table and stop, without opening a window",
    )
    return parser


def validate(parser: argparse.ArgumentParser, args) -> None:
    if args.reference is None or args.decoded is None:
        # Full help rather than a one-line usage error: the epilog lists every
        # format, which is what someone pointing this at new data needs.
        parser.print_help()
        raise SystemExit(2)
    if args.stride < 1:
        parser.error("--stride must be at least 1")
    if args.fps is not None and args.fps <= 0:
        parser.error("--fps must be greater than zero")
    if args.max_error is not None and args.max_error <= 0:
        parser.error("--max-error must be greater than zero")
    if not 0.0 < args.percentile <= 100.0:
        parser.error("--percentile must be in (0, 100]")
    if not 0.0 <= args.error_shading <= 1.0:
        parser.error("--error-shading must be between 0 and 1")


def run(args) -> int:
    """Load, measure, report, and then either play or save. Returns an exit code."""
    reference_path = existing_source(args.reference)
    decoded_path = existing_source(args.decoded)

    def progress(done: int, total: int) -> None:
        print(f"\r  measured {done}/{total} frames", end="", flush=True)

    # A malformed frame in someone else's dataset is ordinary, not a crash, so
    # report it as an error naming the file rather than a traceback.
    try:
        with open_sequence(reference_path, fps=args.fps) as reference, \
                open_sequence(decoded_path, fps=args.fps) as decoded:
            fps = resolve_fps(reference, args.fps)
            args.fps = fps
            report_sources(reference, decoded, reference_path, decoded_path, fps)
            if not len(reference) or not len(decoded):
                raise SystemExit("both sequences must contain at least one frame")

            up = resolve_up(reference, args.up)
            print()
            comparison = compare_frames.compare_sequences(
                reference,
                decoded,
                stride=args.stride,
                order=UP_TO_Z[up],
                metric=args.metric,
                max_error=args.max_error,
                percentile=(
                    None if args.percentile >= 100.0 else args.percentile
                ),
                progress=progress,
            )
            print()
    except (Open4DError, CodecError, ValueError, TypeError, OSError) as error:
        raise SystemExit(f"\nfailed to compare the sequences:\n  {error}") from None

    report_error(comparison)

    if args.csv:
        written = write_csv(args.csv, comparison)
        print(f"\nwrote {written}")

    if args.info:
        return 0

    import viewer_compare_qt

    if args.save:
        viewer_compare_qt.record(comparison, args, args.save)
    else:
        viewer_compare_qt.play(comparison, args)
    return 0


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    validate(parser, args)
    raise SystemExit(run(args))


if __name__ == "__main__":
    main()
