#!/usr/bin/env bash
# N4MC end-to-end run — Ubuntu + CUDA GPU.
# Generates TSDF-Def tensors (if missing) and trains the auto-decoder.
#
#   bash run.sh
#   MESH_DIR=/path/to/frames bash run.sh     # use your own mesh sequence
#
# Override via env vars: ENV_NAME, MESH_DIR, CONFIG, VOXEL_RES.
# NOTE: if you change the number of frames, update `num_frames` in the config.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_NAME="${ENV_NAME:-pytorch}"
SRC="${SCRIPT_DIR}/n4mc_source"

MESH_DIR="${MESH_DIR:-${SCRIPT_DIR}/../tvmc/arap-volume-tracking/data/basketball_player}"
CONFIG="${CONFIG:-../configs/configs_128.txt}"
VOXEL_RES="${VOXEL_RES:-127}"

# Locate conda even in a non-interactive shell (PATH may not include it).
if ! command -v conda >/dev/null 2>&1; then
  for c in "$HOME/miniconda3" "$HOME/anaconda3" "$HOME/miniforge3" "/opt/conda"; do
    if [ -f "$c/etc/profile.d/conda.sh" ]; then
      # shellcheck disable=SC1091
      source "$c/etc/profile.d/conda.sh"
      break
    fi
  done
fi
if ! command -v conda >/dev/null 2>&1; then
  echo "ERROR: conda not found. Run setup.sh first / install Miniconda." >&2
  exit 1
fi
# shellcheck disable=SC1091
source "$(conda info --base)/etc/profile.d/conda.sh"
conda activate "${ENV_NAME}"

cd "${SRC}"

if [ ! -f "./TSDF_128/data/0000.npz" ]; then
  echo "[run] generating TSDF tensors from: ${MESH_DIR}"
  python gen_tsdf_from_meshes.py \
      --mesh_dir "${MESH_DIR}" \
      --save_path "./TSDF_128" \
      --voxel_grid_res "${VOXEL_RES}"
else
  echo "[run] found ./TSDF_128/data — skipping generation (delete it to regenerate)"
fi

echo "[run] training: python train_quant.py --config=${CONFIG}"
python train_quant.py --config="${CONFIG}"
