#!/usr/bin/env bash
# N4MC environment setup — Ubuntu + CUDA GPU.
# Creates (or updates) the conda env declared in environment.yml and verifies
# that torch sees the GPU.
#
#   bash setup.sh                 # create/update env named "pytorch"
#   ENV_NAME=myenv bash setup.sh  # use a different env name
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_NAME="${ENV_NAME:-pytorch}"

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
  echo "ERROR: conda not found. Install Miniconda/Anaconda first." >&2
  exit 1
fi
# shellcheck disable=SC1091
source "$(conda info --base)/etc/profile.d/conda.sh"

if conda env list | awk '{print $1}' | grep -qx "${ENV_NAME}"; then
  echo "[setup] env '${ENV_NAME}' already exists — updating from environment.yml"
  conda env update -n "${ENV_NAME}" -f "${SCRIPT_DIR}/environment.yml"
else
  echo "[setup] creating env '${ENV_NAME}' from environment.yml"
  conda env create -n "${ENV_NAME}" -f "${SCRIPT_DIR}/environment.yml"
fi

conda activate "${ENV_NAME}"
python - <<'PY'
import torch
print(f"[setup] torch {torch.__version__} | CUDA available: {torch.cuda.is_available()}")
if torch.cuda.is_available():
    print(f"[setup] GPU: {torch.cuda.get_device_name(0)}")
PY

echo "[setup] Steps 3 & 5 additionally need a system CUDA toolkit (nvcc), then:"
echo "          pip install git+https://github.com/NVlabs/nvdiffrast.git   # Step 3"
echo "          pip install pytorch3d                                       # Step 5"
echo "[setup] done. Activate with: conda activate ${ENV_NAME}"
