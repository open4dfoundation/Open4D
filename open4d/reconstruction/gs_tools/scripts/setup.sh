#!/usr/bin/env bash
# Fetch, patch, and build everything the two Gaussian-splatting methods need.
#
# Run from anywhere inside the checkout, with the open4d-gs environment active:
#
#   ./scripts/setup.sh                  # submodules, patches, extensions
#   ./scripts/setup.sh --tinycudann     # also build 3DGStream's NTC dependency
#   ./scripts/setup.sh --midas-weights  # also fetch the 1.5 GB MiDaS checkpoint
#   ./scripts/setup.sh --no-build       # fetch and patch only
#
# Idempotent: every step checks whether it has already been done, so re-running
# after a failure resumes rather than duplicating work.
set -euo pipefail

module_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_root="$(git -C "$module_root" rev-parse --show-toplevel)"
queen="$module_root/upstream/queen"
gstream="$module_root/upstream/3dgstream"
gstream_rast="$gstream/submodules/diff-gaussian-rasterization"

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

# --- upstream checkouts ---------------------------------------------------
#
# QUEEN vendors everything it needs as plain files at the pinned commit --
# MiDaS, simple-knn, and both rasterizers -- so there is nothing nested to
# initialize. 3DGStream uses real submodules, of which we want exactly one: its
# rasterizer, recursively for third_party/glm. SIBR_viewers (both trees) and
# 3DGStream's simple-knn are deliberately skipped: the viewer needs its own C++
# toolchain and Open4D has examples/visualization/, and simple-knn is the same
# inria source QUEEN already vendors, built once below.
say "upstream checkouts"
git -C "$repo_root" submodule update --init \
  open4d/reconstruction/gs_tools/upstream/queen \
  open4d/reconstruction/gs_tools/upstream/3dgstream
git -C "$gstream" submodule update --init --recursive submodules/diff-gaussian-rasterization

# --- patch series --------------------------------------------------------
#
# Upstream is modified only by these patches, applied here. `git apply
# --check --reverse` is how the script tells "already applied" from "conflicts",
# so a partially patched tree is a hard error rather than a silent skip.
apply_patches() {
  local tree="$1" series="$2"
  [ -d "$series" ] || return 0
  shopt -s nullglob
  for patch in "$series"/*.patch; do
    local name; name="$(basename "$patch")"
    if git -C "$tree" apply --check --reverse "$patch" 2>/dev/null; then
      echo "  already applied: $name"
    elif git -C "$tree" apply --check "$patch" 2>/dev/null; then
      git -C "$tree" apply "$patch"
      echo "  applied: $name"
    else
      echo "  FAILED: $name does not apply to $tree" >&2
      echo "  the pin moved, or the tree was edited by hand; see THIRD_PARTY.md" >&2
      exit 1
    fi
  done
  shopt -u nullglob
}

say "patches"
apply_patches "$queen" "$module_root/patches/queen"
apply_patches "$gstream" "$module_root/patches/3dgstream"
apply_patches "$gstream_rast" "$module_root/patches/3dgstream-rasterizer"

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
# One simple-knn for both methods; QUEEN's vendored copy is the same inria source
# 3DGStream pins.
install_ext simple_knn "$queen/submodules/simple-knn"
# The intended survivor: QUEEN's grad fork, already a functional superset of
# 3DGStream's. Phase 2 of docs/plan.md repoints 3DGStream at it and deletes the
# two below.
install_ext gaussian_rasterization_grad "$queen/submodules/gaussian-rasterization-grad"
# Kept until the parity test passes: QUEEN's plain rasterizer (inria's, plus a
# different in_frustum near plane) and 3DGStream's, renamed by our patch so both
# can be installed at once.
install_ext diff_gaussian_rasterization "$queen/submodules/diff-gaussian-rasterization"
install_ext gstream_rasterization "$gstream_rast"

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
