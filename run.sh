#!/bin/bash
#
# ReadyOS profile-aware run script.
#

set -euo pipefail
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

PROFILE_TOOL="$BUILD_SUPPORT_DIR/readyos_profiles.py"
VERSION_TOOL="$BUILD_SUPPORT_DIR/update_build_version.py"
DEFAULT_PROFILE="$(python3 "$PROFILE_TOOL" default-id)"
PROFILE="$DEFAULT_PROFILE"

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

REMOTE_MON_ADDR="127.0.0.1:6510"
BINARY_MON_ADDR="127.0.0.1:6502"
REMOTE_MON_LOG="logs/vice_remote_monitor.log"
VICE_STDIO_LOG="logs/vice_readyshell_mon.out"
VICE_LOG_FILE="logs/vice.log"

CONFIG_SOURCE=""
CONFIG_OVERRIDE_LOAD_ALL=""
CONFIG_OVERRIDE_RUN_FIRST=""
RUN_VERSION_TEXT=""
SKIP_BUILD=0
BUILD_ALL=0
MODE=""
PARSE_TRACE_DEBUG=""
INTERACTIVE=0
LIST_PROFILES=0
DEBUG_MONCMDS=""

if command -v x64sc >/dev/null 2>&1; then
    VICE="x64sc"
elif command -v x64 >/dev/null 2>&1; then
    VICE="x64"
else
    echo "Error: VICE emulator not found (tried x64sc, x64)"
    exit 1
fi

cleanup_tmp() {
    [ -n "$DEBUG_MONCMDS" ] && rm -f "$DEBUG_MONCMDS"
}
trap cleanup_tmp EXIT

validate_parse_trace_debug() {
    case "$1" in
        0|1) ;;
        *)
            echo "Error: --parse-trace-debug must be 0 or 1 (got '$1')."
            exit 1
            ;;
    esac
}

validate_load_all_override() {
    case "$1" in
        0|1) ;;
        *)
            echo "Error: --load-all must be 0 or 1 (got '$1')."
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
    echo "0"
}

current_parse_trace_label() {
    case "$(current_parse_trace_debug)" in
        1) echo "debug-trace (READYSHELL_OVERLAYSIZE=0x2480, __OVERLAYSTART__=0xA180)" ;;
        0) echo "release/default (READYSHELL_OVERLAYSIZE=0x2440, __OVERLAYSTART__=0xA1C0)" ;;
        *) echo "custom/unknown" ;;
    esac
}

show_help() {
    echo "ReadyOS Run Script"
    echo ""
    echo "Usage: ./run.sh [flags] [option]"
    echo ""
    echo "Modes (option):"
    echo "  (none)         Run ReadyOS normally"
    echo "  test           Run REU test program standalone"
    echo "  debug          Run with VICE monitor breakpoints at shim entry points"
    echo "  warp           Run in warp mode"
    echo "  launcher       Run launcher.prg directly"
    echo "  editor         Run editor.prg directly"
    echo "  calcplus       Run calcplus.prg directly"
    echo "  hexview        Run hexview.prg directly"
    echo "  2048           Run game2048.prg directly"
    echo "  deminer        Run deminer.prg directly"
    echo "  cal26          Run cal26.prg directly"
    echo "  dizzy          Run dizzy.prg directly"
    echo "  readme         Run readme.prg directly"
    echo "  showcfg        Run BASIC APPS.CFG inspector"
    echo "  xfilechk       Run standalone IEC file-operation harness"
    echo "  monitor        Start with VICE monitor open immediately"
    echo "  readyshell-mon Normal boot with remote monitor endpoints"
    echo "  noreu          Run boot without REU"
    echo "  help           Show this help"
    echo ""
    echo "Flags:"
    echo "  --profile ID                Select release profile (default: $DEFAULT_PROFILE)"
    echo "  --list-profiles             List available profiles and exit"
    echo "  --build-all                 Build every release profile and exit"
    echo "  --skipbuild                 Skip automatic build before run"
    echo "  --config PATH               Override the profile's catalog source"
    echo "  --load-all 0|1              Override launcher auto-preload in generated apps.cfg"
    echo "  --run-first APP             Override launcher runappfirst in generated apps.cfg"
    echo "  --parse-trace-debug 0|1     Build ReadyShell in release(0) or debug-trace(1) profile"
    echo "  --interactive               Show a key-menu with common launch combinations"
    echo "  -h, --help                  Show this help"
}

