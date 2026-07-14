#!/usr/bin/env bash
#
# setup.sh - build the native dependencies of the TSMC pipeline.
#
#   ./setup.sh dotnet-sdk   # install .NET SDK 7.0 and 5.0 into $DOTNET_ROOT
#   ./setup.sh draco        # clone + build Google Draco (encoder/decoder)
#   ./setup.sh dotnet       # build the ARAP (net7.0) and TVMEditor (net5.0) projects
#   ./setup.sh all          # draco + dotnet  (assumes the SDKs are already present)
#   ./setup.sh everything   # dotnet-sdk + draco + dotnet
#
# The Python environment is handled separately (see environment.yml / README).
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

export DOTNET_ROOT="${DOTNET_ROOT:-$HOME/.dotnet}"
export PATH="$DOTNET_ROOT:$PATH"

log() { printf '\n\033[1;34m==== %s ====\033[0m\n' "$*"; }

install_dotnet_sdk() {
    log "Installing .NET SDK 7.0 (ARAP) and 5.0 (TVMEditor) into $DOTNET_ROOT"
    local script; script="$(mktemp)"
    curl -fsSL https://dot.net/v1/dotnet-install.sh -o "$script" \
        || wget -qO "$script" https://dot.net/v1/dotnet-install.sh
    chmod +x "$script"
    "$script" --channel 7.0 --install-dir "$DOTNET_ROOT"
    "$script" --channel 7.0 --runtime aspnetcore --install-dir "$DOTNET_ROOT"
    "$script" --channel 5.0 --install-dir "$DOTNET_ROOT"
    "$script" --channel 5.0 --runtime aspnetcore --install-dir "$DOTNET_ROOT"
    rm -f "$script"
    "$DOTNET_ROOT/dotnet" --list-sdks
}

build_draco() {
    log "Building Google Draco"
    if [ ! -d draco/.git ] && [ ! -f draco/CMakeLists.txt ]; then
        rm -rf draco
        git clone --depth 1 https://github.com/google/draco.git draco
    fi
    cmake -S draco -B draco/build -DCMAKE_BUILD_TYPE=Release
    cmake --build draco/build -j "$(nproc 2>/dev/null || echo 4)"
    ls -l draco/build/draco_encoder draco/build/draco_decoder
}

build_dotnet() {
    log "Building ARAP volume tracking (net7.0) -> arap-volume-tracking/bin"
    ( cd arap-volume-tracking && dotnet build -c Release )
    log "Building TVMEditor (net5.0) -> tvm-editing/.../bin/Release/net5.0"
    ( cd tvm-editing && dotnet build TVMEditor.sln --configuration Release --no-incremental )
}

case "${1:-all}" in
    dotnet-sdk)  install_dotnet_sdk ;;
    draco)       build_draco ;;
    dotnet)      build_dotnet ;;
    all)         build_draco; build_dotnet ;;
    everything)  install_dotnet_sdk; build_draco; build_dotnet ;;
    *) echo "usage: $0 {dotnet-sdk|draco|dotnet|all|everything}"; exit 1 ;;
esac

log "setup.sh ($1) done"
