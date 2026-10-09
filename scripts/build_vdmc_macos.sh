#!/usr/bin/env bash
# Build the MPEG V-DMC reference encoder/decoder (or the faster_vdmc fork) on
# macOS, including Apple Silicon.
#
# Usage:
#   scripts/build_vdmc_macos.sh vdmc|faster_vdmc [--source DIR] [--build-dir DIR]
#                               [--jobs N] [--revert]
#
#   --source DIR     V-DMC source tree. Default: the submodule in this checkout
#                    (open4d/codecs/vdmc or open4d/codecs/faster_vdmc).
#   --build-dir DIR  CMake build directory. Default: DIR/build/Release, the
#                    layout upstream build.sh uses. Binaries go to BUILD/bin.
#   --jobs N         Parallel build jobs. Default: logical CPU count.
#   --revert         Restore every file this script edited and exit.
#
# The upstream CMake files assume x86-64, so the script edits the source tree:
#   1. Removes the x86-only -m64, -mfpmath=sse, -msse* and -mavx* tokens from
#        CMakeLists.txt
#        dependencies/mmetric/CMakeLists.txt
#        dependencies/mmetric/dependencies/dmetric/source/CMakeLists.txt
#   2. Replaces stat64 with stat in
#        dependencies/mmetric/dependencies/dmetric/source/pcc_processing.cpp
#      (arm64 macOS has only the 64-bit stat).
#   3. faster_vdmc only: includes <spawn.h> at the top of source/app/encodeApp.cpp.
#      util/memory.hpp includes <mach/mach.h> inside namespace vmesh, so the
#      later global <spawn.h> cannot see cpu_type_t. Included first, the Mach
#      types land in the global namespace. Skipped when the file has no spawn.h.
# Each file is copied to FILE.open4d-orig before its first edit. The edits are
# idempotent, and --revert moves the copies back. The dependencies under
# dependencies/ are fetched by the first CMake configure, so they need network
# access (GitHub and vcgit.hhi.fraunhofer.de for HM). Fetching happens only once.
#
# It then configures with
#   -DCMAKE_POLICY_VERSION_MINIMUM=3.5   (CMake 4 rejects the old dependencies)
#   -DBITSTREAM_TRACE=OFF -DBUILD_WRAPPER_APPS=OFF
#   CMAKE_CXX_FLAGS=-I<source>/dependencies/mmetric/dependencies
#                   -I<source>/source/wrapper/colourConverter
#                   -I<source>/source/wrapper/videoEncoder
#   (the encode target omits these include directories)
# and builds only the encode and decode targets in Release.
set -euo pipefail

usage() {
  sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

log() {
  printf '==> %s\n' "$*"
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

codec=""
source_dir=""
build_dir=""
jobs=""
revert=0
while [ $# -gt 0 ]; do
  case "$1" in
    vdmc|faster_vdmc) codec="$1" ;;
    --source) [ $# -ge 2 ] || die "--source needs a directory"; source_dir="$2"; shift ;;
    --source=*) source_dir="${1#*=}" ;;
    --build-dir) [ $# -ge 2 ] || die "--build-dir needs a directory"; build_dir="$2"; shift ;;
    --build-dir=*) build_dir="${1#*=}" ;;
    --jobs|-j) [ $# -ge 2 ] || die "--jobs needs a number"; jobs="$2"; shift ;;
    --jobs=*) jobs="${1#*=}" ;;
    --revert) revert=1 ;;
    -h|--help) usage 0 ;;
    *) printf 'error: unknown argument: %s\n\n' "$1" >&2; usage 2 ;;
  esac
  shift
done
[ -n "$codec" ] || { printf 'error: choose vdmc or faster_vdmc\n\n' >&2; usage 2; }

if [ -z "$source_dir" ]; then
  source_dir="$repo_root/open4d/codecs/$codec"
  if [ ! -f "$source_dir/CMakeLists.txt" ]; then
    die "$source_dir is empty. Initialize it with:
  git -C \"$repo_root\" submodule update --init open4d/codecs/$codec
or pass --source DIR pointing at a checkout of the pinned commit."
  fi
fi
[ -f "$source_dir/CMakeLists.txt" ] || die "no CMakeLists.txt in $source_dir"
source_dir="$(cd "$source_dir" && pwd)"
[ -n "$build_dir" ] || build_dir="$source_dir/build/Release"
if [ "$revert" -eq 0 ]; then
  mkdir -p "$build_dir"
  build_dir="$(cd "$build_dir" && pwd)"
