#!/bin/bash
#
# Ready OS Run Script
# Launches VICE with 16MB REU and dual D71 disks
#
# Usage: ./run.sh [flags] [option]
#
# Options:
#   (none)    - Run Ready OS normally (preboot.prg)
#   test      - Run REU test program standalone
#   debug     - Run with VICE monitor enabled (Alt+M to access)
#   warp      - Run in warp mode (max speed, useful for loading)
#   launcher  - Run launcher.prg directly (skip boot)
#   editor    - Run editor.prg directly (no REU switching)
#   calcplus  - Run calcplus.prg directly (no REU switching)
#   hexview   - Run hexview.prg directly (no REU switching)
#   2048      - Run game2048.prg directly (no REU switching)
#   deminer   - Run deminer.prg directly (no REU switching)
#   cal26     - Run cal26.prg directly (no REU switching)
#   dizzy     - Run dizzy.prg directly (no REU switching)
#   readme    - Run readme.prg directly (no REU switching)
#   showcfg   - Run BASIC APPS.CFG inspector (drive 8)
#   xfilechk  - Run standalone IEC file-operation harness on dedicated D71s
#   monitor   - Start with monitor open at boot
#   readyshell-mon - Normal boot + remote monitor endpoints/logging for live readyshell diagnosis
#   noreu     - Run boot without REU (error-path testing)
#   help      - Show this help
#
# Flags:
#   --skipbuild - Skip automatic make/build step
#   --config PATH - Use an alternate readyos config source for this build
#   --load-all 0|1 - Override launcher load_all_to_reu in generated apps.cfg
#   --run-first APP - Override launcher runappfirst in generated apps.cfg
#   --parse-trace-debug 0|1 - Override ReadyShell parse trace profile for build
#   --interactive - Interactive key-menu for common launch combinations
#

set -e
cd "$(dirname "$0")"

resolve_build_support_dir() {
    local candidate=""

    if [ -n "${BUILD_SUPPORT_DIR:-}" ] && [ -d "${BUILD_SUPPORT_DIR}" ]; then
        candidate="${BUILD_SUPPORT_DIR}"
    elif [ -d "build_support" ]; then
        candidate="build_support"
    elif [ -n "${TOOLS_DIR:-}" ] && [ -d "${TOOLS_DIR}" ]; then
        candidate="${TOOLS_DIR}"
    elif [ -d "tools" ]; then
        candidate="tools"
    elif [ -d "../agenticdevharness/tools" ]; then
        candidate="../agenticdevharness/tools"
    else
        echo "Error: ReadyOS build support directory not found."
        echo "Tried: ./build_support, ./tools, and ../agenticdevharness/tools"
        echo "Set BUILD_SUPPORT_DIR to override."
        exit 1
    fi

    BUILD_SUPPORT_DIR="$(cd "$candidate" && pwd)"
}

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

resolve_build_support_dir
configure_vice_env

# Files
DISK_FILE_1="readyos.d71"
DISK_FILE_2="readyos_2.d71"
PREBOOT_PRG="preboot.prg"
TEST_PRG="test_reu.prg"
LAUNCHER_PRG="launcher.prg"
EDITOR_PRG="editor.prg"
CALCPLUS_PRG="calcplus.prg"
HEXVIEW_PRG="hexview.prg"
GAME2048_PRG="game2048.prg"
DEMINER_PRG="deminer.prg"
CAL26_PRG="cal26.prg"
DIZZY_PRG="dizzy.prg"
README_PRG="readme.prg"
SHOWCFG_PRG="showcfg.prg"
XFILECHK_BOOT_PRG="xfilechk_boot.prg"
XFILECHK_PRG="xfilechk.prg"
XFILECHK_DISK_FILE_1="xfilechk.d71"
XFILECHK_DISK_FILE_2="xfilechk_2.d71"

# Dynamic build version (base + rolling suffix A..Z)
VERSION_BASE="0.1.8"
VERSION_STATE_FILE="/tmp/readyos_run_version_suffix.txt"
VERSION_HEADER_FILE="src/generated/build_version.h"
VERSION_ASM_FILE="src/generated/msg_version.inc"
RUN_VERSION_SUFFIX=""
RUN_VERSION_TEXT=""
REMOTE_MON_ADDR="127.0.0.1:6510"
BINARY_MON_ADDR="127.0.0.1:6502"
REMOTE_MON_LOG="logs/vice_remote_monitor.log"
VICE_STDIO_LOG="logs/vice_readyshell_mon.out"
VICE_LOG_FILE="logs/vice.log"
DEFAULT_CONFIG_SOURCE="cfg/readyos_config.ini"
CONFIG_SOURCE="$DEFAULT_CONFIG_SOURCE"
CONFIG_OVERRIDE_LOAD_ALL=""
CONFIG_OVERRIDE_RUN_FIRST=""

# VICE executable (try x64sc first, fall back to x64)
if command -v x64sc &> /dev/null; then
    VICE="x64sc"
elif command -v x64 &> /dev/null; then
    VICE="x64"
else
    echo "Error: VICE emulator not found (tried x64sc, x64)"
    echo "Please install VICE or add it to your PATH"
    exit 1
fi

