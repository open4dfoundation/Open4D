#!/usr/bin/env python3
"""Reconstruct a full-scene mesh from saved synchronized two-camera frames.

This is the no-live-sender path: it replays saved synchronized capture pairs
through the same fusion code the live receiver uses, so it needs no cameras and
no network.

Capture data lives outside the repository (see docs/artifacts.md). Point the
script at your capture set in one of three ways, in order of precedence:

  1. --capture-root /path/to/captures
  2. export FOURD_CAPTURE_ROOT=/path/to/captures
  3. place or symlink the capture set at <module>/datasets/two-camera

The capture root is expected to contain a calibration directory and a metadata
directory; override the individual paths below if your layout differs.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import sys
from pathlib import Path

import cv2
import numpy as np
import open3d as o3d

MODULE_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FUSION_SOURCE = MODULE_ROOT / "python" / "live_two_camera_fusion.py"
DEFAULT_CAPTURE_ROOT = MODULE_ROOT / "datasets" / "two-camera"
DEFAULT_CALIBRATION_NAME = "calibration_2026-07-29"
DEFAULT_METADATA_NAME = "captures_dense_attempt2_20260729"
DEFAULT_RAW_NAME = "dense_attempt2_raw"


def resolve_capture_root(explicit: Path | None) -> Path:
    if explicit is not None:
        return explicit
    from_env = os.environ.get("FOURD_CAPTURE_ROOT")
    if from_env:
        return Path(from_env)
    return DEFAULT_CAPTURE_ROOT


def load_live_module(path: Path):
    if not path.is_file():
        raise SystemExit(
            f"fusion implementation not found: {path}\n"
            "Pass --fusion-source, or check that python/live_two_camera_fusion.py "
            "is present in the module."
        )
    sys.path.insert(0, str(path.resolve().parent))
    spec = importlib.util.spec_from_file_location("live_two_camera_fusion", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import fusion implementation: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--capture-root",
        type=Path,
        default=None,
        help="directory holding the calibration and capture directories "
        "(default: $FOURD_CAPTURE_ROOT, else <module>/datasets/two-camera)",
    )
    parser.add_argument("--calibration-dir", type=Path, default=None,
                        help=f"default: <capture-root>/{DEFAULT_CALIBRATION_NAME}")
    parser.add_argument("--raw-root", type=Path, default=None,
                        help=f"default: <calibration-dir>/{DEFAULT_RAW_NAME}")
    parser.add_argument("--metadata-root", type=Path, default=None,
                        help=f"default: <capture-root>/{DEFAULT_METADATA_NAME}")
    parser.add_argument("--start", type=int, default=84)
    parser.add_argument("--end", type=int, default=90)
    parser.add_argument("--fusion-source", type=Path, default=DEFAULT_FUSION_SOURCE)
    parser.add_argument("--ey-factory", type=Path, default=None)
    parser.add_argument("--j3-factory", type=Path, default=None)
    parser.add_argument("--j3-to-ey", type=Path, default=None)
    parser.add_argument("--output-dir", type=Path,
                        default=MODULE_ROOT / "output" / "two-camera-fusion")
    parser.add_argument("--voxel", type=float, default=0.006)
    parser.add_argument("--truncation", type=float, default=0.03)
    args = parser.parse_args()

    capture_root = resolve_capture_root(args.capture_root)
    if args.calibration_dir is None:
        args.calibration_dir = capture_root / DEFAULT_CALIBRATION_NAME
    if args.raw_root is None:
        args.raw_root = args.calibration_dir / DEFAULT_RAW_NAME
    if args.metadata_root is None:
        args.metadata_root = capture_root / DEFAULT_METADATA_NAME
    factory = args.calibration_dir / "source/work/calibration_stepwise/factory"
    if args.ey_factory is None:
        args.ey_factory = factory / "ey_factory_calibration.json"
    if args.j3_factory is None:
        args.j3_factory = factory / "j3_factory_calibration.json"
    if args.j3_to_ey is None:
        args.j3_to_ey = (
            args.calibration_dir
            / "final_validated_fusion/j3_depth_to_ey_depth_refined.txt"
        )
    args.capture_root = capture_root

    if args.end < args.start:
        parser.error("--end must be at least --start")
    return args


def check_inputs(args: argparse.Namespace) -> None:
    """Fail early with an actionable message instead of a bare traceback."""
    required = {
        "capture root": args.capture_root,
        "raw frame directory (--raw-root)": args.raw_root,
        "metadata directory (--metadata-root)": args.metadata_root,
        "EY factory calibration (--ey-factory)": args.ey_factory,
        "J3 factory calibration (--j3-factory)": args.j3_factory,
        "J3-to-EY transform (--j3-to-ey)": args.j3_to_ey,
    }
    missing = [f"  {label}: {path}" for label, path in required.items()
               if not path.exists()]
    if missing:
        raise SystemExit(
            "Missing required capture inputs:\n"
            + "\n".join(missing)
            + "\n\nSet --capture-root or FOURD_CAPTURE_ROOT to the directory "
            "holding your\ncalibration and capture data. See the module README "
            "for the expected layout."
        )


def main() -> None:
    args = parse_args()
    check_inputs(args)
    fusion = load_live_module(args.fusion_source)
    ey = fusion.CameraProjector(args.ey_factory)
    j3 = fusion.CameraProjector(args.j3_factory)
    transform = fusion.load_transform(args.j3_to_ey)

    prepared = []
    sync_errors = []
    for number in range(args.start, args.end + 1):
        pair_dir = args.raw_root / f"pair_{number:012d}"
        metadata_path = args.metadata_root / f"pair_{number:012d}/metadata.json"
        metadata = json.loads(metadata_path.read_text())
        sync_errors.append(abs(int(metadata["sync_error_us"])))
        ey_depth = np.fromfile(
            pair_dir / "ey_depth_u16le.raw", dtype="<u2"
        ).reshape(fusion.HEIGHT, fusion.WIDTH)
        j3_depth = np.fromfile(
            pair_dir / "j3_depth_u16le.raw", dtype="<u2"
        ).reshape(fusion.HEIGHT, fusion.WIDTH)
        ey_color = cv2.imread(str(pair_dir / "ey_color.jpg"))
        j3_color = cv2.imread(str(pair_dir / "j3_color.jpg"))
        if ey_color is None or j3_color is None:
            raise RuntimeError(f"missing color frame in {pair_dir}")
        ey_depth_ready, ey_color_ready = ey.prepare_from_bgr(
            ey_depth, ey_color
        )
        j3_depth_ready, j3_color_ready = j3.prepare_from_bgr(
            j3_depth, j3_color
        )
        prepared.append(
            fusion.PreparedPair(
                number=number,
                sync_error_us=int(metadata["sync_error_us"]),
                ey_depth=ey_depth_ready,
                ey_color=ey_color_ready,
                j3_depth=j3_depth_ready,
                j3_color=j3_color_ready,
            )
        )

    mesh = fusion.build_mesh(
        prepared,
        ey,
        j3,
        transform,
        args.voxel,
        args.truncation,
    )
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output = args.output_dir / "saved_sequence_two_camera_fused_mesh.ply"
    if not o3d.io.write_triangle_mesh(
        str(output), mesh, write_ascii=False, compressed=True
    ):
        raise RuntimeError(f"failed to write {output}")

    vertices = np.asarray(mesh.vertices)
    triangles = np.asarray(mesh.triangles)
    report = {
        "status": "completed",
        "module": "4d-reconstruction",
        "former_module_name": "MeshReduce",
        "source_pairs": [args.start, args.end],
        "input_frames_per_camera": len(prepared),
        "hardware_synchronized": True,
        "absolute_sync_error_median_us": float(np.median(sync_errors)),
        "absolute_sync_error_max_us": int(max(sync_errors)),
        "fused_coordinate_frame": "EY depth camera",
        "j3_depth_to_ey_depth": transform.tolist(),
        "voxel_length_m": args.voxel,
        "sdf_truncation_m": args.truncation,
        "spatial_crop": False,
        "component_filtering": False,
        "geometry_decimation": False,
        "hole_filling": False,
        "vertices": int(len(vertices)),
        "triangles": int(len(triangles)),
        "bounds_min_m": vertices.min(axis=0).tolist(),
        "bounds_max_m": vertices.max(axis=0).tolist(),
        "output": str(output),
    }
    report_path = args.output_dir / "saved_sequence_fusion_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
