#!/usr/bin/env bash
#
# run.sh - TSMC dynamic-compression pipeline (steps 2-10 of the README).
#
# This script runs the *deterministic* part of the pipeline end-to-end for one
# dataset: it builds the native dependencies if needed (Draco + the two .NET
# projects), runs ARAP volume tracking, and then executes the Python
# compression / evaluation steps.
#
# Step 1 (SAM3 static/dynamic decomposition) is a prerequisite - see the README.
# It produces the dynamic meshes consumed by ARAP
# (arap-volume-tracking/data/<dataset>/mesh_0000.obj ...) and the static
# background (data/<dataset>/meshes/static/static_backgrounds.drc) used by
# evaluation. If those are missing this script tells you and stops.
#
# Usage:
#   ./run.sh                         # runs the bundled "answering" sample (10 frames)
#   DATASET=synthetic ./run.sh       # a different dataset
#   NUM_FRAMES=10 NUM_CENTERS=2000 LAST=9 ./run.sh
#
# Common overrides (environment variables):
#   DATASET          dataset name                         (default: answering)
#   NUM_FRAMES       number of frames in the group        (default: 10)
#   NUM_CENTERS      tracked volume centers / pointCount  (default: 2000)
#   FIRST / LAST     first / last frame index             (default: 0 / 9)
#   GROUP            group index                          (default: 1)
#   NUM_EIGENVECTORS quality/bitrate trade-off            (default: 3)
#   PYTHON           python interpreter to use            (default: python)
#   SKIP_BUILD=1     skip the Draco/.NET build check
#   SKIP_ARAP=1      skip ARAP tracking (reuse existing output)
#
set -euo pipefail

DATASET="${DATASET:-answering}"
NUM_FRAMES="${NUM_FRAMES:-10}"
NUM_CENTERS="${NUM_CENTERS:-2000}"
FIRST="${FIRST:-0}"
LAST="${LAST:-9}"
GROUP="${GROUP:-1}"
NUM_EIGENVECTORS="${NUM_EIGENVECTORS:-3}"
PYTHON="${PYTHON:-python}"
SKIP_BUILD="${SKIP_BUILD:-0}"
SKIP_ARAP="${SKIP_ARAP:-0}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# .NET is used by both ARAP (net7.0) and TVMEditor (net5.0).
export DOTNET_ROOT="${DOTNET_ROOT:-$HOME/.dotnet}"
export PATH="$DOTNET_ROOT:$PATH"

ARAP="arap-volume-tracking"
NET_REL="TVMEditor.Test/bin/Release/net5.0"   # relative to the tvm-editing dir
NET="tvm-editing/$NET_REL"                    # relative to the module root
ARAP_OUT="$ARAP/output/${DATASET}-${NUM_CENTERS}"
ARAP_CONFIG="$ARAP/config/config-${DATASET}-max.xml"
DATA_NET="$NET/Data/${DATASET}_${NUM_CENTERS}"
OUT_NET="$NET/output/${DATASET}_${NUM_CENTERS}"
REF_MESH="$DATA_NET/reference_mesh/others/decoded_decimated_reference_mesh.obj"

