#!/usr/bin/env bash
# Initialize and build every vendored Google Draco submodule in the repository.
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"

# Three copies, all pinned to the same commit: the Draco baseline codec's own,
# plus one each for TSMC and TVMC. Each codec's scripts look for the binaries
# under its own checkout, so each one is built in place.
submodules=(
  open4d/codecs/draco/draco
  open4d/codecs/tsmc/draco
  open4d/codecs/tvmc/draco
)

git -C "$repo_root" submodule update --init "${submodules[@]}"

for submodule in "${submodules[@]}"; do
  draco="$repo_root/$submodule"
  cmake -S "$draco" -B "$draco/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$draco/build" -j
  echo "Built draco_encoder/draco_decoder in $submodule/build"
done
