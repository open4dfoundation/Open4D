#!/usr/bin/env bash
# Bring up the whole browser demo: five systems, one command.
#
#   PYTHON_BIN=<env-python> scripts/run_web_demo.sh
#   PYTHON_BIN=... scripts/run_web_demo.sh --objects dancer,thomas
#
# Starts the Node server on the H.264 corpus (so browsers can decode textures)
# plus a supervised ViVo and NAVA, then prints the URLs. Ctrl-C stops all of
# them.
#
# Not started here, because it needs root and shapes the whole host:
#   sudo scripts/shape_web_demo.sh cascade-20
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY="${PYTHON_BIN:-python}"
PORT="${PORT:-3000}"
OBJECTS="dancer,thomas"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --objects) OBJECTS="$2"; shift 2 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done

# H.264 rather than HEVC: HEVC is Firefox-no and Chrome-only-with-hardware,
# and H.264 measures at 1.003x the bitrate at matched quality on this corpus,
# so serving it costs essentially nothing and every browser can decode it.
CORPUS="${VS4D_COMPRESSED_ROOT:-/media/frozzzen/LocalDisk/ORBIT_datasets_compressed_h264}"
# A files root PER CORPUS. Representation ids carry no codec, so a cache filled
# by one corpus would serve its encodes under another's manifest; the server
# refuses to start if this is violated.
FILES="${VS4D_FILES_ROOT:-$REPO/files-h264}"
VEGA="${VS4D_VEGA_WEB_ROOT:-$REPO/results/vega-web-all}"
TILES="${VS4D_VIVO_TILES_ROOT:-/media/frozzzen/DataDrive/ORBIT_vivo_tiles}"
# The server's own default is 10 segments -- 20 seconds, which is a trial
# length, not a demo length: the page reaches "run complete" before you have
# finished looking at it. 300 segments is ten minutes, and the shaping trace
# repeats every three, so a viewer sees the ladder move several times.
SEGMENTS="${VS4D_TOTAL_SEGMENTS:-300}"

for path in "$CORPUS/megamanifest.json" "$CORPUS/models/quality_model.joblib"; do
  [[ -f "$path" ]] || { echo "missing $path" >&2; exit 1; }
done

PIDS=()
cleanup() {
  trap - INT TERM EXIT
  echo
  echo "stopping…"
  for pid in "${PIDS[@]:-}"; do [[ -n "$pid" ]] && kill "$pid" 2>/dev/null; done
  wait 2>/dev/null
  echo "stopped"
}
trap cleanup INT TERM EXIT

echo "corpus   $CORPUS"
echo "files    $FILES"
echo "segments $SEGMENTS ($((SEGMENTS * 2))s per run)"
echo

( cd "$REPO/system/Server" && VS4D_COMPRESSED_ROOT="$CORPUS" \
    VS4D_FILES_ROOT="$FILES" VS4D_VEGA_WEB_ROOT="$VEGA" \
    VS4D_VIVO_TILES_ROOT="$TILES" VS4D_TOTAL_SEGMENTS="$SEGMENTS" \
    PYTHON_BIN="$PY" PORT="$PORT" \
    node server.js ) &
PIDS+=($!)
sleep 8

if [[ -f "$TILES/catalog.json" ]]; then
  for baseline in vivo nava; do
    PYTHON_BIN="$PY" "$REPO/scripts/serve_pointcloud_baseline.sh" \
      "$baseline" "$OBJECTS" &
    PIDS+=($!)
  done
  sleep 10
else
  echo "no tile corpus at $TILES — ViVo and NAVA will show as unavailable"
fi

HOST="$(hostname -I | awk '{print $1}')"
cat <<EOF

  Demo is up. Start here:

    http://$HOST:$PORT/web/compare.html

  Direct links:
    ours    http://$HOST:$PORT/web/
    vivo    http://$HOST:$PORT/web/baseline.html?bridge=ws://$HOST:8790
    nava    http://$HOST:$PORT/web/baseline.html?bridge=ws://$HOST:8791
    vega    http://$HOST:$PORT/web/vega.html
    nevo    http://$HOST:$PORT/web/nevo.html

  To make adaptation visible, in another shell:
    sudo scripts/shape_web_demo.sh cascade-20

  Ctrl-C stops everything.

EOF
wait
