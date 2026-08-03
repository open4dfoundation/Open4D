#!/usr/bin/env bash
set -Eeuo pipefail

cd /workspace
./setup.sh --build-only --skip-draco
exec python3 pipeline.py "$@"