# Base VICE options for REU + dual 1571 drives
VICE_OPTS=(
    -logfile "$VICE_LOG_FILE"
    -reu
    -reusize 16384
    -drive8type 1571
    -drive8truedrive
    -devicebackend8 0
    +busdevice8
    -drive9type 1571
    -drive9truedrive
    -devicebackend9 0
    +busdevice9
)

# Only non-program user data is preserved across rebuilds. PRGs are always
# rebuilt and reinstalled from current workspace artifacts.
BUILD_MANAGED_SEQS=(
    "apps.cfg"
    "editor help"
)

update_dynamic_version() {
    local last=""
    local next="A"
    local last_code
    local next_code

    mkdir -p src/generated

    if [ -f "$VERSION_STATE_FILE" ]; then
        last="$(tr -dc 'A-Z' < "$VERSION_STATE_FILE" | head -c 1)"
    fi

    if [ -n "$last" ]; then
        if [ "$last" = "Z" ]; then
            next="A"
        else
            last_code=$(printf '%d' "'$last")
            next_code=$((last_code + 1))
            next=$(printf "\\$(printf '%03o' "$next_code")")
        fi
    fi

    RUN_VERSION_SUFFIX="$next"
    RUN_VERSION_TEXT="${VERSION_BASE}${RUN_VERSION_SUFFIX}"

    printf '%s\n' "$RUN_VERSION_SUFFIX" > "$VERSION_STATE_FILE"

    cat > "$VERSION_HEADER_FILE" <<EOF
/* Auto-generated by run.sh. Do not edit by hand. */
#ifndef READYOS_BUILD_VERSION_H
#define READYOS_BUILD_VERSION_H

#define READYOS_VERSION_BASE "${VERSION_BASE}"
#define READYOS_VERSION_SUFFIX "${RUN_VERSION_SUFFIX}"
#define READYOS_VERSION_TEXT "${RUN_VERSION_TEXT}"
#define READYOS_TITLE_TEXT "READY OS v${RUN_VERSION_TEXT}"
#define READYOS_BOOT_VERSION_TEXT "v${RUN_VERSION_TEXT}"

#endif /* READYOS_BUILD_VERSION_H */
EOF

    cat > "$VERSION_ASM_FILE" <<EOF
; Auto-generated by run.sh. Do not edit by hand.

msg_version:
    .byte "v${RUN_VERSION_TEXT}"
msg_version_end:
EOF
}

# Function to show help
show_help() {
    echo "Ready OS Run Script"
    echo ""
    echo "Usage: ./run.sh [flags] [option]"
    echo ""
    echo "Modes (option):"
    echo "  (none)    Run Ready OS normally (preboot.prg)"
    echo "  test      Run REU test program standalone"
    echo "  debug     Run with VICE monitor breakpoints at shim entry points"
    echo "  warp      Run in warp mode (max speed for loading)"
    echo "  launcher  Run launcher.prg directly (skip boot loader)"
    echo "  editor    Run editor.prg directly (standalone, no REU switching)"
    echo "  calcplus  Run calcplus.prg directly (standalone, no REU switching)"
    echo "  hexview   Run hexview.prg directly (standalone, no REU switching)"
    echo "  2048      Run game2048.prg directly (standalone, no REU switching)"
    echo "  deminer   Run deminer.prg directly (standalone, no REU switching)"
    echo "  cal26     Run cal26.prg directly (standalone, no REU switching)"
    echo "  dizzy     Run dizzy.prg directly (standalone, no REU switching)"
    echo "  readme    Run readme.prg directly (standalone, no REU switching)"
    echo "  showcfg   Run BASIC APPS.CFG inspector (drive 8)"
    echo "  xfilechk  Run standalone IEC file-operation harness on dedicated D71s"
    echo "  monitor   Start with VICE monitor open immediately"
    echo "  readyshell-mon  Normal boot + remote monitor sockets/logs for readyshell debugging"
    echo "  noreu     Run boot without REU (for testing error handling)"
    echo "  help      Show this help"
    echo ""
    echo "Flags:"
    echo "  --skipbuild                  Skip automatic build before run"
    echo "  --config PATH                Use alternate config source for apps.cfg build"
    echo "  --load-all 0|1              Override launcher auto-preload in generated apps.cfg"
    echo "  --run-first APP             Override launcher runappfirst in generated apps.cfg"
    echo "  --parse-trace-debug 0|1      Build ReadyShell in release(0) or debug-trace(1) profile"
    echo "  --parse-trace-debug=0|1      Same as above (equals syntax)"
    echo "  --interactive                Show a key-menu with common run combinations"
    echo "  -h, --help                   Show this help"
    echo ""
    echo "ReadyShell parse-trace profiles:"
    echo "  0  release/default: overlaysize 0x2440, overlaystart 0xA1C0"
    echo "  1  debug trace:    overlaysize 0x2480, overlaystart 0xA180"
    echo ""
    echo "Common combinations:"
    echo "  ./run.sh"
    echo "  ./run.sh --config cfg/readyos_config.ini"
    echo "  ./run.sh --load-all 1 --run-first editor"
    echo "  ./run.sh --skipbuild"
    echo "  ./run.sh --parse-trace-debug 1"
    echo "  ./run.sh --parse-trace-debug 1 debug"
    echo "  ./run.sh --parse-trace-debug 1 readyshell-mon"
    echo "  ./run.sh --skipbuild --parse-trace-debug 1 readyshell-mon"
    echo "  ./run.sh xfilechk"
    echo "  ./run.sh --interactive"
    echo ""
    echo "Disks:"
    echo "  Drive 8 -> $DISK_FILE_1"
    echo "  Drive 9 -> $DISK_FILE_2"
    echo ""
    echo "Keyboard shortcuts in Ready OS:"
    echo "  F1        Load all apps to REU (in launcher)"
    echo "  F3        Load selected app to REU (in launcher)"
    echo "  RETURN    Launch selected app"
    echo "  CTRL+B    Return to launcher (from any app)"
    echo "  F2        Switch to next app"
    echo "  F4        Switch to previous app"
    echo ""
    echo "VICE shortcuts:"
    echo "  Alt+M     Open monitor (debugger)"
    echo "  Alt+W     Toggle warp mode"
    echo "  Alt+P     Toggle pause"
    echo "  Alt+R     Reset"
    echo "  Alt+Q     Quit"
    echo ""
    echo "Monitor commands for debugging:"
    echo "  m C800 C9ff     View shim memory"
    echo "  m C820 C83f     View shim data area"
    echo "  m 1000 1020     View app space start"
    echo "  d C800          Disassemble shim"
    echo "  break C809      Break on preload"
    echo "  break C80c      Break on return_to_launcher"
    echo "  break C80f      Break on switch_app"
    echo ""
    echo "Remote-monitor mode (readyshell-mon):"
    echo "  Text monitor socket:   $REMOTE_MON_ADDR"
    echo "  Binary monitor socket: $BINARY_MON_ADDR"
    echo "  Text monitor log:      $REMOTE_MON_LOG"
    echo "  VICE stdio log:        $VICE_STDIO_LOG"
}

