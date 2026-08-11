#!/usr/bin/env bash
#
# Run the full TSMC pipeline over one dataset, one volume-center group at a time.
#
# Build the .NET components first with ./setup.sh.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat >&2 <<'USAGE'
Usage: ./run.sh DATASET [options]

  DATASET               Name of the sequence to compress. Must match the
                        directory under ./data/ and the *_2000 directories the
                        TVM editor reads, e.g. `answering`.

Options:
  --num-frames N        Frames per group (default: 10)
  --num-centers N       Volume centers (default: 2000)
  --groups A B ...      Group indices to run (default: 1..10)
  --mesh-path PATH      Input .obj frames, relative to ./tsmc
                        (default: ../data/<DATASET>/meshes)
  --centers-dir PATH    Volume centers, relative to ./tsmc
                        (default: ../arap-volume-tracking/data/combined-100-max-2000)
  --target-mesh-path P  Target meshes for displacements, relative to ./tsmc
                        (default: ../arap-volume-tracking/data/combined_scaled)
  -h, --help            Show this message

Environment:
  PYTHON                Python interpreter (default: python)
  TSMC_EDITOR_BUILD     Editor output dir relative to ./tvm-editing
                        (default: TVMEditor.Test/bin/Release/net10.0)
USAGE
  exit 2
}

[[ $# -ge 1 ]] || usage
case "$1" in -h|--help) usage ;; esac

DATASET="$1"; shift
NUM_FRAMES=10
NUM_CENTERS=2000
GROUP_INDICES=()
CENTERS_DIR="../arap-volume-tracking/data/combined-100-max-2000"
MESH_PATH="../data/DATASET_PLACEHOLDER/meshes"
TARGET_MESH_PATH="../arap-volume-tracking/data/combined_scaled"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --num-frames) NUM_FRAMES="$2"; shift 2 ;;
    --num-centers) NUM_CENTERS="$2"; shift 2 ;;
    --centers-dir) CENTERS_DIR="$2"; shift 2 ;;
    --mesh-path) MESH_PATH="$2"; shift 2 ;;
    --target-mesh-path) TARGET_MESH_PATH="$2"; shift 2 ;;
    --groups)
      shift
      while [[ $# -gt 0 && "$1" != --* ]]; do GROUP_INDICES+=("$1"); shift; done
      ;;
    -h|--help) usage ;;
    *) echo "error: unknown option: $1" >&2; usage ;;
  esac
