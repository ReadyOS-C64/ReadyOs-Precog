#!/usr/bin/env bash
set -euo pipefail

SRC_D71="${1:-}"
DST_D71="${2:-}"
REL_FILES=(
  "cal26.rel"
  "cal26cfg.rel"
  "dizzy.rel"
  "dizzycfg.rel"
)

find_default_src() {
  local cand
  for cand in /dev/*.d71; do
    if [ -f "$cand" ]; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  return 1
}

if [ -z "$SRC_D71" ]; then
  if ! SRC_D71="$(find_default_src)"; then
    echo "error: no source d71 provided and no /dev/*.d71 found" >&2
    echo "usage: $0 <source.d71> [dest.d71]" >&2
    exit 2
  fi
fi

if [ -z "$DST_D71" ]; then
  DST_D71="$(python3 "$(dirname "$0")/readyos_profiles.py" latest-disk --profile precog-dual-d71 --drive 8)"
fi

if [ ! -f "$SRC_D71" ]; then
  echo "error: source disk not found: $SRC_D71" >&2
  exit 2
fi

if [ ! -f "$DST_D71" ]; then
  echo "error: destination disk not found: $DST_D71" >&2
  exit 2
fi

TMP_DIR="$(mktemp -d /tmp/readyos_rel_recover.XXXXXX)"
cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

recover_one_rel() {
  local rel_name="$1"
  local host_bin="$TMP_DIR/${rel_name}.bin"
  local read_out
  local rec_len

  read_out="$(c1541 "$SRC_D71" -read "${rel_name},l" "$host_bin" 2>&1 || true)"
  if [ ! -s "$host_bin" ]; then
    echo "error: could not read ${rel_name} as REL from $SRC_D71" >&2
    return 1
  fi

  rec_len="$(printf '%s\n' "$read_out" | sed -n 's/.*record length \([0-9][0-9]*\).*/\1/p' | head -n 1)"
  if [ -z "$rec_len" ]; then
    echo "error: could not determine REL record length for ${rel_name}" >&2
    return 1
  fi

  c1541 "$DST_D71" -delete "$rel_name" >/dev/null 2>/dev/null || true
  c1541 "$DST_D71" -write "$host_bin" "${rel_name},l,${rec_len}" >/dev/null
  echo "restored ${rel_name} (record length ${rec_len})"
}

for rel_name in "${REL_FILES[@]}"; do
  recover_one_rel "$rel_name"
done

echo "REL recovery complete:"
echo "  source: $SRC_D71"
echo "  dest:   $DST_D71"