validate_parse_trace_debug() {
    local value="$1"
    case "$value" in
        0|1) ;;
        *)
            echo "Error: --parse-trace-debug must be 0 or 1 (got '$value')."
            exit 1
            ;;
    esac
}

validate_load_all_override() {
    local value="$1"
    case "$value" in
        0|1) ;;
        *)
            echo "Error: --load-all must be 0 or 1 (got '$value')."
            exit 1
            ;;
    esac
}

validate_run_first_override() {
    local value="$1"
    if [[ ! "$value" =~ ^[a-z0-9_.-]+$ ]]; then
        echo "Error: --run-first must be a lowercase prg token (got '$value')."
        exit 1
    fi
    if [[ "$value" == *.prg ]]; then
        echo "Error: --run-first must not include .prg (got '$value')."
        exit 1
    fi
    if [ "${#value}" -gt 12 ]; then
        echo "Error: --run-first must be 12 characters or fewer (got '$value')."
        exit 1
    fi
}

current_parse_trace_debug() {
    if [ -n "$PARSE_TRACE_DEBUG" ]; then
        echo "$PARSE_TRACE_DEBUG"
        return
    fi
    if [ -n "${READYSHELL_PARSE_TRACE_DEBUG:-}" ]; then
        echo "$READYSHELL_PARSE_TRACE_DEBUG"
        return
    fi
    # Keep this in sync with Makefile default.
    echo "0"
}

current_parse_trace_label() {
    local value
    value="$(current_parse_trace_debug)"
    case "$value" in
        1) echo "debug-trace (READYSHELL_OVERLAYSIZE=0x2480, __OVERLAYSTART__=0xA180)" ;;
        0) echo "release/default (READYSHELL_OVERLAYSIZE=0x2440, __OVERLAYSTART__=0xA1C0)" ;;
        *) echo "custom/unknown ($value)" ;;
    esac
}

run_interactive_menu() {
    local choice
    echo "ReadyOS interactive launcher"
    echo ""
    echo "Pick one option:"
    echo "  [1] Normal run (release profile build)"
    echo "  [2] Normal run (debug-trace profile build)"
    echo "  [3] Debug monitor mode (release profile build)"
    echo "  [4] Debug monitor mode (debug-trace profile build)"
    echo "  [5] Readyshell remote monitor (debug-trace profile build)"
    echo "  [6] Normal run (skip build, keep current binaries)"
    echo "  [q] Quit"
    printf "Choice: "
    IFS= read -r -n 1 choice
    echo ""
    case "$choice" in
        1)
            MODE=""
            PARSE_TRACE_DEBUG="0"
            SKIP_BUILD=0
            ;;
        2)
            MODE=""
            PARSE_TRACE_DEBUG="1"
            SKIP_BUILD=0
            ;;
        3)
            MODE="debug"
            PARSE_TRACE_DEBUG="0"
            SKIP_BUILD=0
            ;;
        4)
            MODE="debug"
            PARSE_TRACE_DEBUG="1"
            SKIP_BUILD=0
            ;;
        5)
            MODE="readyshell-mon"
            PARSE_TRACE_DEBUG="1"
            SKIP_BUILD=0
            ;;
        6)
            MODE=""
            PARSE_TRACE_DEBUG=""
            SKIP_BUILD=1
            ;;
        q|Q)
            echo "Cancelled."
            exit 0
            ;;
        *)
            echo "Invalid selection: '$choice'"
            exit 1
            ;;
    esac
}

# Function to check if disk image exists
check_disk() {
    local disk="$1"
    if [ ! -f "$disk" ]; then
        echo "Error: Disk image not found: $disk"
        echo "Run 'make' first to build."
        exit 1
    fi
}

