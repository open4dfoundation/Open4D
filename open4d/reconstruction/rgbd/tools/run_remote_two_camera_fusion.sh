#!/usr/bin/env bash
# Bounded two-camera reconstruction from a live remote capture host.
#
# Requires the capture host's sender to be running and its data tunnel to map
# the sender's 127.0.0.1:17000 to this machine. See REMOTE_TWO_CAMERA.md.
#
# Configuration (all overridable):
#   FOURD_CAPTURE_ROOT   directory holding your calibration set (required)
#   FOURD_CALIBRATION_DIR  calibration directory (default: <root>/calibration)
#   PYTHON               interpreter (default: python3)
#   OUTPUT_DIR           default: <module>/output/two-camera-fusion
#   MAX_PAIRS            frame pairs to accept before exiting (default: 30)
#   MESH_DEVICE          TSDF device (default: auto; uses CUDA when available)
#   MESH_BLOCK_COUNT     CUDA sparse-volume capacity (default: 20000)
set -euo pipefail

MODULE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FUSION="${FUSION:-$MODULE_ROOT/python/live_two_camera_fusion.py}"
PYTHON="${PYTHON:-python3}"
OUTPUT_DIR="${OUTPUT_DIR:-$MODULE_ROOT/output/two-camera-fusion}"

CAPTURE_ROOT="${FOURD_CAPTURE_ROOT:-$MODULE_ROOT/datasets/two-camera}"
CALIBRATION_ROOT="${FOURD_CALIBRATION_DIR:-$CAPTURE_ROOT/calibration}"

EY_FACTORY="$CALIBRATION_ROOT/source/work/calibration_stepwise/factory/ey_factory_calibration.json"
J3_FACTORY="$CALIBRATION_ROOT/source/work/calibration_stepwise/factory/j3_factory_calibration.json"
J3_TO_EY="$CALIBRATION_ROOT/final_validated_fusion/j3_depth_to_ey_depth_refined.txt"

missing=0
for required in "$FUSION" "$EY_FACTORY" "$J3_FACTORY" "$J3_TO_EY"; do
  if [[ ! -f "$required" ]]; then
    echo "missing required input: $required" >&2
    missing=1
  fi
done
if [[ "$missing" -ne 0 ]]; then
  cat >&2 <<'MESSAGE'

Set FOURD_CAPTURE_ROOT to the directory holding your calibration data, or
FOURD_CALIBRATION_DIR to the calibration directory directly. See the module
README for the expected layout.
MESSAGE
  exit 2
fi

mkdir -p "$OUTPUT_DIR"

exec "$PYTHON" "$FUSION" \
  --bind 127.0.0.1 \
  --port "${PORT:-17000}" \
  --headless \
  --max-pairs "${MAX_PAIRS:-30}" \
  --point-stride "${POINT_STRIDE:-2}" \
  --point-voxel "${POINT_VOXEL:-0.005}" \
  --mesh-window "${MESH_WINDOW:-7}" \
  --mesh-interval "${MESH_INTERVAL:-1.0}" \
  --mesh-voxel "${MESH_VOXEL:-0.006}" \
  --mesh-truncation "${MESH_TRUNCATION:-0.03}" \
  --mesh-device "${MESH_DEVICE:-auto}" \
  --mesh-block-count "${MESH_BLOCK_COUNT:-20000}" \
  --mesh-fusion-mode "${MESH_FUSION_MODE:-independent-merge}" \
  --mesh-merge-mode "${MESH_MERGE_MODE:-concatenate}" \
  --mesh-weld-radius "${MESH_WELD_RADIUS:-0.003}" \
  --ey-factory "$EY_FACTORY" \
  --j3-factory "$J3_FACTORY" \
  --j3-to-ey "$J3_TO_EY" \
  --output-dir "$OUTPUT_DIR" \
  "$@"