run_interactive_menu() {
    local choice
    echo "ReadyOS interactive launcher"
    echo ""
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
        1) MODE=""; PARSE_TRACE_DEBUG="0"; SKIP_BUILD=0 ;;
        2) MODE=""; PARSE_TRACE_DEBUG="1"; SKIP_BUILD=0 ;;
        3) MODE="debug"; PARSE_TRACE_DEBUG="0"; SKIP_BUILD=0 ;;
        4) MODE="debug"; PARSE_TRACE_DEBUG="1"; SKIP_BUILD=0 ;;
        5) MODE="readyshell-mon"; PARSE_TRACE_DEBUG="1"; SKIP_BUILD=0 ;;
        6) MODE=""; PARSE_TRACE_DEBUG=""; SKIP_BUILD=1 ;;
        q|Q) exit 0 ;;
        *)
            echo "Invalid selection: '$choice'"
            exit 1
            ;;
    esac
}

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            show_help
            exit 0
            ;;
        --profile)
            PROFILE="$2"
            shift 2
            ;;
        --profile=*)
            PROFILE="${1#*=}"
            shift
            ;;
        --list-profiles)
            LIST_PROFILES=1
            shift
            ;;
        --build-all)
            BUILD_ALL=1
            shift
            ;;
        --skipbuild)
            SKIP_BUILD=1
            shift
            ;;
        --config)
            CONFIG_SOURCE="$2"
            shift 2
            ;;
        --config=*)
            CONFIG_SOURCE="${1#*=}"
            shift
            ;;
        --load-all)
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
                exit 1
            fi
            MODE="$1"
            shift
            ;;
    esac
done

if [ "$LIST_PROFILES" -eq 1 ]; then
    python3 "$PROFILE_TOOL" list-ids
    exit 0
fi

if [ "$BUILD_ALL" -eq 1 ] && [ "$SKIP_BUILD" -eq 1 ]; then
    echo "Error: --build-all cannot be combined with --skipbuild."
    exit 1
fi

if [ "$BUILD_ALL" -eq 1 ] && [ -n "$MODE" ]; then
    echo "Error: --build-all cannot be combined with explicit mode '$MODE'."
    exit 1
fi

if [ -n "$PARSE_TRACE_DEBUG" ]; then
    export READYSHELL_PARSE_TRACE_DEBUG="$PARSE_TRACE_DEBUG"
elif [ -n "${READYSHELL_PARSE_TRACE_DEBUG:-}" ]; then
    validate_parse_trace_debug "$READYSHELL_PARSE_TRACE_DEBUG"
fi

if [ "$SKIP_BUILD" -eq 1 ] && { [ -n "$CONFIG_SOURCE" ] || [ -n "$CONFIG_OVERRIDE_LOAD_ALL" ] || [ -n "$CONFIG_OVERRIDE_RUN_FIRST" ]; }; then
    echo "Error: --skipbuild cannot be combined with --config, --load-all, or --run-first."
    exit 1
fi

if [ "$INTERACTIVE" -eq 1 ]; then
    if [ -n "$MODE" ]; then
        echo "Error: --interactive cannot be combined with explicit mode '$MODE'."
        exit 1
    fi
    run_interactive_menu
fi

if [ -n "$CONFIG_SOURCE" ] && [ ! -f "$CONFIG_SOURCE" ]; then
    echo "Error: config source not found: $CONFIG_SOURCE"
    exit 1
fi

load_profile_env() {
    local mode="$1"
    local version_arg=()
    if [ "$mode" = "latest" ]; then
        local resolved
        resolved="$(python3 "$PROFILE_TOOL" resolve --profile "$PROFILE" --latest)"
        local version
        version="$(RESOLVED_JSON="$resolved" python3 - <<'PY'
import json, os
data = json.loads(os.environ["RESOLVED_JSON"])
print(data["version_text"])
PY
)"
        eval "$(python3 "$PROFILE_TOOL" export-shell --profile "$PROFILE" --version "$version")"
    else
        eval "$(python3 "$PROFILE_TOOL" export-shell --profile "$PROFILE" --version "$RUN_VERSION_TEXT")"
    fi
}

