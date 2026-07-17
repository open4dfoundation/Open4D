# scripts/setup_draco.sh
#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"

git -C "$repo_root" submodule update --init \
  open4d/modules/tsmc/draco \
  open4d/modules/tvmc/draco

for module in tsmc tvmc; do
  draco="$repo_root/open4d/modules/$module/draco"
  cmake -S "$draco" -B "$draco/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$draco/build" -j
done
