#!/usr/bin/env bash
# Is this vendored copy still identical to the research repo it came from?
#
# The client code lives in two places: here, and in 4DVideoStreaming where it
# is developed. Nothing enforces that they match — Open4D vendors its methods
# as plain files (see reconstruction/), and that is deliberate, because the two
# repos have unrelated histories and one is private. The cost is silent drift,
# so this makes it a command instead of a discovery.
#
#   VS4D_REPO=/path/to/4DVideoStreaming scripts/check-sync.sh
#
# Exits non-zero if anything differs, so CI can call it.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${VS4D_REPO:-$HOME/4DVideoStreaming}"
RECORDED="$(sed -n 's/^vendored-from: *//p' "$HERE/PROVENANCE" 2>/dev/null)"

if [[ ! -d "$SRC" ]]; then
  echo "no research repo at $SRC; set VS4D_REPO" >&2
  exit 2
fi

echo "vendored from : ${RECORDED:-unrecorded}"
if have=$(git -C "$SRC" rev-parse HEAD 2>/dev/null); then
  echo "that repo now : $have"
  [[ -n "$RECORDED" && "$have" != "$RECORDED" ]] &&
    echo "  (it has moved on; differences below may be intended)"
fi
echo

# Generated output and dependencies are not part of the comparison.
EXCLUDES=(-x node_modules -x dist -x __pycache__ -x .gitignore -x PROVENANCE
          -x README.md -x check-sync.sh)
status=0
compare() {   # <path here> <path there>
  local out
  out=$(diff -r "${EXCLUDES[@]}" "$HERE/$1" "$SRC/$2" 2>&1 \
        | grep -vE "^Only in $HERE" || true)
  if [[ -n "$out" ]]; then
    echo "DIFFERS  $1"
    echo "$out" | sed 's/^/    /' | head -12
    status=1
  else
    echo "same     $1"
  fi
}

compare system/ClientCore        system/ClientCore
compare system/WebClient/src     system/WebClient/src
compare system/WebClient/public  system/WebClient/public
compare system/WebClient/bridge  system/WebClient/bridge
compare system/Server/server.js  system/Server/server.js
compare tile_ladder.py           baselines/ViVo/orbitvivo/tile_ladder.py

echo
[[ $status -eq 0 ]] && echo "in sync" || echo "DRIFTED — reconcile before relying on either copy"
exit $status