maybe_build() {
    if [ "$BUILD_ALL" -eq 1 ]; then
        RUN_VERSION_TEXT="$(python3 "$VERSION_TOOL" --next)"
        echo "Build version: $RUN_VERSION_TEXT"
        echo "Building all release profiles"
        echo "ReadyShell parse trace profile: $(current_parse_trace_label)"
        make -B "BUILD_SUPPORT_DIR=$BUILD_SUPPORT_DIR" "READYOS_VERSION_TEXT=$RUN_VERSION_TEXT" release-all
        return
    fi

    if [ "$SKIP_BUILD" -eq 1 ]; then
        echo "Skipping build (--skipbuild)."
        load_profile_env latest
        return
    fi

    RUN_VERSION_TEXT="$(python3 "$VERSION_TOOL" --next)"
    echo "Build version: $RUN_VERSION_TEXT"
    echo "Profile: $PROFILE"
    echo "ReadyShell parse trace profile: $(current_parse_trace_label)"
    if [ -n "$CONFIG_SOURCE" ]; then
        echo "Catalog override: $CONFIG_SOURCE"
    fi
    if [ -n "$CONFIG_OVERRIDE_LOAD_ALL" ]; then
        echo "Config override load_all_to_reu: $CONFIG_OVERRIDE_LOAD_ALL"
    fi
    if [ -n "$CONFIG_OVERRIDE_RUN_FIRST" ]; then
        echo "Config override runappfirst: $CONFIG_OVERRIDE_RUN_FIRST"
    fi

    local make_args=(-B "BUILD_SUPPORT_DIR=$BUILD_SUPPORT_DIR" "PROFILE=$PROFILE" "READYOS_VERSION_TEXT=$RUN_VERSION_TEXT")
    if [ -n "$CONFIG_SOURCE" ]; then
        make_args+=("READYOS_CONFIG_SRC=$CONFIG_SOURCE")
    fi
    if [ -n "$CONFIG_OVERRIDE_LOAD_ALL" ]; then
        make_args+=("READYOS_CONFIG_LOAD_ALL=$CONFIG_OVERRIDE_LOAD_ALL")
    fi
    if [ -n "$CONFIG_OVERRIDE_RUN_FIRST" ]; then
        make_args+=("READYOS_CONFIG_RUN_FIRST=$CONFIG_OVERRIDE_RUN_FIRST")
    fi
    make_args+=(profile)

    make "${make_args[@]}"
    load_profile_env build
}

check_prg() {
    if [ ! -f "$1" ]; then
        echo "Error: program file not found: $1"
        exit 1
    fi
}

check_profile_disks() {
    local disk
    for disk in "${PROFILE_DISK_PATHS[@]}"; do
        if [ ! -f "$disk" ]; then
            echo "Error: disk image not found: $disk"
            exit 1
        fi
    done
}

print_info() {
    local mode="$1"
    local target="$2"
    local idx=0
    echo "=== Ready OS ==="
    echo ""
    echo "Mode: $mode"
    echo "VICE: $VICE"
    echo "Profile: $PROFILE_DISPLAY_NAME"
    echo "Target: $target"
    for idx in "${!PROFILE_DISK_PATHS[@]}"; do
        echo "Drive ${PROFILE_DISK_DRIVES[$idx]}: ${PROFILE_DISK_PATHS[$idx]}"
    done
    echo "Build Support: $BUILD_SUPPORT_DIR"
    echo "ReadyShell parse trace: $(current_parse_trace_label)"
    echo ""
}

start_vice() {
    "$VICE" "$@"
}

case "${MODE:-}" in
    help|-h|--help) ;;
    xfilechk) ;;
    *)
        maybe_build
        ;;
esac

