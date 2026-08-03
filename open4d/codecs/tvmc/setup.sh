#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_ONLY=0
SKIP_DRACO=0

for arg in "$@"; do
  case "$arg" in
    --build-only) BUILD_ONLY=1 ;;
    --skip-draco) SKIP_DRACO=1 ;;
    -h|--help)
      echo "Usage: ./setup.sh [--build-only] [--skip-draco]"
      exit 0
      ;;
    *) echo "error: unknown option: $arg" >&2; exit 2 ;;
  esac
done

command -v dotnet >/dev/null 2>&1 || {
  echo "error: .NET 10 SDK is required (https://dotnet.microsoft.com/download/dotnet/10.0)" >&2
  exit 1
}
dotnet --list-sdks | grep -Eq '^10\.' || {
  echo "error: .NET 10 SDK is required; installed SDKs are:" >&2
  dotnet --list-sdks >&2
  exit 1
}

if [[ "$SKIP_DRACO" -eq 0 ]]; then
  command -v git >/dev/null 2>&1 || {
    echo "error: Git is required to install Draco" >&2
    exit 1
  }
  command -v cmake >/dev/null 2>&1 || {
    echo "error: CMake is required to build Draco." >&2
    if command -v brew >/dev/null 2>&1; then
      echo "Install it with: brew install cmake" >&2
    fi
    exit 1
  }
fi

if [[ "$BUILD_ONLY" -eq 0 ]]; then
  PYTHON_BIN="${PYTHON:-}"
  if [[ -z "$PYTHON_BIN" ]]; then
    for candidate in python3.10 python3.11 python3.9 python3.8 python3 python; do
      if command -v "$candidate" >/dev/null 2>&1; then
        PYTHON_BIN="$candidate"
        break
      fi
    done
  fi
  [[ -n "$PYTHON_BIN" ]] || { echo "error: Python 3.8-3.11 is required" >&2; exit 1; }
  "$PYTHON_BIN" -c 'import sys; raise SystemExit(not ((3, 8) <= sys.version_info[:2] < (3, 12)))' || {
    echo "error: Open3D 0.18 requires Python 3.8-3.11; found $($PYTHON_BIN --version)" >&2
    exit 1
  }
  if [[ ! -x "$ROOT/.venv/bin/python" && ! -x "$ROOT/.venv/Scripts/python.exe" ]]; then
    "$PYTHON_BIN" -m venv "$ROOT/.venv"
  fi
  if [[ -x "$ROOT/.venv/Scripts/python.exe" ]]; then
    VENV_PYTHON="$ROOT/.venv/Scripts/python.exe"
  else
    VENV_PYTHON="$ROOT/.venv/bin/python"
  fi
  "$VENV_PYTHON" -m pip install --upgrade pip
  "$VENV_PYTHON" -m pip install -r "$ROOT/requirements.txt"
fi

dotnet build "$ROOT/arap-volume-tracking/Client/Client.csproj" -c Release -p:NuGetAudit=false
dotnet build "$ROOT/tvm-editing/TVMEditor.Test/TVMEditor.Test.csproj" -c Release \
  --output "$ROOT/tvm-editing/TVMEditor.Test/bin/Release/net5.0" -p:NuGetAudit=false

DRACO_ENCODER="$ROOT/draco/build/draco_encoder"
if [[ "${OS:-}" == "Windows_NT" ]]; then
  DRACO_ENCODER="$ROOT/draco/build/Release/draco_encoder.exe"
fi

if [[ "$SKIP_DRACO" -eq 0 && ! -x "$DRACO_ENCODER" ]]; then
  if [[ ! -e "$ROOT/draco/.git" ]]; then
    if git -C "$ROOT" submodule status draco >/dev/null 2>&1; then
      git -C "$ROOT" submodule update --init --recursive draco
    else
      git clone --depth 1 https://github.com/google/draco.git "$ROOT/draco"
    fi
  fi
  cmake -S "$ROOT/draco" -B "$ROOT/draco/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/draco/build" --config Release --parallel
fi

echo "TVMC setup complete. Try: ./run_pipeline.sh --dry-run basketball"