check_os_disks() {
    check_disk "$DISK_FILE_1"
    check_disk "$DISK_FILE_2"
}

# Function to check if a prg file exists
check_prg() {
    local prg="$1"
    if [ ! -f "$prg" ]; then
        echo "Error: Program file not found: $prg"
        echo "Run 'make' first to build."
        exit 1
    fi
}

is_managed_build_file() {
    local name_lc="$1"
    local type_lc="$2"
    local f

    if [ "$type_lc" = "seq" ]; then
        for f in "${BUILD_MANAGED_SEQS[@]}"; do
            if [ "$name_lc" = "$f" ]; then
                return 0
            fi
        done
    fi

    return 1
}

collect_user_disk_files() {
    local src_disk="$1"
    local stage_dir="$2"
    local manifest="$stage_dir/manifest.tsv"
    local listing="$stage_dir/listing.txt"
    local idx=0
    local line
    local name
    local type
    local name_lc
    local host_file
    local rel_out
    local rec_len
    local read_spec

    : > "$manifest"
    c1541 "$src_disk" -list >"$listing" 2>/dev/null || true

    while IFS= read -r line; do
        if [[ "$line" != *\"*\"* ]]; then
            continue
        fi

        name="$(printf "%s\n" "$line" | sed -n 's/.*"\(.*\)".*/\1/p')"
        [ -n "$name" ] || continue

        type="$(printf "%s\n" "$line" | awk '{print tolower($NF)}')"
        case "$type" in
            seq|rel|usr) ;;
            *) continue ;;
        esac

        name_lc="$(printf "%s" "$name" | tr '[:upper:]' '[:lower:]')"
        if is_managed_build_file "$name_lc" "$type"; then
            continue
        fi

        idx=$((idx + 1))
        host_file="$stage_dir/file_${idx}.bin"

        if [ "$type" = "rel" ]; then
            rel_out="$(c1541 "$src_disk" -read "${name},l" "$host_file" 2>&1 || true)"
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
        esac

        if ! c1541 "$src_disk" -read "$read_spec" "$host_file" >/dev/null 2>/dev/null; then
            rm -f "$host_file"
            continue
        fi
        printf '%s\t%s\t0\t%s\n' "$name" "$type" "$host_file" >> "$manifest"
    done < "$listing"
}

restore_user_disk_files() {
    local dst_disk="$1"
    local manifest="$2"
    local name
    local type
    local rec_len
    local host_file
    local restore_spec
    local restored=0
    local failed=0

    [ -s "$manifest" ] || return 0

    while IFS=$'\t' read -r name type rec_len host_file; do
        [ -f "$host_file" ] || continue

        c1541 "$dst_disk" -delete "$name" >/dev/null 2>/dev/null || true
        case "$type" in
            rel)
                restore_spec="${name},l,${rec_len}"
                if c1541 "$dst_disk" -write "$host_file" "$restore_spec" >/dev/null 2>/dev/null; then
                    restored=$((restored + 1))
                else
                    echo "Warning: failed to restore REL file '$name'"
                    failed=$((failed + 1))
                fi
                ;;
            seq)
                restore_spec="${name},s"
                if c1541 "$dst_disk" -write "$host_file" "$restore_spec" >/dev/null 2>/dev/null; then
                    restored=$((restored + 1))
                else
                    echo "Warning: failed to restore SEQ file '$name'"
                    failed=$((failed + 1))
                fi
                ;;
            usr)
                restore_spec="${name},u"
                if c1541 "$dst_disk" -write "$host_file" "$restore_spec" >/dev/null 2>/dev/null; then
                    restored=$((restored + 1))
                else
                    echo "Warning: failed to restore USR file '$name'"
                    failed=$((failed + 1))
                fi
                ;;
        esac
    done < "$manifest"

    if [ "$restored" -gt 0 ]; then
        echo "Restored $restored user file(s) into $dst_disk"
    fi
    if [ "$failed" -gt 0 ]; then
        echo "Warning: $failed user file(s) could not be restored"
    fi
}

ensure_cal26_rel_seeded() {
    local listing
    local events_blocks
    local cfg_blocks
    if [ ! -f "$DISK_FILE_1" ]; then
        return
    fi

    listing="$(c1541 "$DISK_FILE_1" -list 2>/dev/null || true)"
    events_blocks="$(echo "$listing" | awk 'BEGIN{IGNORECASE=1} /"cal26.rel"[[:space:]]*rel/{print $1; exit}')"
    cfg_blocks="$(echo "$listing" | awk 'BEGIN{IGNORECASE=1} /"cal26cfg.rel"[[:space:]]*rel/{print $1; exit}')"

    if [ -n "$events_blocks" ] && [ -n "$cfg_blocks" ] && \
       [ "$events_blocks" -gt 0 ] && [ "$cfg_blocks" -gt 0 ]; then
        return
    fi

    echo "Seeding/repairing CAL26 REL files on $DISK_FILE_1 ..."
    python3 "$BUILD_SUPPORT_DIR/seed_cal26_rel.py" --disk "$DISK_FILE_1" >/dev/null

    # Validate again after seeding so run.sh never launches with missing/empty data files.
    listing="$(c1541 "$DISK_FILE_1" -list 2>/dev/null || true)"
    events_blocks="$(echo "$listing" | awk 'BEGIN{IGNORECASE=1} /"cal26.rel"[[:space:]]*rel/{print $1; exit}')"
    cfg_blocks="$(echo "$listing" | awk 'BEGIN{IGNORECASE=1} /"cal26cfg.rel"[[:space:]]*rel/{print $1; exit}')"
    if [ -z "$events_blocks" ] || [ -z "$cfg_blocks" ] || \
       [ "$events_blocks" -le 0 ] || [ "$cfg_blocks" -le 0 ]; then
        echo "Error: CAL26 data files not valid on disk after seeding."
        echo "Expected non-empty REL files: cal26.rel, cal26cfg.rel"
        exit 1
    fi
}

