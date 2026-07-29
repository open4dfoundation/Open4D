#!/usr/bin/env python3
"""Reconstruct a full-scene mesh from saved synchronized two-camera frames."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path

import cv2
import numpy as np
import open3d as o3d


def load_live_module(path: Path):
    sys.path.insert(0, str(path.resolve().parent))
    spec = importlib.util.spec_from_file_location("orbbec_live_fusion", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import fusion implementation: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def main() -> None:
    stream_root = Path("/home/ryan/camera_streaming_windows")
    calibration = stream_root / "calibration_2026-07-29"
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--raw-root",
        type=Path,
        default=calibration / "dense_attempt2_raw",
    )
    parser.add_argument("--start", type=int, default=84)
    parser.add_argument("--end", type=int, default=90)
    parser.add_argument(
        "--fusion-source",
        type=Path,
        default=stream_root / "live_two_camera_fusion.py",
    )
    parser.add_argument(
        "--ey-factory",
        type=Path,
        default=calibration
        / "source/work/calibration_stepwise/factory/ey_factory_calibration.json",
    )
    parser.add_argument(
        "--j3-factory",
        type=Path,
        default=calibration
        / "source/work/calibration_stepwise/factory/j3_factory_calibration.json",
    )
    parser.add_argument(
        "--j3-to-ey",
        type=Path,
        default=calibration
        / "final_validated_fusion/j3_depth_to_ey_depth_refined.txt",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1]
        / "output/two-camera-fusion",
    )
    parser.add_argument("--voxel", type=float, default=0.006)
    parser.add_argument("--truncation", type=float, default=0.03)
    args = parser.parse_args()

    if args.end < args.start:
        parser.error("--end must be at least --start")
    fusion = load_live_module(args.fusion_source)
    ey = fusion.CameraProjector(args.ey_factory)
    j3 = fusion.CameraProjector(args.j3_factory)
    transform = fusion.load_transform(args.j3_to_ey)

    prepared = []
    sync_errors = []
    for number in range(args.start, args.end + 1):
        pair_dir = args.raw_root / f"pair_{number:012d}"
        metadata_path = (
            calibration.parent
            / "captures_dense_attempt2_20260729"
            / f"pair_{number:012d}/metadata.json"
        )
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
