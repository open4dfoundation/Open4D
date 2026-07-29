#!/usr/bin/env bash
set -euo pipefail

MODULE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STREAM_ROOT="${STREAM_ROOT:-/home/ryan/camera_streaming_windows}"
CALIBRATION_ROOT="${CALIBRATION_ROOT:-$STREAM_ROOT/calibration_2026-07-29}"
PYTHON="${PYTHON:-/home/ryan/miniconda3/envs/tsmc/bin/python}"
OUTPUT_DIR="${OUTPUT_DIR:-$MODULE_ROOT/output/two-camera-fusion}"

mkdir -p "$OUTPUT_DIR"

exec "$PYTHON" "$STREAM_ROOT/live_two_camera_fusion.py" \
  --bind 127.0.0.1 \
  --port 17000 \
  --headless \
  --max-pairs "${MAX_PAIRS:-30}" \
  --point-stride "${POINT_STRIDE:-2}" \
  --point-voxel "${POINT_VOXEL:-0.005}" \
  --mesh-window "${MESH_WINDOW:-7}" \
  --mesh-interval "${MESH_INTERVAL:-1.0}" \
  --mesh-voxel "${MESH_VOXEL:-0.006}" \
  --mesh-truncation "${MESH_TRUNCATION:-0.03}" \
  --ey-factory \
    "$CALIBRATION_ROOT/source/work/calibration_stepwise/factory/ey_factory_calibration.json" \
  --j3-factory \
    "$CALIBRATION_ROOT/source/work/calibration_stepwise/factory/j3_factory_calibration.json" \
  --j3-to-ey \
    "$CALIBRATION_ROOT/final_validated_fusion/j3_depth_to_ey_depth_refined.txt" \
  --output-dir "$OUTPUT_DIR" \
  "$@"