# Function to print startup info
print_info() {
    local mode="$1"
    local target="$2"
    echo "=== Ready OS ==="
    echo ""
    echo "Mode: $mode"
    echo "VICE: $VICE"
    echo "Target: $target"
    echo "Disk8: $DISK_FILE_1"
    echo "Disk9: $DISK_FILE_2"
    echo "Build Support: $BUILD_SUPPORT_DIR"
    echo "Config Source: $CONFIG_SOURCE"
    if [ -n "$CONFIG_OVERRIDE_LOAD_ALL" ]; then
        echo "Config Override load_all_to_reu: $CONFIG_OVERRIDE_LOAD_ALL"
    fi
    if [ -n "$CONFIG_OVERRIDE_RUN_FIRST" ]; then
        echo "Config Override runappfirst: $CONFIG_OVERRIDE_RUN_FIRST"
    fi
    echo "ReadyShell parse trace: $(current_parse_trace_label)"
    echo ""
}

# Build by default unless --skipbuild is passed
maybe_build() {
    local preserve_dir_1=""
    local preserve_manifest_1=""
    local preserve_previous_disk_1=""
    local preserve_count_1=0
    local preserve_dir_2=""
    local preserve_manifest_2=""
    local preserve_previous_disk_2=""
    local preserve_count_2=0
    local MAKE_ARGS=()

    update_dynamic_version
    echo "Build version: ${RUN_VERSION_TEXT}"
    echo "Config source: ${CONFIG_SOURCE}"
    if [ -n "$CONFIG_OVERRIDE_LOAD_ALL" ]; then
        echo "Config override load_all_to_reu: ${CONFIG_OVERRIDE_LOAD_ALL}"
    fi
    if [ -n "$CONFIG_OVERRIDE_RUN_FIRST" ]; then
        echo "Config override runappfirst: ${CONFIG_OVERRIDE_RUN_FIRST}"
    fi
    echo "ReadyShell parse trace profile: $(current_parse_trace_label)"

    if [ -n "$PARSE_TRACE_DEBUG" ]; then
        export READYSHELL_PARSE_TRACE_DEBUG="$PARSE_TRACE_DEBUG"
    elif [ -n "${READYSHELL_PARSE_TRACE_DEBUG:-}" ]; then
        validate_parse_trace_debug "$READYSHELL_PARSE_TRACE_DEBUG"
    fi

    if [ "$SKIP_BUILD" -eq 1 ]; then
        echo "Skipping build (--skipbuild): binaries keep their previous embedded version."
        if [ -n "$PARSE_TRACE_DEBUG" ]; then
            echo "Note: --parse-trace-debug affects build profile only. With --skipbuild, it does not rebuild binaries."
        fi
        echo ""
        return
    fi

    if [ ! -f "$CONFIG_SOURCE" ]; then
        echo "Error: Config source not found: $CONFIG_SOURCE"
        exit 1
    fi

    if [ -f "$DISK_FILE_1" ]; then
        preserve_dir_1="$(mktemp -d /tmp/readyos_preserve_d8.XXXXXX)"
        preserve_previous_disk_1="$preserve_dir_1/previous.d71"
        cp -f "$DISK_FILE_1" "$preserve_previous_disk_1"
        preserve_manifest_1="$preserve_dir_1/manifest.tsv"
        collect_user_disk_files "$preserve_previous_disk_1" "$preserve_dir_1"
        if [ -s "$preserve_manifest_1" ]; then
            preserve_count_1="$(wc -l < "$preserve_manifest_1" | tr -d '[:space:]')"
            echo "Preserving $preserve_count_1 user file(s) from existing disk 8..."
        fi
    fi

    if [ -f "$DISK_FILE_2" ]; then
        preserve_dir_2="$(mktemp -d /tmp/readyos_preserve_d9.XXXXXX)"
        preserve_previous_disk_2="$preserve_dir_2/previous.d71"
        cp -f "$DISK_FILE_2" "$preserve_previous_disk_2"
        preserve_manifest_2="$preserve_dir_2/manifest.tsv"
        collect_user_disk_files "$preserve_previous_disk_2" "$preserve_dir_2"
        if [ -s "$preserve_manifest_2" ]; then
            preserve_count_2="$(wc -l < "$preserve_manifest_2" | tr -d '[:space:]')"
            echo "Preserving $preserve_count_2 user file(s) from existing disk 9..."
        fi
    fi

    echo "Building fresh disk images (use --skipbuild to skip)..."
    MAKE_ARGS=(-B "BUILD_SUPPORT_DIR=$BUILD_SUPPORT_DIR" "READYOS_CONFIG_SRC=$CONFIG_SOURCE")
    if [ -n "$CONFIG_OVERRIDE_LOAD_ALL" ]; then
        MAKE_ARGS+=("READYOS_CONFIG_LOAD_ALL=$CONFIG_OVERRIDE_LOAD_ALL")
    fi
    if [ -n "$CONFIG_OVERRIDE_RUN_FIRST" ]; then
        MAKE_ARGS+=("READYOS_CONFIG_RUN_FIRST=$CONFIG_OVERRIDE_RUN_FIRST")
    fi
    MAKE_ARGS+=("$DISK_FILE_1" "$DISK_FILE_2")
    if ! make "${MAKE_ARGS[@]}"; then
        echo "Build failed during disk rebuild."
        if [ -n "$preserve_previous_disk_1" ] && [ -f "$preserve_previous_disk_1" ]; then
            echo "Restoring previous disk 8 image to keep user data..."
            cp -f "$preserve_previous_disk_1" "$DISK_FILE_1"
        fi
        if [ -n "$preserve_previous_disk_2" ] && [ -f "$preserve_previous_disk_2" ]; then
            echo "Restoring previous disk 9 image to keep user data..."
            cp -f "$preserve_previous_disk_2" "$DISK_FILE_2"
        fi
        [ -n "$preserve_dir_1" ] && [ -d "$preserve_dir_1" ] && rm -rf "$preserve_dir_1"
        [ -n "$preserve_dir_2" ] && [ -d "$preserve_dir_2" ] && rm -rf "$preserve_dir_2"
        return 1
    fi

    if [ -n "$preserve_manifest_1" ] && [ -s "$preserve_manifest_1" ]; then
        restore_user_disk_files "$DISK_FILE_1" "$preserve_manifest_1"
    fi
    if [ -n "$preserve_manifest_2" ] && [ -s "$preserve_manifest_2" ]; then
        restore_user_disk_files "$DISK_FILE_2" "$preserve_manifest_2"
    fi
    [ -n "$preserve_dir_1" ] && [ -d "$preserve_dir_1" ] && rm -rf "$preserve_dir_1"
    [ -n "$preserve_dir_2" ] && [ -d "$preserve_dir_2" ] && rm -rf "$preserve_dir_2"
    echo ""
}