fi

# Files edited for macOS, relative to the source tree.
flag_files=(
  CMakeLists.txt
  dependencies/mmetric/CMakeLists.txt
  dependencies/mmetric/dependencies/dmetric/source/CMakeLists.txt
)
stat_file=dependencies/mmetric/dependencies/dmetric/source/pcc_processing.cpp
spawn_file=source/app/encodeApp.cpp
spawn_marker='open4d macOS: global <spawn.h> before the vmesh headers'
backup_suffix=.open4d-orig

if [ "$revert" -eq 1 ]; then
  restored=0
  for relative in "${flag_files[@]}" "$stat_file" "$spawn_file"; do
    file="$source_dir/$relative"
    if [ -f "$file$backup_suffix" ]; then
      mv -f "$file$backup_suffix" "$file"
      log "restored $relative"
      restored=1
    fi
  done
  [ "$restored" -eq 1 ] || log "nothing to restore in $source_dir"
  exit 0
fi

# --- Prerequisites ---------------------------------------------------------
[ "$(uname -s)" = Darwin ] || log "warning: this script targets macOS; continuing on $(uname -s)"

if ! command -v xcrun >/dev/null 2>&1 || ! xcrun --find clang++ >/dev/null 2>&1; then
  die "no C++ compiler found. Install the Xcode Command Line Tools with:
  xcode-select --install"
fi
command -v git >/dev/null 2>&1 || die "git is missing; it ships with the Xcode Command Line Tools (xcode-select --install)"
command -v cmake >/dev/null 2>&1 || die "cmake is missing. Install CMake 3.17 or newer, for example
  brew install cmake ninja
or, without Homebrew, into a throwaway virtual environment:
  python3 -m venv /tmp/cmake-venv && /tmp/cmake-venv/bin/pip install cmake ninja
  export PATH=/tmp/cmake-venv/bin:\$PATH"
cmake_version="$(cmake --version | awk 'NR == 1 { print $3 }')"
cmake_major="${cmake_version%%.*}"
cmake_minor="${cmake_version#*.}"
cmake_minor="${cmake_minor%%.*}"
if [ "$cmake_major" -lt 3 ] || { [ "$cmake_major" -eq 3 ] && [ "$cmake_minor" -lt 17 ]; }; then
  die "CMake $cmake_version is too old; V-DMC needs 3.17 or newer"
fi

generator=()
if command -v ninja >/dev/null 2>&1; then
  generator=(-G Ninja)
fi
# CMake refuses to switch generators in an existing build directory.
if [ -f "$build_dir/CMakeCache.txt" ]; then
  generator=()
fi

if [ -z "$jobs" ]; then
  jobs="$(sysctl -n hw.logicalcpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
fi

# The first configure clones the dependencies. Check the hosts only if some
# dependency is still missing.
needs_network=0
for dependency in directx-headers directx-math directx-mesh uvatlas tinyply mmetric hm; do
  [ -e "$source_dir/dependencies/$dependency" ] || needs_network=1
done
[ -f "$source_dir/dependencies/hm/README" ] || needs_network=1
if [ "$needs_network" -eq 1 ]; then
  for host in https://github.com https://vcgit.hhi.fraunhofer.de; do
    # Any HTTP answer counts; only a failed connection stops the build.
    if ! curl -sS -o /dev/null --max-time 20 "$host" 2>/dev/null; then
      die "cannot reach $host. The first configure downloads the V-DMC
dependencies (DirectX, UVAtlas, mmetric, dmetric, tinyply from GitHub; HM from
vcgit.hhi.fraunhofer.de). Connect to the network and rerun this script."
    fi
  done
fi

# The dependency CMake code applies patches with "git am", which needs a
# committer identity even though the commits stay local.
git config user.name >/dev/null 2>&1 || export GIT_COMMITTER_NAME="${GIT_COMMITTER_NAME:-open4d-build}"
git config user.email >/dev/null 2>&1 || export GIT_COMMITTER_EMAIL="${GIT_COMMITTER_EMAIL:-open4d-build@localhost}"

# --- Source adjustments ----------------------------------------------------
backup_once() {
  [ -f "$1$backup_suffix" ] || cp -p "$1" "$1$backup_suffix"
}

