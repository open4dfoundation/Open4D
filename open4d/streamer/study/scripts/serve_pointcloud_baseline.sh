#!/usr/bin/env bash
# Keep a point-cloud baseline reachable from the browser page.
#
# The baseline servers accept ONE connection, serve it from frame zero, and
# exit. That is right for a measured trial and must not change: a trial is one
# client, one run, one deterministic start. But it means every browser reload
# needs a fresh server, so this supervises one: it restarts the baseline after
# each session and runs the bridge once alongside it, since the bridge itself
# survives client disconnects.
#
#   scripts/serve_pointcloud_baseline.sh vivo dancer,thomas   # ws://host:8790
#   scripts/serve_pointcloud_baseline.sh nava dancer,thomas    # ws://host:8791
#
# Run both at once: they take different default ports, so the chooser can
# offer either without restarting anything. Ctrl-C stops one pair.
set -uo pipefail

BASELINE="${1:-vivo}"
OBJECTS="${2:-dancer,thomas}"
# Per-baseline default ports so ViVo and NAVA can run side by side and the
# web page can switch between them without anyone restarting anything. The
# server reports these in /api/systems, so changing them here alone will make
# the chooser point at the wrong port -- change VS4D_POINTCLOUD_PORTS too.
case "$1" in
  nava) DEFAULT_PORT=12346; DEFAULT_BRIDGE=8791 ;;
  *)    DEFAULT_PORT=12345; DEFAULT_BRIDGE=8790 ;;
esac
PORT="${VS4D_BASELINE_PORT:-$DEFAULT_PORT}"
BRIDGE_PORT="${VS4D_BRIDGE_PORT:-$DEFAULT_BRIDGE}"
TILES="${VS4D_VIVO_TILES_ROOT:-/media/frozzzen/DataDrive/ORBIT_vivo_tiles}"
OUT="${VS4D_BASELINE_OUTPUT:-/tmp/vs4d-baseline-$BASELINE}"
PY="${PYTHON_BIN:-python}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

case "$BASELINE" in
  vivo) MODULE=baselines.ViVo.orbitvivo.server ;;
  nava) MODULE=baselines.NAVA.orbitnava.server ;;
  *) echo "usage: $0 {vivo|nava} [obj1,obj2,...]" >&2
     echo "MetaStream/DeltaStream/LiVo need the RGB-D corpus and cannot be" >&2
     echo "served from the prepared tiles." >&2
     exit 2 ;;
esac

if [[ ! -f "$TILES/catalog.json" ]]; then
  echo "no tile catalogue at $TILES/catalog.json" >&2
  exit 1
fi
mkdir -p "$OUT"
cd "$REPO"

cleanup() {
  trap - INT TERM EXIT
  [[ -n "${BRIDGE_PID:-}" ]] && kill "$BRIDGE_PID" 2>/dev/null
  [[ -n "${SERVER_PID:-}" ]] && kill "$SERVER_PID" 2>/dev/null
  echo "stopped"
}
trap cleanup INT TERM EXIT

node system/WebClient/bridge/v4ds-bridge.js \
  --baseline-port "$PORT" --listen-port "$BRIDGE_PORT" &
BRIDGE_PID=$!
echo "bridge pid $BRIDGE_PID on ws://0.0.0.0:$BRIDGE_PORT"

# shellcheck disable=SC2086
while kill -0 "$BRIDGE_PID" 2>/dev/null; do
  "$PY" -m "$MODULE" \
    --prepared-dir "$TILES" --output-dir "$OUT" --tile-catalog-ladder \
    --objects ${OBJECTS//,/ } --port "$PORT" &
  SERVER_PID=$!
  wait "$SERVER_PID"
  SERVER_PID=""
  # A crash loop would otherwise spin as fast as the process can fail.
  sleep 1
  echo "--- $BASELINE session ended; ready for the next page load ---"
done