case "${MODE:-}" in
    help|-h|--help)
        show_help
        ;;
    "")
        if [ "$BUILD_ALL" -eq 1 ]; then
            exit 0
        fi
        check_profile_disks
        check_prg "$PROFILE_AUTOSTART_PRG"
        print_info "Normal" "$PROFILE_AUTOSTART_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 "${PROFILE_VICE_ATTACH_ARGS[@]}" -autostart "$PROFILE_AUTOSTART_PRG"
        ;;
    test)
        check_prg "$TEST_PRG"
        print_info "REU Test" "$TEST_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 -autostartprgmode 1 "$TEST_PRG"
        ;;
    debug)
        check_profile_disks
        check_prg "$PROFILE_AUTOSTART_PRG"
        print_info "Debug" "$PROFILE_AUTOSTART_PRG"
        DEBUG_MONCMDS="$(mktemp /tmp/vice_debug_XXXXXX.cmd)"
        printf 'break C809\nbreak C80c\nbreak C80f\n' >"$DEBUG_MONCMDS"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 "${PROFILE_VICE_ATTACH_ARGS[@]}" -moncommands "$DEBUG_MONCMDS" -autostart "$PROFILE_AUTOSTART_PRG"
        ;;
    warp)
        check_profile_disks
        check_prg "$PROFILE_AUTOSTART_PRG"
        print_info "Warp Mode" "$PROFILE_AUTOSTART_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 "${PROFILE_VICE_ATTACH_ARGS[@]}" -warp -autostart "$PROFILE_AUTOSTART_PRG"
        ;;
    launcher)
        check_profile_disks
        check_prg "$LAUNCHER_PRG"
        print_info "Direct Launch" "$LAUNCHER_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 "${PROFILE_VICE_ATTACH_ARGS[@]}" -autostartprgmode 1 "$LAUNCHER_PRG"
        ;;
    editor)
        check_prg "$EDITOR_PRG"
        print_info "Standalone" "$EDITOR_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 -autostartprgmode 1 "$EDITOR_PRG"
        ;;
    calcplus)
        check_prg "$CALCPLUS_PRG"
        print_info "Standalone" "$CALCPLUS_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 -autostartprgmode 1 "$CALCPLUS_PRG"
        ;;
    hexview)
        check_prg "$HEXVIEW_PRG"
        print_info "Standalone" "$HEXVIEW_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 -autostartprgmode 1 "$HEXVIEW_PRG"
        ;;
    2048)
        check_prg "$GAME2048_PRG"
        print_info "Standalone" "$GAME2048_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 -autostartprgmode 1 "$GAME2048_PRG"
        ;;
    deminer)
        check_prg "$DEMINER_PRG"
        print_info "Standalone" "$DEMINER_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 -autostartprgmode 1 "$DEMINER_PRG"
        ;;
    cal26)
        check_profile_disks
        check_prg "$CAL26_PRG"
        print_info "Standalone" "$CAL26_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 "${PROFILE_VICE_ATTACH_ARGS[@]}" -autostartprgmode 1 "$CAL26_PRG"
        ;;
    dizzy)
        check_profile_disks
        check_prg "$DIZZY_PRG"
        print_info "Standalone" "$DIZZY_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 "${PROFILE_VICE_ATTACH_ARGS[@]}" -autostartprgmode 1 "$DIZZY_PRG"
        ;;
    readme)
        check_prg "$README_PRG"
        print_info "Standalone" "$README_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 -autostartprgmode 1 "$README_PRG"
        ;;
    showcfg)
        check_profile_disks
        check_prg "$SHOWCFG_PRG"
        print_info "Catalog Inspector" "$SHOWCFG_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 "${PROFILE_VICE_ATTACH_ARGS[@]}" -autostartprgmode 1 "$SHOWCFG_PRG"
        ;;
    xfilechk)
        RUN_VERSION_TEXT="$(python3 "$VERSION_TOOL" --next)"
        echo "Build version: $RUN_VERSION_TEXT"
        if [ "$SKIP_BUILD" -eq 1 ]; then
            echo "Skipping build (--skipbuild): using existing standalone IEC harness artifacts."
        else
            make -B "BUILD_SUPPORT_DIR=$BUILD_SUPPORT_DIR" "$XFILECHK_BOOT_PRG" "$XFILECHK_PRG" "$XFILECHK_DISK_FILE_1" "$XFILECHK_DISK_FILE_2"
        fi
        check_prg "$XFILECHK_BOOT_PRG"
        check_prg "$XFILECHK_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 \
            -drive8type 1571 -drive8truedrive -devicebackend8 0 +busdevice8 -8 "$XFILECHK_DISK_FILE_1" \
            -drive9type 1571 -drive9truedrive -devicebackend9 0 +busdevice9 -9 "$XFILECHK_DISK_FILE_2" \
            -autostart "$XFILECHK_BOOT_PRG"
        ;;
    monitor)
        check_profile_disks
        check_prg "$PROFILE_AUTOSTART_PRG"
        print_info "Monitor" "$PROFILE_AUTOSTART_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 "${PROFILE_VICE_ATTACH_ARGS[@]}" -initbreak 0xC80D -autostart "$PROFILE_AUTOSTART_PRG"
        ;;
    readyshell-mon)
        check_profile_disks
        check_prg "$PROFILE_AUTOSTART_PRG"
        mkdir -p logs
        : > "$REMOTE_MON_LOG"
        : > "$VICE_STDIO_LOG"
        print_info "Readyshell Remote Monitor" "$PROFILE_AUTOSTART_PRG"
        start_vice -logfile "$VICE_LOG_FILE" -reu -reusize 16384 \
            "${PROFILE_VICE_ATTACH_ARGS[@]}" \
            -remotemonitor -remotemonitoraddress "$REMOTE_MON_ADDR" \
            -binarymonitor -binarymonitoraddress "$BINARY_MON_ADDR" \
            -monlog -monlogname "$REMOTE_MON_LOG" \
            -autostart "$PROFILE_AUTOSTART_PRG" >"$VICE_STDIO_LOG" 2>&1
        ;;
    noreu)
        check_profile_disks
        check_prg "$PROFILE_AUTOSTART_PRG"
        print_info "No REU" "$PROFILE_AUTOSTART_PRG"
        start_vice "${PROFILE_VICE_ATTACH_ARGS[@]}" -autostart "$PROFILE_AUTOSTART_PRG"
        ;;
    *)
        echo "Unknown option: ${MODE:-}"
        exit 1
        ;;
esac