# Rewrite a file in place without BSD/GNU sed -i differences.
rewrite() {
  local file="$1"
  shift
  local temporary
  temporary="$(mktemp "${TMPDIR:-/tmp}/open4d-vdmc.XXXXXX")"
  sed -E "$@" "$file" >"$temporary"
  if cmp -s "$file" "$temporary"; then
    rm -f "$temporary"
    return 0
  fi
  backup_once "$file"
  cat "$temporary" >"$file"
  rm -f "$temporary"
  log "patched ${file#"$source_dir"/}"
}

strip_x86_flags() {
  local file="$source_dir/$1"
  [ -f "$file" ] || return 0
  # Loop so that adjacent tokens such as "-msse2 -msse3" all go.
  rewrite "$file" \
    -e ':again' \
    -e 's/([[:space:]])-(m64|mfpmath=sse|msse[0-9.]*|mavx[0-9]*)([[:space:]"])/\1\3/' \
    -e 't again'
}

replace_stat64() {
  local file="$source_dir/$stat_file"
  [ -f "$file" ] || return 0
  rewrite "$file" -e 's/(^|[^_[:alnum:]])stat64([^_[:alnum:]]|$)/\1stat\2/g'
}

hoist_spawn_include() {
  local file="$source_dir/$spawn_file"
  [ -f "$file" ] || return 0
  grep -q '<spawn.h>' "$file" || return 0
  grep -qF "$spawn_marker" "$file" && return 0
  backup_once "$file"
  local temporary
  temporary="$(mktemp "${TMPDIR:-/tmp}/open4d-vdmc.XXXXXX")"
  awk -v marker="$spawn_marker" '
    !done && /^#include/ {
      print "// " marker
      print "#if defined(__APPLE__)"
      print "#  include <spawn.h>"
      print "#endif"
      done = 1
    }
    { print }
  ' "$file" >"$temporary"
  cat "$temporary" >"$file"
  rm -f "$temporary"
  log "patched $spawn_file"
}

apply_adjustments() {
  local relative
  for relative in "${flag_files[@]}"; do
    strip_x86_flags "$relative"
  done
  replace_stat64
  hoist_spawn_include
}

# --- Configure and build ---------------------------------------------------
# The encode target does not declare these include directories itself; with
# BUILD_WRAPPER_APPS=OFF nothing else adds them, so pass them globally.
extra_includes="-I$source_dir/dependencies/mmetric/dependencies"
extra_includes="$extra_includes -I$source_dir/source/wrapper/colourConverter"
extra_includes="$extra_includes -I$source_dir/source/wrapper/videoEncoder"

configure() {
  cmake -S "$source_dir" -B "$build_dir" ${generator[@]+"${generator[@]}"} \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBITSTREAM_TRACE=OFF \
    -DBUILD_WRAPPER_APPS=OFF \
    "-DCMAKE_CXX_FLAGS=$extra_includes" \
    "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$build_dir/bin"
}

log "codec:  $codec"
log "source: $source_dir"
log "build:  $build_dir"
log "cmake:  $cmake_version, $jobs jobs"

started="$(date +%s)"
apply_adjustments
# The first configure fetches the dependencies; the self-hosted HM server
# occasionally answers 502, so retry a fetching configure a few times.
attempt=1
until configure; do
  [ "$needs_network" -eq 1 ] && [ "$attempt" -lt 3 ] || die "CMake configure failed (see the output above)"
  attempt=$((attempt + 1))
  log "configure failed; retrying ($attempt/3) in 10 s"
  sleep 10
done
# Edit the freshly fetched dependencies, then let CMake pick up the change.
apply_adjustments
configure >/dev/null
cmake --build "$build_dir" --config Release --parallel "$jobs" --target encode decode
elapsed=$(($(date +%s) - started))

encoder="$build_dir/bin/encode"
decoder="$build_dir/bin/decode"
[ -x "$encoder" ] || die "build finished but $encoder is missing"
[ -x "$decoder" ] || die "build finished but $decoder is missing"

variable="OPEN4D_$(printf '%s' "$codec" | tr '[:lower:]' '[:upper:]')"
log "built $codec in ${elapsed}s"
printf '\nencoder: %s\ndecoder: %s\n\n' "$encoder" "$decoder"
printf 'export %s_ENCODER=%q\nexport %s_DECODER=%q\n' \
  "$variable" "$encoder" "$variable" "$decoder"
