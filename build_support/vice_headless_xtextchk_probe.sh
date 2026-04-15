#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

configure_vice_env() {
  if [ -z "${GSETTINGS_SCHEMA_DIR:-}" ] && [ -f "/opt/homebrew/share/glib-2.0/schemas/gschemas.compiled" ]; then
    export GSETTINGS_SCHEMA_DIR="/opt/homebrew/share/glib-2.0/schemas"
  fi

  if [ -d "/opt/homebrew/share" ]; then
    if [ -n "${XDG_DATA_DIRS:-}" ]; then
      case ":$XDG_DATA_DIRS:" in
        *":/opt/homebrew/share:"*) ;;
        *) export XDG_DATA_DIRS="/opt/homebrew/share:$XDG_DATA_DIRS" ;;
      esac
    else
      export XDG_DATA_DIRS="/opt/homebrew/share"
    fi
  fi
}

configure_vice_env

LOG_DIR="$ROOT_DIR/logs"
HARNESS_DIR_REL="artifacts/dev_harness/xtextchk"
HARNESS_DIR="$ROOT_DIR/$HARNESS_DIR_REL"
HARNESS_BOOT_REL="$HARNESS_DIR_REL/xtextchk_boot.prg"
mkdir -p "$LOG_DIR"

CASE_ID=0
LIMIT_CYCLES="${LIMIT_CYCLES:-12000000}"
TRUE_DRIVE="${TRUE_DRIVE:-1}"
VICE_SPEED="${VICE_SPEED:-800}"
WARP="${WARP:-0}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case)
      shift
      CASE_ID="${1:-0}"
      ;;
    --limit-cycles)
      shift
      LIMIT_CYCLES="${1:-12000000}"
      ;;
    --true-drive)
      shift
      TRUE_DRIVE="${1:-1}"
      ;;
    --speed)
      shift
      VICE_SPEED="${1:-800}"
      ;;
    --warp)
      shift
      WARP="${1:-0}"
      ;;
  esac
  shift
done

echo "[probe] Building xtextchk artifacts (case=$CASE_ID)..."
make -B \
  "$HARNESS_DIR_REL/xtextchk.prg" \
  "$HARNESS_DIR_REL/xtextchk.d71" \
  XTEXTCHK_CASE="$CASE_ID" >/dev/null

TS="$(date +%Y%m%d_%H%M%S)"
CASE_DISK_8="/tmp/xtextchk_c${CASE_ID}_${TS}_8.d71"
STDOUT_LOG="$LOG_DIR/xtextchk_c${CASE_ID}_${TS}.stdout.log"
STDERR_LOG="$LOG_DIR/xtextchk_c${CASE_ID}_${TS}.stderr.log"
DBG_BIN="$LOG_DIR/xtextchk_c${CASE_ID}_${TS}.dbg.bin"
DBG_ERR="$LOG_DIR/xtextchk_c${CASE_ID}_${TS}.dbgread.err.log"
LIST8="$LOG_DIR/xtextchk_c${CASE_ID}_${TS}.d8.list.txt"

cp -f "$HARNESS_DIR/xtextchk.d71" "$CASE_DISK_8"

echo "[probe] Running headless xtextchk case=$CASE_ID"
set +e
if [[ "$TRUE_DRIVE" = "1" ]]; then
  if [[ "$WARP" = "1" ]]; then
    script -q /dev/null x64sc \
      -console \
      -warp \
      -sounddev dummy \
      -speed "$VICE_SPEED" \
      -drive8type 1571 \
      -devicebackend8 0 \
      +busdevice8 \
      -drive8truedrive \
      -8 "$CASE_DISK_8" \
      -limitcycles "$LIMIT_CYCLES" \
      -autostartprgmode 1 \
      "$HARNESS_BOOT_REL" \
      >"$STDOUT_LOG" 2>"$STDERR_LOG"
  else
    script -q /dev/null x64sc \
      -console \
      -sounddev dummy \
      -speed "$VICE_SPEED" \
      -drive8type 1571 \
      -devicebackend8 0 \
      +busdevice8 \
      -drive8truedrive \
      -8 "$CASE_DISK_8" \
      -limitcycles "$LIMIT_CYCLES" \
      -autostartprgmode 1 \
      "$HARNESS_BOOT_REL" \
      >"$STDOUT_LOG" 2>"$STDERR_LOG"
  fi
else
  if [[ "$WARP" = "1" ]]; then
    script -q /dev/null x64sc \
      -console \
      -warp \
      -sounddev dummy \
      -speed "$VICE_SPEED" \
      -drive8type 1571 \
      -devicebackend8 0 \
      +busdevice8 \
      -8 "$CASE_DISK_8" \
      -limitcycles "$LIMIT_CYCLES" \
      -autostartprgmode 1 \
      "$HARNESS_BOOT_REL" \
      >"$STDOUT_LOG" 2>"$STDERR_LOG"
  else
    script -q /dev/null x64sc \
      -console \
      -sounddev dummy \
      -speed "$VICE_SPEED" \
      -drive8type 1571 \
      -devicebackend8 0 \
      +busdevice8 \
      -8 "$CASE_DISK_8" \
      -limitcycles "$LIMIT_CYCLES" \
      -autostartprgmode 1 \
      "$HARNESS_BOOT_REL" \
      >"$STDOUT_LOG" 2>"$STDERR_LOG"
  fi
fi
VICE_RC=$?
set -e

echo "[probe] VICE rc: $VICE_RC"
echo "[probe] stdout: $STDOUT_LOG"
echo "[probe] stderr: $STDERR_LOG"

set +e
c1541 "$CASE_DISK_8" -read "xtextstat,s" "$DBG_BIN" >/dev/null 2>"$DBG_ERR"
DBG_RC=$?
set -e

c1541 "$CASE_DISK_8" -list >"$LIST8" 2>/dev/null || true
echo "[probe] disk8 list: $LIST8"

if [[ "$DBG_RC" -ne 0 || ! -f "$DBG_BIN" ]]; then
  echo "error: failed to read xtextstat from case disk" >&2
  echo "--- dbg read stderr ---" >&2
  tail -n 40 "$DBG_ERR" >&2 || true
  echo "--- stdout tail ---" >&2
  tail -n 60 "$STDOUT_LOG" >&2 || true
  echo "--- stderr tail ---" >&2
  tail -n 60 "$STDERR_LOG" >&2 || true
  exit 2
fi

python3 build_support/parse_xtextchk_stat.py --dbg-bin "$DBG_BIN"