# Parse global flags and mode
SKIP_BUILD=0
MODE=""
DEBUG_MONCMDS=""
PARSE_TRACE_DEBUG=""
INTERACTIVE=0
cleanup_tmp() {
    [ -n "$DEBUG_MONCMDS" ] && rm -f "$DEBUG_MONCMDS"
}
trap cleanup_tmp EXIT

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            show_help
            exit 0
            ;;
        --skipbuild)
            SKIP_BUILD=1
            shift
            ;;
        --config)
            if [ $# -lt 2 ]; then
                echo "Error: --config requires a path."
                exit 1
            fi
            CONFIG_SOURCE="$2"
            shift 2
            ;;
        --config=*)
            CONFIG_SOURCE="${1#*=}"
            shift
            ;;
        --load-all)
            if [ $# -lt 2 ]; then
                echo "Error: --load-all requires 0 or 1."
                exit 1
            fi
            CONFIG_OVERRIDE_LOAD_ALL="$2"
            validate_load_all_override "$CONFIG_OVERRIDE_LOAD_ALL"
            shift 2
            ;;
        --load-all=*)
            CONFIG_OVERRIDE_LOAD_ALL="${1#*=}"
            validate_load_all_override "$CONFIG_OVERRIDE_LOAD_ALL"
            shift
            ;;
        --run-first)
            if [ $# -lt 2 ]; then
                echo "Error: --run-first requires a prg token."
                exit 1
            fi
            CONFIG_OVERRIDE_RUN_FIRST="$2"
            validate_run_first_override "$CONFIG_OVERRIDE_RUN_FIRST"
            shift 2
            ;;
        --run-first=*)
            CONFIG_OVERRIDE_RUN_FIRST="${1#*=}"
            validate_run_first_override "$CONFIG_OVERRIDE_RUN_FIRST"
            shift
            ;;
        --interactive)
            INTERACTIVE=1
            shift
            ;;
        --parse-trace-debug)
            if [ $# -lt 2 ]; then
                echo "Error: --parse-trace-debug requires value 0 or 1."
                exit 1
            fi
            PARSE_TRACE_DEBUG="$2"
            validate_parse_trace_debug "$PARSE_TRACE_DEBUG"
            shift 2
            ;;
        --parse-trace-debug=*)
            PARSE_TRACE_DEBUG="${1#*=}"
            validate_parse_trace_debug "$PARSE_TRACE_DEBUG"
            shift
            ;;
        *)
            if [ -n "$MODE" ]; then
                echo "Unknown option: $1"
                echo ""
                show_help
                exit 1
            fi
            MODE="$1"
            shift
            ;;
    esac
done

if [ -n "$PARSE_TRACE_DEBUG" ]; then
    export READYSHELL_PARSE_TRACE_DEBUG="$PARSE_TRACE_DEBUG"
elif [ -n "${READYSHELL_PARSE_TRACE_DEBUG:-}" ]; then
    validate_parse_trace_debug "$READYSHELL_PARSE_TRACE_DEBUG"
fi

