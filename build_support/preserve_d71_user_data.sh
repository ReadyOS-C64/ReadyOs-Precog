#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 3 ]; then
  echo "Usage: $0 backup <disk.d71> <out_dir> | restore <disk.d71> <manifest.tsv>" >&2
  exit 1
fi

MODE="$1"
DISK="$2"
TARGET="$3"

MANAGED_PRGS=(
  "preboot"
  "prebootraw"
  "setd71"
  "showcfg"
  "boot"
  "launcher"
  "editor"
  "calcplus"
  "hexview"
  "clipmgr"
  "reuviewer"
  "tasklist"
  "simplefiles"
  "simplecells"
  "game2048"
  "deminer"
  "cal26"
  "dizzy"
  "readme"
  "readyshell"
  "rsovl1"
  "rsovl2"
  "rsovl3"
  "ovl1"
  "ovl2"
  "ovl3"
  "test reu"
)

MANAGED_SEQS=(
  "apps.cfg"
  "editor help"
  "example tasks"
  "c"
  "b"
  "test"
)

is_managed_prg() {
  local name_lc="$1"
  local n
  for n in "${MANAGED_PRGS[@]}"; do
    if [ "$name_lc" = "$n" ]; then
      return 0
    fi
  done
  return 1
}

is_managed_seq() {
  local name_lc="$1"
  local n
  for n in "${MANAGED_SEQS[@]}"; do
    if [ "$name_lc" = "$n" ]; then
      return 0
    fi
  done
  return 1
}

backup_mode() {
  local out_dir="$1"
  local listing="$out_dir/listing.txt"
  local manifest="$out_dir/manifest.tsv"
  local idx=0
  local line
  local name
  local type
  local name_lc
  local host_file
  local rel_out
  local rec_len
  local read_spec

  mkdir -p "$out_dir"
  : > "$manifest"

  c1541 "$DISK" -list >"$listing" 2>/dev/null || true

  while IFS= read -r line; do
    if [[ "$line" != *\"*\"* ]]; then
      continue
    fi

    name="$(printf "%s\n" "$line" | sed -n 's/.*"\(.*\)".*/\1/p')"
    [ -n "$name" ] || continue

    type="$(printf "%s\n" "$line" | awk '{print tolower($NF)}')"
    case "$type" in
      prg|seq|rel|usr) ;;
      *) continue ;;
    esac

    name_lc="$(printf "%s" "$name" | tr '[:upper:]' '[:lower:]')"
    if [ "$type" = "prg" ] && is_managed_prg "$name_lc"; then
      continue
    fi
    if [ "$type" = "seq" ] && is_managed_seq "$name_lc"; then
      continue
    fi

    idx=$((idx + 1))
    host_file="$out_dir/file_${idx}.bin"

    if [ "$type" = "rel" ]; then
      rel_out="$(c1541 "$DISK" -read "${name},l" "$host_file" 2>&1 || true)"
      if [ ! -s "$host_file" ]; then
        rm -f "$host_file"
        continue
      fi
      rec_len="$(printf "%s\n" "$rel_out" | sed -n 's/.*record length \([0-9][0-9]*\).*/\1/p' | head -n 1)"
      if [ -z "$rec_len" ]; then
        rm -f "$host_file"
        continue
      fi
      printf '%s\t%s\t%s\t%s\n' "$name" "$type" "$rec_len" "$host_file" >> "$manifest"
      continue
    fi

    read_spec="$name"
    case "$type" in
      seq) read_spec="${name},s" ;;
      usr) read_spec="${name},u" ;;
      prg) read_spec="${name},p" ;;
    esac

    if ! c1541 "$DISK" -read "$read_spec" "$host_file" >/dev/null 2>/dev/null; then
      rm -f "$host_file"
      continue
    fi
    printf '%s\t%s\t0\t%s\n' "$name" "$type" "$host_file" >> "$manifest"
  done < "$listing"
}

restore_mode() {
  local manifest="$1"
  local name
  local type
  local rec_len
  local host_file
  local restore_spec

  [ -s "$manifest" ] || exit 0

  while IFS=$'\t' read -r name type rec_len host_file; do
    [ -f "$host_file" ] || continue

    c1541 "$DISK" -delete "$name" >/dev/null 2>/dev/null || true

    case "$type" in
      rel)
        restore_spec="${name},l,${rec_len}"
        c1541 "$DISK" -write "$host_file" "$restore_spec" >/dev/null 2>/dev/null || true
        ;;
      seq)
        restore_spec="${name},s"
        c1541 "$DISK" -write "$host_file" "$restore_spec" >/dev/null 2>/dev/null || true
        ;;
      usr)
        restore_spec="${name},u"
        c1541 "$DISK" -write "$host_file" "$restore_spec" >/dev/null 2>/dev/null || true
        ;;
      *)
        c1541 "$DISK" -write "$host_file" "$name" >/dev/null 2>/dev/null || true
        ;;
    esac
  done < "$manifest"
}

case "$MODE" in
  backup)
    backup_mode "$TARGET"
    ;;
  restore)
    restore_mode "$TARGET"
    ;;
  *)
    echo "Unknown mode: $MODE" >&2
    exit 1
    ;;
esac
