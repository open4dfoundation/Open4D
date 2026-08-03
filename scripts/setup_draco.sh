# scripts/setup_draco.sh
#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"

git -C "$repo_root" submodule update --init \
  open4d/codecs/tsmc/draco \
  open4d/codecs/tvmc/draco

for codec in tsmc tvmc; do
  draco="$repo_root/open4d/codecs/$codec/draco"
  cmake -S "$draco" -B "$draco/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$draco/build" -j
done
