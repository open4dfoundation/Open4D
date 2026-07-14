#!/usr/bin/env bash
# Complete N4MC user pipeline:
# raw mesh sequence -> TSDF -> training -> decoded meshes -> metrics -> .n4mc
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_NAME="${ENV_NAME:-pytorch}"
MESH_DIR="${MESH_DIR:-${SCRIPT_DIR}/../tvmc/arap-volume-tracking/data/basketball_player}"
OUTPUT_DIR="${OUTPUT_DIR:-${SCRIPT_DIR}/outputs/basketball}"
ARCHIVE="${ARCHIVE:-${OUTPUT_DIR}/sequence.n4mc}"
FRAMES="${FRAMES:-0}"
VOXEL_RES="${VOXEL_RES:-127}"
EPOCHS="${EPOCHS:-500}"
SAMPLES="${SAMPLES:-10000}"

if [ "${SMOKE:-0}" = "1" ]; then
  FRAMES="${FRAMES_SMOKE:-1}"
  VOXEL_RES="${VOXEL_RES_SMOKE:-15}"
  EPOCHS="${EPOCHS_SMOKE:-1}"
  SAMPLES="${SAMPLES_SMOKE:-500}"
  OUTPUT_DIR="${OUTPUT_DIR_SMOKE:-${SCRIPT_DIR}/outputs/smoke}"
  ARCHIVE="${ARCHIVE_SMOKE:-${OUTPUT_DIR}/sequence.n4mc}"
fi

if ! command -v conda >/dev/null 2>&1; then
  for c in "$HOME/miniconda3" "$HOME/anaconda3" "$HOME/miniforge3" /opt/conda; do
    if [ -f "$c/etc/profile.d/conda.sh" ]; then
      # shellcheck disable=SC1090
      source "$c/etc/profile.d/conda.sh"
      break
    fi
  done
fi
if ! command -v conda >/dev/null 2>&1; then
  echo "ERROR: conda not found; run setup.sh first." >&2
  exit 1
fi
# shellcheck disable=SC1091
source "$(conda info --base)/etc/profile.d/conda.sh"
conda activate "$ENV_NAME"

cmd=(python "$SCRIPT_DIR/pipeline.py"
  --mesh-dir "$MESH_DIR"
  --output-dir "$OUTPUT_DIR"
  --archive "$ARCHIVE"
  --frames "$FRAMES"
  --voxel-res "$VOXEL_RES"
  --epochs "$EPOCHS"
  --samples "$SAMPLES"
  --from "${FROM_STAGE:-prepare}"
  --to "${TO_STAGE:-package}")

if [ "${FORCE:-0}" = "1" ]; then cmd+=(--force); fi
if [ "${DRY_RUN:-0}" = "1" ]; then cmd+=(--dry-run); fi

printf '[run] input: %s\n' "$MESH_DIR"
printf '[run] output: %s\n' "$OUTPUT_DIR"
printf '[run] frames=%s resolution=%s epochs=%s\n' "$FRAMES" "$VOXEL_RES" "$EPOCHS"
"${cmd[@]}"
