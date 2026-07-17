#!/usr/bin/env bash
# Initialize and build the vendored Google Draco source for this module.
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
draco="$repo_root/open4d/modules/Draco/draco"

git -C "$repo_root" submodule update --init open4d/modules/Draco/draco

cmake -S "$draco" -B "$draco/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$draco/build" -j

echo "Built draco_encoder/draco_decoder in $draco/build"
