#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "${1:-}" == "--docker" ]]; then
  shift
  command -v docker >/dev/null 2>&1 || {
    echo "error: Docker is not installed or is not on PATH." >&2
    echo >&2
    echo "Choose one:" >&2
    echo "  Docker: install and start Docker Desktop, then rerun this command." >&2
    echo "          https://docs.docker.com/desktop/setup/install/mac-install/" >&2
    echo "  Local:  ./setup.sh && ./run_pipeline.sh ${*:-basketball}" >&2
    exit 1
  }
  PLATFORM="${TVMC_DOCKER_PLATFORM:-linux/amd64}"
  DOCKER_TTY=()
  if [[ -t 0 && -t 1 ]]; then
    DOCKER_TTY=(-it)
  fi
  docker build --platform "$PLATFORM" -t open4d-tvmc "$ROOT"
  exec docker run --rm "${DOCKER_TTY[@]}" \
    --platform "$PLATFORM" \
    -v "$ROOT:/workspace" \
    -w /workspace \
    open4d-tvmc "$@"
fi

if [[ -x "$ROOT/.venv/Scripts/python.exe" ]]; then
  PYTHON="$ROOT/.venv/Scripts/python.exe"
elif [[ -x "$ROOT/.venv/bin/python" ]]; then
  PYTHON="$ROOT/.venv/bin/python"
else
  PYTHON="${PYTHON:-python3}"
fi

exec "$PYTHON" "$ROOT/pipeline.py" "$@"