log()  { printf '\n\033[1;34m==== %s ====\033[0m\n' "$*"; }
die()  { printf '\n\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 0. Native dependencies (Draco + .NET builds)
# ---------------------------------------------------------------------------
if [ "$SKIP_BUILD" != "1" ]; then
    if [ ! -x draco/build/draco_encoder ] || [ ! -f "$ARAP/bin/Client.dll" ] \
       || [ ! -f "$NET/TVMEditor.Test.dll" ]; then
        log "Building native dependencies (Draco + .NET)"
        bash setup.sh all
    fi
fi

# ---------------------------------------------------------------------------
# 1. SAM3 decomposition (prerequisite check)
# ---------------------------------------------------------------------------
if ! ls "$ARAP/data/${DATASET}"/*.obj >/dev/null 2>&1; then
    die "No dynamic meshes in $ARAP/data/${DATASET}/.
     These are produced by the SAM3 decomposition (README step 1).
     Run the notebook tsmc/sam3_mesh_segmentation.ipynb (or *_auto.ipynb) first,
     then place the dynamic meshes as $ARAP/data/${DATASET}/mesh_0000.obj ..."
fi
[ -f "$ARAP_CONFIG" ] || die "Missing ARAP config $ARAP_CONFIG"

# ---------------------------------------------------------------------------
# 2. ARAP volume tracking  (produces the per-frame volume centers)
# ---------------------------------------------------------------------------
expected=$(( LAST - FIRST + 1 ))
have=$(ls "$ARAP_OUT"/*res_*.xyz 2>/dev/null | wc -l | tr -d ' ')
if [ "$SKIP_ARAP" != "1" ] && [ "${have:-0}" -lt "$expected" ]; then
    log "ARAP volume tracking ($expected frames) - this is the slow step"
    ( cd "$ARAP" && dotnet ./bin/Client.dll "./config/config-${DATASET}-max.xml" )
else
    log "ARAP output present ($have frames) - skipping tracking"
fi
have=$(ls "$ARAP_OUT"/*res_*.xyz 2>/dev/null | wc -l | tr -d ' ')
[ "${have:-0}" -ge "$expected" ] || \
    die "ARAP produced only $have/$expected frames. The downstream .NET step needs
     as many tracked frames as meshes, or it fails with IndexOutOfRange."

cd tsmc

# ---------------------------------------------------------------------------
# 3. Reference centers
# ---------------------------------------------------------------------------
log "3. get_reference_center"
"$PYTHON" ./get_reference_center.py --dataset "$DATASET" --num_frames "$NUM_FRAMES" \
    --num_centers "$NUM_CENTERS" --centers_dir "../$ARAP_OUT" --group_idx "$GROUP"

# ---------------------------------------------------------------------------
# 4. Center transformations
# ---------------------------------------------------------------------------
log "4. get_transformation"
"$PYTHON" ./get_transformation.py --dataset "$DATASET" --num_frames "$NUM_FRAMES" \
    --num_centers "$NUM_CENTERS" --centers_dir "../$ARAP_OUT" \
    --firstIndex "$FIRST" --lastIndex "$LAST" --group_idx "$GROUP"

# ---------------------------------------------------------------------------
# 5. TVMEditor - deform each frame to the reference centers (mode 1)
# ---------------------------------------------------------------------------
log "5. TVMEditor (mode 1: frames -> reference shape)"
( cd ../tvm-editing && "./$NET_REL/TVMEditor.Test" "$DATASET" 1 "$FIRST" "$LAST" \
    "./$NET_REL/Data/${DATASET}_${NUM_CENTERS}/" "./$NET_REL/output/${DATASET}_${NUM_CENTERS}/" )

# ---------------------------------------------------------------------------
# 6. Extract the reference mesh
# ---------------------------------------------------------------------------
log "6. extract_reference_mesh"
"$PYTHON" ./extract_reference_mesh.py --dataset "$DATASET" --num_frames "$NUM_FRAMES" \
    --num_centers "$NUM_CENTERS" \
    --inputDir "../$OUT_NET/output/" \
    --outputDir "../$DATA_NET/reference_mesh/" \
    --firstIndex "$FIRST" --lastIndex "$LAST" --key 6

# ---------------------------------------------------------------------------
# 7. TVMEditor - deform the reference mesh back to each frame (mode 2)
# ---------------------------------------------------------------------------
log "7. TVMEditor (mode 2: reference mesh -> frames)"
( cd ../tvm-editing && "./$NET_REL/TVMEditor.Test" "$DATASET" 2 "$FIRST" "$LAST" \
    "./$NET_REL/Data/${DATASET}_${NUM_CENTERS}" "./$NET_REL/output/${DATASET}_${NUM_CENTERS}" )

# ---------------------------------------------------------------------------
# 8. Displacements (needs Draco)
# ---------------------------------------------------------------------------
log "8. get_displacements"
"$PYTHON" ./get_displacements.py --dataset "$DATASET" --num_frames "$NUM_FRAMES" \
    --num_centers "$NUM_CENTERS" --target_mesh_path "../$ARAP/data/${DATASET}" \
    --firstIndex "$FIRST" --lastIndex "$LAST" --group_idx "$GROUP"

# ---------------------------------------------------------------------------
# 9. Compress displacements
# ---------------------------------------------------------------------------
log "9. compress_displacements (num_eigenvectors=$NUM_EIGENVECTORS)"
"$PYTHON" compress_displacements.py --dataset "$DATASET" --num_frames "$NUM_FRAMES" \
    --num_eigenvectors "$NUM_EIGENVECTORS" \
    --displacement_path "../$OUT_NET/reference" \
    --output_path "../$OUT_NET/reference" \
    --firstIndex "$FIRST" --lastIndex "$LAST" \
    --reference_mesh_path "../$REF_MESH"

# ---------------------------------------------------------------------------
# 10. Evaluation (needs the SAM3 static background for full bitrate accounting)
# ---------------------------------------------------------------------------
log "10. evaluation"
"$PYTHON" evaluation.py --dataset "$DATASET" --num_frames "$NUM_FRAMES" \
    --num_centers "$NUM_CENTERS" \
    --input_path "../$OUT_NET/reference" \
    --dynamic_static_path "../data/${DATASET}/meshes" \
    --firstIndex "$FIRST" --lastIndex "$LAST" \
    --reference_mesh_path "../$REF_MESH" --group_idx "$GROUP"

log "Pipeline finished for dataset '${DATASET}'"
