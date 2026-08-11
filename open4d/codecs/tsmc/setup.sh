#!/usr/bin/env bash
#
# Build TSMC's .NET components and the vendored Draco binaries.
#
# Unlike TVMC's setup.sh this creates no virtualenv: TSMC runs in the
# repository-wide conda environment (see ../../../environment.yml), which is what
# tsmc/environment.yml now points at.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKIP_DRACO=0

for arg in "$@"; do
  case "$arg" in
    --skip-draco) SKIP_DRACO=1 ;;
    -h|--help)
      echo "Usage: ./setup.sh [--skip-draco]"
      exit 0
      ;;
    *) echo "error: unknown option: $arg" >&2; exit 2 ;;
  esac
done

# A distribution's own dotnet under /usr/lib/dotnet shadows a newer SDK in
# ~/.dotnet, and the resulting NETSDK1045 reads as a missing SDK when the SDK is
# merely second on PATH. Prefer ~/.dotnet when it holds a 10.x SDK.
if [[ -d "$HOME/.dotnet" ]]; then
  export DOTNET_ROOT="$HOME/.dotnet"
  export PATH="$HOME/.dotnet:$PATH"
fi

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
    echo "error: Git is required to fetch Draco" >&2
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

dotnet build "$ROOT/arap-volume-tracking/Client/Client.csproj" -c Release -p:NuGetAudit=false
dotnet build "$ROOT/tvm-editing/TVMEditor.Test/TVMEditor.Test.csproj" -c Release -p:NuGetAudit=false

# run.sh locates the editor through this path, so fail loudly here rather than
# letting the pipeline report a missing file eight steps in.
EDITOR="$ROOT/tvm-editing/TVMEditor.Test/bin/Release/net10.0/TVMEditor.Test"
if [[ ! -x "$EDITOR" && ! -x "$EDITOR.exe" ]]; then
  echo "error: expected the editor at $EDITOR after a successful build." >&2
  echo "If TVMEditor.Test.csproj no longer targets net10.0, update TSMC_EDITOR_BUILD in run.sh." >&2
  exit 1
fi

DRACO_ENCODER="$ROOT/draco/build/draco_encoder"
if [[ "${OS:-}" == "Windows_NT" ]]; then
  DRACO_ENCODER="$ROOT/draco/build/Release/draco_encoder.exe"
fi

if [[ "$SKIP_DRACO" -eq 0 && ! -x "$DRACO_ENCODER" ]]; then
  if [[ ! -e "$ROOT/draco/.git" ]]; then
    git -C "$ROOT/../../.." submodule update --init --recursive open4d/codecs/tsmc/draco
  fi
  cmake -S "$ROOT/draco" -B "$ROOT/draco/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/draco/build" --config Release --parallel
fi

echo "TSMC setup complete. Try: ./run.sh --help"