done
[[ ${#GROUP_INDICES[@]} -gt 0 ]] || GROUP_INDICES=(1 2 3 4 5 6 7 8 9 10)

MESH_PATH="${MESH_PATH/DATASET_PLACEHOLDER/$DATASET}"

if [[ -z "${DOTNET_ROOT:-}" ]] && command -v dotnet >/dev/null 2>&1; then
  _dotnet="$(command -v dotnet)"
  while [[ -L "$_dotnet" ]]; do
    _link="$(readlink "$_dotnet")"
    case "$_link" in /*) _dotnet="$_link" ;; *) _dotnet="$(dirname "$_dotnet")/$_link" ;; esac
  done
  DOTNET_ROOT="$(cd "$(dirname "$_dotnet")" && pwd)"
  export DOTNET_ROOT
fi

PYTHON="${PYTHON:-python}"

# The one place the target framework appears. TVMEditor.Test.csproj targets
# net10.0, so `dotnet build -c Release` writes bin/Release/net10.0. This used to
# read net5.0 at nine separate call sites, which no build has produced since the
# projects moved off .NET 5.
TSMC_EDITOR_BUILD="${TSMC_EDITOR_BUILD:-TVMEditor.Test/bin/Release/net10.0}"

EDITING="$ROOT/tvm-editing"
TSMC="$ROOT/tsmc"
EDITOR_DLL="$EDITING/$TSMC_EDITOR_BUILD/TVMEditor.Test.dll"
# Path to the same directory as seen from ./tsmc, which is where the Python
# steps run and what their arguments are relative to.
EDITOR_FROM_TSMC="../tvm-editing/$TSMC_EDITOR_BUILD"

FIRST_INDEX=0
LAST_INDEX=$((NUM_FRAMES - 1))
SUFFIX="${DATASET}_${NUM_CENTERS}"

# Preflight. Every one of these is something the pipeline would otherwise fail
# on several minutes and several steps later.
if [[ ! -f "$EDITOR_DLL" ]]; then
  echo "error: TVM editor not found at $EDITOR_DLL" >&2
  echo "Build it with ./setup.sh, or set TSMC_EDITOR_BUILD if the project's" >&2
  echo "target framework has changed." >&2
  exit 1
fi
if [[ ! -d "$TSMC/$CENTERS_DIR" ]]; then
  echo "error: volume centers not found at $TSMC/$CENTERS_DIR" >&2
  echo "Generate them with arap-volume-tracking (see README.md step 2), or pass" >&2
  echo "--centers-dir. No centers ship with the repository." >&2
  exit 1
fi
if [[ ! -d "$TSMC/$MESH_PATH" ]]; then
  echo "error: input frames not found at $TSMC/$MESH_PATH" >&2
  echo "Pass --mesh-path, or produce them per README.md step 1." >&2
  exit 1
fi
if [[ ! -d "$ROOT/data/$DATASET" ]]; then
  echo "error: no dataset directory at $ROOT/data/$DATASET" >&2
  echo "Step 1 of README.md produces ./data/<dataset>/{dynamic,static}." >&2
  exit 1
fi

for GROUP_IDX in "${GROUP_INDICES[@]}"; do
  echo "======================"
  echo " Running group $GROUP_IDX"
  echo "======================"

  # --- Step 1: Get reference center ---
  ( cd "$TSMC" && "$PYTHON" ./get_reference_center.py \
      --dataset "$DATASET" --num_frames "$NUM_FRAMES" --num_centers "$NUM_CENTERS" \
      --centers_dir "$CENTERS_DIR" \
      --group_idx "$GROUP_IDX" )
     # --random_state 19056

  # --- Step 2: Get transformation ---
  ( cd "$TSMC" && "$PYTHON" ./get_transformation.py \
      --dataset "$DATASET" --num_frames "$NUM_FRAMES" --num_centers "$NUM_CENTERS" \
      --centers_dir "$CENTERS_DIR" \
      --mesh_path "$MESH_PATH" \
      --firstIndex "$FIRST_INDEX" --lastIndex "$LAST_INDEX" \
      --group_idx "$GROUP_IDX" )

  # --- Step 3: Run TVMEditor (stage 1) ---
  ( cd "$EDITING" && dotnet "$TSMC_EDITOR_BUILD/TVMEditor.Test.dll" "$DATASET" 1 "$FIRST_INDEX" "$LAST_INDEX" \
      "./$TSMC_EDITOR_BUILD/Data/${SUFFIX}/" \
      "./$TSMC_EDITOR_BUILD/output/${SUFFIX}/" )

  # --- Step 4: Extract reference mesh ---
  ( cd "$TSMC" && "$PYTHON" ./extract_reference_mesh.py \
      --dataset "$DATASET" --num_frames "$NUM_FRAMES" --num_centers "$NUM_CENTERS" \
      --inputDir "$EDITOR_FROM_TSMC/output/${SUFFIX}/output/" \
      --outputDir "$EDITOR_FROM_TSMC/Data/${SUFFIX}/reference_mesh/" \
      --firstIndex "$FIRST_INDEX" --lastIndex "$LAST_INDEX" --key 4 )

  # extract_reference_mesh.py cannot be trusted to report its own failure. Open3D
  # vendors PoissonRecon, whose error path terminates the process with status 0,
  # so a surface it cannot close leaves this step "successful" with nothing
  # written -- and stage 2 below then dies on an IndexOutOfRangeException that
  # points nowhere near the cause. Check the artifact, not the exit code.
  if [[ ! -s "$EDITING/$TSMC_EDITOR_BUILD/Data/${SUFFIX}/reference_mesh/decimated_reference_mesh.obj" ]]; then
    echo "error: step 4 wrote no reference mesh for group $GROUP_IDX." >&2
    echo "If the log shows Poisson 'Failed to close loop', the deformed centers" >&2
    echo "did not yield a closeable surface -- check the volume-tracking" >&2
    echo "resolution and point count that produced --centers-dir." >&2
    exit 1
  fi

  # --- Step 5: Run TVMEditor (stage 2) ---
  ( cd "$EDITING" && dotnet "$TSMC_EDITOR_BUILD/TVMEditor.Test.dll" "$DATASET" 2 "$FIRST_INDEX" "$LAST_INDEX" \
      "./$TSMC_EDITOR_BUILD/Data/${SUFFIX}" \
      "./$TSMC_EDITOR_BUILD/output/${SUFFIX}" )

  # --- Step 6: Displacements ---
  ( cd "$TSMC" && "$PYTHON" ./get_displacements.py \
      --dataset "$DATASET" --num_frames "$NUM_FRAMES" --num_centers "$NUM_CENTERS" \
      --target_mesh_path "$TARGET_MESH_PATH" \
      --firstIndex "$FIRST_INDEX" --lastIndex "$LAST_INDEX" \
      --group_idx "$GROUP_IDX" )

  # --- Step 7: Compress displacements ---
  ( cd "$TSMC" && "$PYTHON" compress_displacements.py \
      --dataset "$DATASET" --num_frames "$NUM_FRAMES" --num_eigenvectors 5 \
      --displacement_path "$EDITOR_FROM_TSMC/output/${SUFFIX}/reference" \
      --output_path "$EDITOR_FROM_TSMC/output/${SUFFIX}/reference" \
      --firstIndex "$FIRST_INDEX" --lastIndex "$LAST_INDEX" \
      --reference_mesh_path "$EDITOR_FROM_TSMC/Data/${SUFFIX}/reference_mesh/others/decoded_decimated_reference_mesh.obj" )

  # --- Step 8: Evaluation ---
  ( cd "$TSMC" && "$PYTHON" evaluation.py \
      --dataset "$DATASET" --num_frames "$NUM_FRAMES" --num_centers "$NUM_CENTERS" \
      --input_path "$EDITOR_FROM_TSMC/output/${SUFFIX}/reference" \
      --dynamic_static_path "../data/$DATASET/meshes" \
      --firstIndex "$FIRST_INDEX" --lastIndex "$LAST_INDEX" \
      --reference_mesh_path "$EDITOR_FROM_TSMC/Data/${SUFFIX}/reference_mesh/others/decoded_decimated_reference_mesh.obj" \
      --group_idx "$GROUP_IDX" )
done