if [ "$SKIP_BUILD" -eq 1 ]; then
    if [ "$CONFIG_SOURCE" != "$DEFAULT_CONFIG_SOURCE" ] || \
       [ -n "$CONFIG_OVERRIDE_LOAD_ALL" ] || \
       [ -n "$CONFIG_OVERRIDE_RUN_FIRST" ]; then
        echo "Error: --skipbuild cannot be combined with --config, --load-all, or --run-first."
        exit 1
    fi
fi

if [ "$INTERACTIVE" -eq 1 ]; then
    if [ -n "$MODE" ]; then
        echo "Error: --interactive cannot be combined with an explicit mode ('$MODE')."
        exit 1
    fi
    run_interactive_menu
fi

# Parse command line option
case "${MODE:-}" in
    help|-h|--help)
        show_help
        exit 0
        ;;

    test)
        maybe_build
        check_prg "$TEST_PRG"
        print_info "REU Test" "$TEST_PRG"
        echo "Running standalone REU test program..."
        echo ""
        $VICE "${VICE_OPTS[@]}" -autostartprgmode 1 "$TEST_PRG"
        ;;

    debug)
        maybe_build
        check_os_disks
        ensure_cal26_rel_seeded
        check_prg "$PREBOOT_PRG"
        print_info "Debug" "$PREBOOT_PRG"
        echo "Debug mode - VICE monitor available (Alt+M)"
        echo ""
        echo "Useful breakpoints to set in monitor:"
        echo "  break C800   - load_disk"
        echo "  break C803   - load_reu"
        echo "  break C809   - preload"
        echo "  break C80c   - return_to_launcher"
        echo "  break C80f   - switch_app"
        echo "  break 1000   - app entry point"
        echo ""
        DEBUG_MONCMDS="$(mktemp /tmp/vice_debug_XXXXXX.cmd)"
        cat > "$DEBUG_MONCMDS" <<EOF
