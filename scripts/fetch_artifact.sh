#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 URL SHA256 DESTINATION" >&2
  exit 2
}

[[ $# -eq 3 ]] || usage

url="$1"
expected="$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]')"
destination="$3"

[[ "$expected" =~ ^[0-9a-f]{64}$ ]] || {
  echo "SHA256 must be exactly 64 hexadecimal characters" >&2
  exit 2
}

mkdir -p "$(dirname "$destination")"
temporary="$(mktemp "${destination}.tmp.XXXXXX")"
trap 'rm -f "$temporary"' EXIT

curl --fail --location --retry 3 --output "$temporary" "$url"

if command -v sha256sum >/dev/null 2>&1; then
  actual="$(sha256sum "$temporary" | awk '{print $1}')"
elif command -v shasum >/dev/null 2>&1; then
  actual="$(shasum -a 256 "$temporary" | awk '{print $1}')"
else
  echo "Neither sha256sum nor shasum is available" >&2
  exit 1
fi

if [[ "$actual" != "$expected" ]]; then
  echo "Checksum mismatch for $url" >&2
  echo "expected: $expected" >&2
  echo "actual:   $actual" >&2
  exit 1
fi

mv "$temporary" "$destination"
trap - EXIT
echo "Fetched $destination"
