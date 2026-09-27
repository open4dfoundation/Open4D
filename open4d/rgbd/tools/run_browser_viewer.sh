#!/usr/bin/env bash
# Launch the repository-owned browser viewer for a remote two-camera stream.
set -euo pipefail

MODULE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON="${PYTHON:-python3}"
CAPTURE_ROOT="${FOURD_CAPTURE_ROOT:-$MODULE_ROOT/datasets/two-camera}"
CALIBRATION_ROOT="${FOURD_CALIBRATION_DIR:-$CAPTURE_ROOT/calibration}"
OUTPUT_DIR="${OUTPUT_DIR:-$MODULE_ROOT/output/two-camera-fusion}"

EY_FACTORY="$CALIBRATION_ROOT/source/work/calibration_stepwise/factory/ey_factory_calibration.json"
J3_FACTORY="$CALIBRATION_ROOT/source/work/calibration_stepwise/factory/j3_factory_calibration.json"
J3_TO_EY="$CALIBRATION_ROOT/final_validated_fusion/j3_depth_to_ey_depth_refined.txt"

for required in \
  "$MODULE_ROOT/python/live_two_camera_fusion.py" \
  "$MODULE_ROOT/python/live_two_camera_webrtc.py" \
  "$EY_FACTORY" \
  "$J3_FACTORY" \
  "$J3_TO_EY"; do
  if [[ ! -f "$required" ]]; then
    echo "missing required input: $required" >&2
    exit 2
  fi
done

mkdir -p "$OUTPUT_DIR"
export WEBRTC_IP="${WEBRTC_IP:-127.0.0.1}"
export WEBRTC_PORT="${WEBRTC_PORT:-8888}"

exec "$PYTHON" "$MODULE_ROOT/python/live_two_camera_webrtc.py" \
  --bind 127.0.0.1 \
  --port "${PORT:-17000}" \
  --point-stride "${POINT_STRIDE:-2}" \
  --point-voxel "${POINT_VOXEL:-0.005}" \
  --display-mode "${DISPLAY_MODE:-auto}" \
  --mesh-window "${MESH_WINDOW:-1}" \
  --mesh-interval "${MESH_INTERVAL:-4.0}" \
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
