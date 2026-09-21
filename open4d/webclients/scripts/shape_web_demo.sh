#!/usr/bin/env bash
# Replay a bandwidth trace against the browser demo, so adaptation is visible.
#
#   sudo scripts/shape_web_demo.sh                       # cascade-20, eth0
#   sudo scripts/shape_web_demo.sh poor-wifi
#   sudo scripts/shape_web_demo.sh cascade-20 --scale 0.5
#
# WHAT THIS AFFECTS. It installs a root token bucket on the server's egress
# NIC, so it shapes EVERY outbound flow on this machine, not just the demo.
# That is deliberate and inherited from the Quest methodology: a destination
# u32 filter silently misses traffic on a multiqueue NIC, and this NIC is
# multiqueue (`qdisc mq 0: root`). The cost is that other users and other
# services on this host are shaped too for as long as it runs. Do not leave it
# running, and do not run it on a shared box without telling whoever else is
# on it.
#
# Ctrl-C restores the original qdisc. So does the script exiting for any other
# reason; if it is killed with -9, remove the rule by hand with
#   sudo tc qdisc del dev <nic> root
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TRACE_NAME="${1:-cascade-20}"
shift || true
NIC="${VS4D_SHAPED_INTERFACE:-$(ip route show default | grep -oP 'dev \K\S+' | head -1)}"
TRACE="$REPO/system/Client/traces/${TRACE_NAME}.csv"

if [[ ! -f "$TRACE" ]]; then
  echo "no such trace: $TRACE" >&2
  echo "available:" >&2
  ls "$REPO/system/Client/traces/" | sed 's/\.csv$//' | sed 's/^/  /' >&2
  exit 2
fi
if [[ -z "$NIC" ]]; then
  echo "could not determine the egress NIC; set VS4D_SHAPED_INTERFACE" >&2
  exit 2
fi
if [[ "$(id -u)" -ne 0 ]]; then
  echo "tc needs root: re-run as" >&2
  echo "  sudo $0 $TRACE_NAME $*" >&2
  exit 1
fi

echo "trace     $TRACE"
echo "interface $NIC  (shaping ALL egress on this host)"
python3 - "$TRACE" <<'PY'
import csv, sys
rows = list(csv.reader(open(sys.argv[1])))[1:]
vals = [float(b) for _, b in rows if b]
print(f"profile   {len(vals)} points, {min(vals):.1f}-{max(vals):.1f} Mbps, "
      f"{rows[-1][0]}s long")
PY
echo
echo "Watch the 'link:' line on /web/ and /web/baseline.html follow this."
echo "Ctrl-C restores the original qdisc."
echo

exec node "$REPO/system/Server/quest-trace-player.js" \
  --interface "$NIC" --hold "$@" "$TRACE"
