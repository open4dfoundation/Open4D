#!/usr/bin/env bash
# Verify the vendored trees and build everything the two Gaussian-splatting
# methods need.
#
# Run from anywhere inside the checkout, with the open4d-gs environment active:
#
#   ./scripts/setup.sh                  # verify trees and patches, build extensions
#   ./scripts/setup.sh --tinycudann     # also build 3DGStream's NTC dependency
#   ./scripts/setup.sh --midas-weights  # also fetch the 1.5 GB MiDaS checkpoint
#   ./scripts/setup.sh --no-build       # verify only
#
# Idempotent: every step checks whether it has already been done, so re-running
# after a failure resumes rather than duplicating work.
set -euo pipefail

module_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_root="$(git -C "$module_root" rev-parse --show-toplevel)"
# QUEEN and 3DGStream are siblings of this module, not children of it. `paths.py`
# resolves them the same way; keep the two in step.
queen="$module_root/../queen"
gstream="$module_root/../3dgstream"
rast="$module_root/rasterizers"
simple_knn="$module_root/simple-knn"

build=1
tinycudann=0
midas_weights=0
for arg in "$@"; do
  case "$arg" in
    --no-build) build=0 ;;
    --tinycudann) tinycudann=1 ;;
    --midas-weights) midas_weights=1 ;;
    --rebuild) build=1 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

say() { printf '\n== %s\n' "$*"; }

# --- vendored trees ------------------------------------------------------
#
# These were pinned submodules under `upstream/` until they were vendored as
# plain tracked files: the two methods now sit beside this module under
# open4d/reconstruction/, and the three rasterizers, the single simple-knn, and
# the single glm copy live inside it. So nothing is fetched here anymore -- a
# checkout either has these trees or is not a complete checkout of this
# repository. SIBR_viewers is tracked but deliberately never built: it needs its
# own C++ toolchain and Open4D has examples/visualization/.
say "vendored trees"
for tree in "$queen" "$gstream" "$simple_knn" \
  "$rast/gaussian-rasterization-grad" \
  "$rast/diff-gaussian-rasterization" \
  "$rast/gstream-rasterization"; do
  if [ ! -d "$tree" ]; then
    echo "  MISSING: $tree" >&2
    echo "  this needs a complete checkout of the repository; see ../README.md" >&2
    exit 1
  fi
  echo "  present: $(cd "$tree" && pwd | sed "s|^$repo_root/||")"
done