break C809
break C80c
break C80f
EOF
        $VICE "${VICE_OPTS[@]}" \
            -8 "$DISK_FILE_1" \
            -9 "$DISK_FILE_2" \
            -moncommands "$DEBUG_MONCMDS" \
            -autostart "$PREBOOT_PRG"
        ;;

    warp)
        maybe_build
        check_os_disks
        ensure_cal26_rel_seeded
        check_prg "$PREBOOT_PRG"
        print_info "Warp Mode" "$PREBOOT_PRG"
        echo "Warp mode enabled - max speed loading"
        echo "(Press Alt+W in VICE to toggle warp off)"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -warp \
            -8 "$DISK_FILE_1" \
            -9 "$DISK_FILE_2" \
            -autostart "$PREBOOT_PRG"
        ;;

    launcher)
        maybe_build
        check_os_disks
        ensure_cal26_rel_seeded
        check_prg "$LAUNCHER_PRG"
        print_info "Direct Launch" "$LAUNCHER_PRG"
        echo "Running launcher directly (skipping boot loader)"
        echo "Note: Shim will NOT be installed - REU switching won't work!"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -8 "$DISK_FILE_1" \
            -9 "$DISK_FILE_2" \
            -autostartprgmode 1 \
            "$LAUNCHER_PRG"
        ;;

    editor)
        maybe_build
        check_prg "$EDITOR_PRG"
        print_info "Standalone" "$EDITOR_PRG"
        echo "Running editor standalone (no REU, no shim)"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -autostartprgmode 1 \
            "$EDITOR_PRG"
        ;;

    calcplus)
        maybe_build
        check_prg "$CALCPLUS_PRG"
        print_info "Standalone" "$CALCPLUS_PRG"
        echo "Running calculator plus standalone (no REU, no shim)"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -autostartprgmode 1 \
            "$CALCPLUS_PRG"
        ;;

    hexview)
        maybe_build
        check_prg "$HEXVIEW_PRG"
        print_info "Standalone" "$HEXVIEW_PRG"
        echo "Running hex viewer standalone (no REU, no shim)"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -autostartprgmode 1 \
            "$HEXVIEW_PRG"
        ;;

    2048)
        maybe_build
        check_prg "$GAME2048_PRG"
        print_info "Standalone" "$GAME2048_PRG"
        echo "Running 2048 standalone (no REU, no shim)"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -autostartprgmode 1 \
            "$GAME2048_PRG"
        ;;

    deminer)
        maybe_build
        check_prg "$DEMINER_PRG"
        print_info "Standalone" "$DEMINER_PRG"
        echo "Running Deminer standalone (no REU, no shim)"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -autostartprgmode 1 \
            "$DEMINER_PRG"
        ;;

    cal26)
        maybe_build
        check_disk "$DISK_FILE_1"
        ensure_cal26_rel_seeded
        check_prg "$CAL26_PRG"
        print_info "Standalone" "$CAL26_PRG"
        echo "Running CAL26 standalone (no REU, no shim, disk attached)"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -8 "$DISK_FILE_1" \
            -autostartprgmode 1 \
            "$CAL26_PRG"
        ;;

    dizzy)
        maybe_build
        check_disk "$DISK_FILE_1"
        check_prg "$DIZZY_PRG"
        print_info "Standalone" "$DIZZY_PRG"
        echo "Running DIZZY standalone (no REU, no shim)"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -8 "$DISK_FILE_1" \
            -autostartprgmode 1 \
            "$DIZZY_PRG"
        ;;

    readme)
        maybe_build
        check_prg "$README_PRG"
        print_info "Standalone" "$README_PRG"
        echo "Running README app standalone (no REU, no shim)"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -autostartprgmode 1 \
            "$README_PRG"
        ;;

    showcfg)
        maybe_build
        check_disk "$DISK_FILE_1"
        check_prg "$SHOWCFG_PRG"
        print_info "Catalog Inspector" "$SHOWCFG_PRG"
        echo "Running BASIC APPS.CFG inspector (reads apps.cfg from drive 8)"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -8 "$DISK_FILE_1" \
            -9 "$DISK_FILE_2" \
            -autostartprgmode 1 \
            "$SHOWCFG_PRG"
        ;;

    xfilechk)
        update_dynamic_version
        echo "Build version: ${RUN_VERSION_TEXT}"
        if [ "$SKIP_BUILD" -eq 1 ]; then
            echo "Skipping build (--skipbuild): using existing standalone IEC harness artifacts."
            echo ""
        else
            echo "Building standalone IEC harness disks..."
            make -B BUILD_SUPPORT_DIR="$BUILD_SUPPORT_DIR" \
                "$XFILECHK_BOOT_PRG" \
                "$XFILECHK_PRG" \
                "$XFILECHK_DISK_FILE_1" \
                "$XFILECHK_DISK_FILE_2"
            echo ""
        fi
        check_prg "$XFILECHK_BOOT_PRG"
        check_prg "$XFILECHK_PRG"
        check_disk "$XFILECHK_DISK_FILE_1"
        check_disk "$XFILECHK_DISK_FILE_2"
        DISK_FILE_1="$XFILECHK_DISK_FILE_1"
        DISK_FILE_2="$XFILECHK_DISK_FILE_2"
        print_info "Standalone IEC Harness" "$XFILECHK_BOOT_PRG"
        echo "Running xfilechk on dedicated D71 fixtures"
        echo "Drive 8: $XFILECHK_DISK_FILE_1"
        echo "Drive 9: $XFILECHK_DISK_FILE_2"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -8 "$XFILECHK_DISK_FILE_1" \
            -9 "$XFILECHK_DISK_FILE_2" \
            -autostart "$XFILECHK_BOOT_PRG"
        ;;

    monitor)
        maybe_build
        check_os_disks
        ensure_cal26_rel_seeded
        check_prg "$PREBOOT_PRG"
        print_info "Monitor" "$PREBOOT_PRG"
        echo "Starting with monitor open..."
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -initbreak 0xC80D \
            -8 "$DISK_FILE_1" \
            -9 "$DISK_FILE_2" \
            -autostart "$PREBOOT_PRG"
        ;;

    readyshell-mon)
        maybe_build
        check_os_disks
        ensure_cal26_rel_seeded
        check_prg "$PREBOOT_PRG"
        mkdir -p logs
        : > "$REMOTE_MON_LOG"
        : > "$VICE_STDIO_LOG"
        print_info "Readyshell Remote Monitor" "$PREBOOT_PRG"
        echo "Starting ReadyOS with remote monitor endpoints enabled:"
        echo "  text monitor:   $REMOTE_MON_ADDR"
        echo "  binary monitor: $BINARY_MON_ADDR"
        echo "  text log:       $REMOTE_MON_LOG"
        echo "  VICE stdio log: $VICE_STDIO_LOG"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -remotemonitor \
            -remotemonitoraddress "$REMOTE_MON_ADDR" \
            -binarymonitor \
            -binarymonitoraddress "$BINARY_MON_ADDR" \
            -monlog \
            -monlogname "$REMOTE_MON_LOG" \
            -8 "$DISK_FILE_1" \
            -9 "$DISK_FILE_2" \
            -autostart "$PREBOOT_PRG" \
            >"$VICE_STDIO_LOG" 2>&1
        ;;

    noreu)
        maybe_build
        check_os_disks
        ensure_cal26_rel_seeded
        check_prg "$PREBOOT_PRG"
        print_info "No REU" "$PREBOOT_PRG"
        echo "Running WITHOUT REU (testing error handling)"
        echo ""
        $VICE \
            -8 "$DISK_FILE_1" \
            -9 "$DISK_FILE_2" \
            -autostart "$PREBOOT_PRG"
        ;;

    "")
        # Default: normal run
        maybe_build
        check_os_disks
        ensure_cal26_rel_seeded
        check_prg "$PREBOOT_PRG"
        print_info "Normal" "$PREBOOT_PRG"
        echo "Instructions:"
        echo "  1. preboot.prg loads/runs setd71.prg; setd71 sets drives 8/9 to 1571 mode, then loads boot.prg"
        echo "  2. Boot loader installs shim and loads launcher"
        echo "  3. Select 'LOAD TO REU' and press RETURN to preload apps"
        echo "  4. Select an app (including 2048 and deminer) and press RETURN to launch"
        echo "  5. Press CTRL+B to return to launcher"
        echo "  6. Press F2/F4 to switch between apps"
        echo ""
        $VICE "${VICE_OPTS[@]}" \
            -8 "$DISK_FILE_1" \
            -9 "$DISK_FILE_2" \
            -autostart "$PREBOOT_PRG"
        ;;

    *)
        echo "Unknown option: ${MODE:-}"
        echo ""
        show_help
        exit 1
        ;;
esac
