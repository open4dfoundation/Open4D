#!/usr/bin/env bash
#
# install_sam3.sh - install SAM3, used by the static/dynamic decomposition
# (README step 1). SAM3 is NOT on PyPI; it is installed from source.
# This is a prerequisite for a *fresh* dataset - the compression pipeline
# (run.sh) consumes the dynamic/static meshes that SAM3 produces.
#
# Requirements: an activated Python env (conda env `tsmc` or equivalent) with
# a CUDA-capable torch already installed, plus a GPU for inference.
#
#   ./install_sam3.sh                 # clone + pip install SAM3 into ./third_party
#   SAM3_DIR=/path/to/sam3 ./install_sam3.sh   # use an existing checkout
#
# After installing you still need the pretrained checkpoints - follow
# https://github.com/facebookresearch/sam3 and run the notebooks in ./tsmc/.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAM3_DIR="${SAM3_DIR:-$SCRIPT_DIR/third_party/sam3}"

if [ ! -d "$SAM3_DIR/.git" ]; then
    mkdir -p "$(dirname "$SAM3_DIR")"
    git clone https://github.com/facebookresearch/sam3.git "$SAM3_DIR"
fi

python -m pip install -e "$SAM3_DIR"

cat <<EOF

SAM3 installed from $SAM3_DIR.
Next:
  1. Download the pretrained checkpoints (see the SAM3 repo README).
  2. Run the decomposition notebooks in ./tsmc/ :
       - sam3_mesh_segmentation.ipynb        (answering)
       - sam3_mesh_segmentation_auto.ipynb   (synthetic, automatic dynamic part)
     Headless option:
       jupyter nbconvert --to notebook --execute tsmc/sam3_mesh_segmentation.ipynb
  3. This writes static/dynamic meshes under data/<dataset>/meshes/ and the
     dynamic meshes that ARAP consumes in arap-volume-tracking/data/<dataset>/.
EOF
