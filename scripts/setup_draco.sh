# scripts/setup_draco.sh
#!/usr/bin/env bash
set -euo pipefail
cd modules/tsmc
git clone --depth 1 https://github.com/google/draco.git draco
cmake -S draco -B draco/build -DCMAKE_BUILD_TYPE=Release
cmake --build draco/build -j