# --- patch series --------------------------------------------------------
#
# The vendored trees are committed in their patched state, so there is nothing to
# apply: these files are tracked by Open4D itself now, and modifying them here
# would leave the whole repository dirty rather than one submodule. The series is
# still worth keeping and still worth running, as a detector -- `git apply
# --check --reverse` succeeds only against a tree that already carries the patch,
# so a failure here means a vendored tree drifted from what THIRD_PARTY.md
# records, and no build should be trusted on top of it.
verify_patches() {
  local tree="$1" series="$2"
  [ -d "$series" ] || return 0
  # Run from the repository root with --directory, never `git -C "$tree" apply`.
  # Paths in these patches are relative to the tree, but `git apply` invoked from
  # a subdirectory resolves them against the repository root and silently ignores
  # whatever falls outside the current directory -- so it matches no file and
  # reports success for both polarities. That made this step a no-op from the
  # moment the trees stopped being submodules, which is how the QUEEN patch below
  # went missing unnoticed. The --numstat guard keeps that failure mode visible.
  local prefix; prefix="$(cd "$tree" && pwd)"; prefix="${prefix#"$repo_root"/}"
  shopt -s nullglob
  for patch in "$series"/*.patch; do
    local name; name="$(basename "$patch")"
    if [ "$(git -C "$repo_root" apply --numstat --directory="$prefix" "$patch" \
              2>/dev/null | wc -l)" -eq 0 ]; then
      echo "  BROKEN: $name names no file under $prefix" >&2
      echo "  the patch series and the vendored layout disagree" >&2
      exit 1
    fi
    if git -C "$repo_root" apply --check --reverse --directory="$prefix" \
         "$patch" 2>/dev/null; then
      echo "  present: $name"
    else
      echo "  MISSING: $name is not applied in $prefix" >&2
      echo "  the vendored tree drifted from THIRD_PARTY.md; do not build on it" >&2
      exit 1
    fi
  done
  shopt -u nullglob
}

say "patches"
verify_patches "$queen" "$module_root/patches/queen"
verify_patches "$gstream" "$module_root/patches/3dgstream"
verify_patches "$rast/gstream-rasterization" "$module_root/patches/3dgstream-rasterizer"

if [ "$build" -eq 0 ]; then
  say "done (--no-build)"
  exit 0
fi

# --- compiled extensions -------------------------------------------------
#
# --no-build-isolation is required, not a preference: these setup.py files import
# torch to find CUDAExtension, and build isolation gives them an empty
# environment where that import fails.
if ! command -v nvcc >/dev/null 2>&1; then
  echo "nvcc is not on PATH. Point /usr/local/cuda at 12.6 and add its bin," >&2
  echo "as ../../../requirements-gpu.txt describes; the extensions need it." >&2
  exit 1
fi

# Without build isolation the build backend runs in this environment, so what it
# imports has to be here. tiny-cuda-nn's setup.py imports pkg_resources, which
# setuptools 81 deprecated and 82 removed -- so a current setuptools is present
# and the build still dies on `No module named 'pkg_resources'`, which reads like
# a missing package rather than one that is too new.
python -c "import pkg_resources" 2>/dev/null || pip install "setuptools<81"

install_ext() {
  local import_name="$1" source_dir="$2"
  if python -c "import $import_name" 2>/dev/null; then
    echo "  already built: $import_name"
    return 0
  fi
  echo "  building $import_name from ${source_dir#"$module_root"/}"
  pip install --no-build-isolation "$source_dir"
}

say "CUDA extensions"
# One simple-knn for both methods: QUEEN's copy, whose added <float.h>/<cfloat>
# includes are what let it compile here at all.
install_ext simple_knn "$simple_knn"
# The intended survivor: QUEEN's grad fork, already a functional superset of
# 3DGStream's. Phase 2 of docs/plan.md repoints 3DGStream at it and deletes the
# two below. All three find glm through ../../glm/ in their own setup.py.
install_ext gaussian_rasterization_grad "$rast/gaussian-rasterization-grad"
# Kept until the parity test passes: QUEEN's plain rasterizer (inria's, plus a
# different in_frustum near plane) and 3DGStream's, renamed by the vendored
# patch so both can be installed at once.
install_ext diff_gaussian_rasterization "$rast/diff-gaussian-rasterization"
install_ext gstream_rasterization "$rast/gstream-rasterization"

if [ "$tinycudann" -eq 1 ]; then
  say "tiny-cuda-nn (3DGStream NTC)"
  if python -c "import tinycudann" 2>/dev/null; then
    echo "  already built"
  else
    # 89 is the RTX 4090; override TCNN_CUDA_ARCHITECTURES for other hardware.
    # tiny-cuda-nn hard-fails on gcc > 12 with CUDA 12.6.
    TCNN_CUDA_ARCHITECTURES="${TCNN_CUDA_ARCHITECTURES:-89}" \
      pip install --no-build-isolation \
      "git+https://github.com/NVlabs/tiny-cuda-nn/#subdirectory=bindings/torch"
  fi
fi

if [ "$midas_weights" -eq 1 ]; then
  say "MiDaS weights"
  weights="$queen/MiDaS/weights/dpt_beit_large_512.pt"
  if [ -f "$weights" ]; then
    echo "  already present: $weights"
  else
    mkdir -p "$(dirname "$weights")"
    curl -fL --progress-bar -o "$weights" \
      https://github.com/isl-org/MiDaS/releases/download/v3_1/dpt_beit_large_512.pt
  fi
fi

say "environment"
if command -v gs-tools >/dev/null 2>&1; then
  gs-tools doctor || true
else
  echo "gs-tools is not on PATH; run: pip install -e $module_root"
fi
