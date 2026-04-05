#!/usr/bin/env python3
"""
Ready OS Binary Verification Script

Performs structural shim checks plus deep binary/layout checks:
- boot.prg structure and shim byte layout
- shim jump table / routine alignment / critical REU instruction sequences
- app PRG load addresses and binary extents
- app linker map segment bounds against REU snapshot window ($1000-$C5FF)
- region conflict checks across app/REU metadata/shim/I/O boundaries
- app headroom warnings near the $C5FF snapshot ceiling
"""

import os
import re
import struct
import subprocess
import sys
import tempfile

os.chdir(os.path.dirname(os.path.abspath(__file__)))


APP_PRGS = [
    ("launcher", "launcher.prg"),
    ("editor", "editor.prg"),
    ("calcplus", "calcplus.prg"),
    ("hexview", "hexview.prg"),
    ("clipmgr", "clipmgr.prg"),
    ("reuviewer", "reuviewer.prg"),
    ("tasklist", "tasklist.prg"),
    ("game2048", "game2048.prg"),
    ("cal26", "cal26.prg"),
    ("dizzy", "dizzy.prg"),
    ("readme", "readme.prg"),
    ("readyshell", "readyshell.prg"),
]
READYSHELL_OVERLAY_PRGS = [
    ("readyshell.ovl1", "readyshell.prg.1"),
    ("readyshell.ovl2", "readyshell.prg.2"),
    ("readyshell.ovl3", "readyshell.prg.3"),
]

APP_LOAD_START = 0x1000
APP_SNAPSHOT_END = 0xC5FF      # Shim stashes/fetches exactly $1000-$C5FF ($B600 bytes)
APP_SNAPSHOT_SIZE = APP_SNAPSHOT_END - APP_LOAD_START + 1
APP_LINKER_END = 0xC5FF        # cfg/ready_app.cfg MAIN range upper bound
REU_META_START = 0xC600        # reu_mgr alloc table area
REU_META_END = 0xC7FF
SHIM_START = 0xC800
SHIM_END = 0xC9FF
IO_START = 0xD000
APP_HEADROOM_WARN = 1024
DISK_8 = "readyos.d71"
DISK_9 = "readyos_2.d71"


def parse_env_int(name, default):
    raw = os.environ.get(name)
    if raw is None:
        return default
    try:
        return int(raw, 0)
    except ValueError:
        return default


APP_HEADROOM_FAIL = parse_env_int("READYOS_MIN_HEADROOM_FAIL", 0)


def check(name, condition, detail=""):
    status = "OK" if condition else "FAIL"
    msg = f"  [{status}] {name}"
    if detail:
        msg += f" - {detail}"
    print(msg)
    return condition


def warn(name, detail=""):
    msg = f"  [WARN] {name}"
    if detail:
        msg += f" - {detail}"
    print(msg)


def intersects(a_start, a_end, b_start, b_end):
    return not (a_end < b_start or b_end < a_start)


def parse_prg_range(path):
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) < 2:
        raise ValueError("file too short for PRG header")
    load_addr = struct.unpack_from("<H", raw, 0)[0]
    payload = len(raw) - 2
    if payload == 0:
        raise ValueError("empty PRG payload")
    end_addr = load_addr + payload - 1
    return load_addr, end_addr, payload


def parse_map_segments(map_path):
    """
    Parse segment list from cc65 linker map.
    Returns dict: segment_name -> (start, end, size)
    """
    with open(map_path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    segments = {}
    in_seg = False

    seg_re = re.compile(
        r"^\s*([A-Z0-9_]+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+[0-9A-F]{5}\s*$"
    )

    for line in lines:
        if line.strip() == "Segment list:":
            in_seg = True
            continue

        if not in_seg:
            continue

        m = seg_re.match(line)
        if m:
            name = m.group(1)
            start = int(m.group(2), 16)
            end = int(m.group(3), 16)
            size = int(m.group(4), 16)
            segments[name] = (start, end, size)
            continue

        if segments and line.strip() == "":
            break

    if not segments:
        raise ValueError("segment list not found in map")

    return segments


def parse_launcher_runtime_contract(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()

    return {
        "max_apps": int(re.search(r"#define\s+MAX_APPS\s+(\d+)", src).group(1)),
        "max_file_len": int(re.search(r"#define\s+MAX_FILE_LEN\s+(\d+)", src).group(1)),
        "default_drive": int(re.search(r"#define\s+DEFAULT_DRIVE\s+(\d+)", src).group(1)),
        "supports_drive_prefix": 'draw_drive_field' in src and 'draw_drive_prefixed_name' in src,
        "uses_cfg_parser": 'load_catalog_from_disk' in src and 'parse_catalog_entry_line' in src,
        "cfg_open_spec": re.search(r'#define\s+APP_CFG_OPEN_SPEC\s+"([^"]+)"', src).group(1),
    }


def normalize_catalog_prg_token(prg_raw):
    prg = prg_raw.strip()
    if not prg:
        raise ValueError("empty PRG token")
    if has_upper_ascii(prg):
        raise ValueError(f"PRG token must be lowercase: {prg_raw!r}")
    if "," in prg:
        raise ValueError(f"comma suffix not allowed in PRG token: {prg_raw!r}")

    if prg.lower().endswith(".prg"):
        raise ValueError(f".prg extension not allowed in catalog: {prg_raw!r}")

    if len(prg) == 0 or len(prg) > 12:
        raise ValueError(f"PRG token length invalid: {prg_raw!r}")
    if not re.fullmatch(r"[a-z0-9_.-]+", prg):
        raise ValueError(f"invalid chars in PRG token: {prg_raw!r}")
    return prg


def has_upper_ascii(text):
    for ch in text:
        if "A" <= ch <= "Z":
            return True
    return False


def parse_apps_catalog(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        raw_lines = list(enumerate(f.read().splitlines(), start=1))

    def clean(line_no, line):
        line = line.strip()
        if not line:
            return None
        if line.startswith("#") or line.startswith(";"):
            return None
        if has_upper_ascii(line):
            raise ValueError(f"{path}:{line_no}: alphabetic text must be lowercase")
        return line

    logical = []
    for line_no, line in raw_lines:
        cleaned = clean(line_no, line)
        if cleaned is not None:
            logical.append((line_no, cleaned))

    entries = []
    i = 0
    while i < len(logical):
        entry_no, entry_line = logical[i]
        i += 1
        parts = [p.strip() for p in entry_line.split(":", 2)]
        if len(parts) != 3:
            raise ValueError(f"{path}:{entry_no}: malformed catalog line: {entry_line!r}")
        drive_raw, prg, label = parts
        if not drive_raw.isdigit():
            raise ValueError(f"{path}:{entry_no}: invalid drive token: {drive_raw!r}")
        drive = int(drive_raw, 10)
        if drive < 8 or drive > 11:
            raise ValueError(f"{path}:{entry_no}: drive must be 8..11: {drive}")
        prg_norm = normalize_catalog_prg_token(prg)
        if len(label) == 0:
            raise ValueError(f"{path}:{entry_no}: empty display name")
        if len(label) > 31:
            raise ValueError(f"{path}:{entry_no}: display name too long ({len(label)} > 31)")
        if i >= len(logical):
            raise ValueError(f"{path}:{entry_no}: missing description for entry: {entry_line!r}")
        desc_no, desc = logical[i]
        i += 1
        if len(desc) == 0:
            raise ValueError(f"{path}:{desc_no}: empty description")
        if len(desc) > 38:
            raise ValueError(f"{path}:{desc_no}: description too long ({len(desc)} > 38)")
        entries.append(
            {
                "drive": drive,
                "prg": prg_norm,
                "label": label,
                "desc": desc,
            }
        )

    return entries


def parse_reu_mgr_contract(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()

    def read_define(name):
        m = re.search(rf"#define\s+{name}\s+(\d+)", src)
        if not m:
            raise ValueError(f"{name} definition not found in reu_mgr.h")
        return int(m.group(1), 10)

    return {
        "reu_total_banks": read_define("REU_TOTAL_BANKS"),
        "reu_first_dynamic": read_define("REU_FIRST_DYNAMIC"),
        "reu_type_free": read_define("REU_FREE"),
        "reu_type_app_state": read_define("REU_APP_STATE"),
        "reu_type_clipboard": read_define("REU_CLIPBOARD"),
        "reu_type_app_alloc": read_define("REU_APP_ALLOC"),
        "reu_type_reserved": read_define("REU_RESERVED"),
    }


def parse_tui_switch_contract(paths):
    for path in paths:
        if not os.path.exists(path):
            continue
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            src = f.read()

        max_bank_m = re.search(r"#define\s+APP_BANK_MAX\s+(\d+)", src)
        if not max_bank_m:
            continue

        return {
            "source_path": path,
            "app_bank_max": int(max_bank_m.group(1), 10),
            "uses_bitmap_lo": "SHIM_REU_BITMAP_LO" in src,
            "uses_bitmap_hi": "SHIM_REU_BITMAP_HI" in src,
            "has_bank_loaded_helper": "tui_bank_loaded" in src,
        }

    raise ValueError("APP_BANK_MAX definition not found in tui_nav.c or tui.c")


def parse_resume_contract(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()

    def read_hex_define(name):
        m = re.search(rf"#define\s+{name}\s+0x([0-9A-Fa-f]+)", src)
        if not m:
            raise ValueError(f"{name} definition not found in resume_state.h")
        return int(m.group(1), 16)

    return {
        "snapshot_size": read_hex_define("REU_APP_SNAPSHOT_SIZE"),
        "resume_off": read_hex_define("REU_RESUME_OFF"),
        "resume_tail_size": read_hex_define("REU_RESUME_TAIL_SIZE"),
    }


def parse_resume_hook_contract(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()

    def has_before(call_name, jump_name):
        pattern = rf"{call_name}\s*\(\s*\)\s*;\s*{jump_name}\s*\("
        return re.search(pattern, src, re.MULTILINE) is not None

    return {
        "includes_resume": '#include "../../lib/resume_state.h"' in src,
        "has_init": "resume_init_for_app(" in src,
        "has_try_load": ("resume_try_load(" in src) or ("resume_load_segments(" in src),
        "has_local_resume": ("TASKLIST_RESUME_OFF" in src and "resume_stash_segments(" in src and "resume_fetch_segments(" in src),
        "has_save_before_return": has_before("resume_save_state", "tui_return_to_launcher"),
        "has_save_before_switch": has_before("resume_save_state", "tui_switch_to_app"),
    }


def read_c1541_listing(disk_path):
    proc = subprocess.run(
        ["c1541", disk_path, "-list"],
        text=True,
        capture_output=True,
        check=False,
    )
    if proc.returncode != 0:
        raise ValueError(f"c1541 list failed for {disk_path}: {proc.stderr.strip()}")

    entries = {}
    line_re = re.compile(r'^\s*\d+\s+"([^"]+)"\s+([a-zA-Z]+)')
    for line in proc.stdout.splitlines():
        m = line_re.match(line)
        if not m:
            continue
        name = m.group(1).strip().lower()
        ftype = m.group(2).strip().lower()
        entries[name] = ftype
    return entries


def read_c1541_file_bytes(disk_path, spec):
    with tempfile.NamedTemporaryFile(prefix="readyos_verify_", suffix=".bin", delete=False) as tf:
        out_path = tf.name
    try:
        proc = subprocess.run(
            ["c1541", disk_path, "-read", spec, out_path],
            text=True,
            capture_output=True,
            check=False,
        )
        if proc.returncode != 0:
            raise ValueError(f"c1541 read failed for {disk_path}:{spec}: {proc.stderr.strip()}")
        with open(out_path, "rb") as f:
            return f.read()
    finally:
        try:
            os.remove(out_path)
        except OSError:
            pass


def parse_apps_catalog_bytes(raw):
    for idx, byte in enumerate(raw):
        if byte in (0x0D, 0x0A):
            continue
        if byte < 0x20 or byte > 0x7E:
            raise ValueError(f"invalid non-text byte in disk catalog: offset={idx} byte=0x{byte:02X}")
        if 0x61 <= byte <= 0x7A:
            raise ValueError(
                f"disk catalog is ASCII-lowercase at offset {idx} (0x{byte:02X}); "
                f"expected lowercase PETASCII text bytes"
            )

    def petscii_lower_to_ascii(byte):
        if 0x41 <= byte <= 0x5A:
            return chr(byte + 0x20)
        return chr(byte)

    text = "".join(petscii_lower_to_ascii(b) for b in raw)
    lines = []
    cur = []
    for ch in text:
        if ch == "\r" or ch == "\n":
            lines.append("".join(cur))
            cur = []
            continue
        cur.append(ch)
    if cur:
        lines.append("".join(cur))

    def clean(line):
        line = line.strip()
        if not line:
            return ""
        if line.startswith("#") or line.startswith(";"):
            return ""
        return line

    logical = [clean(line) for line in lines]
    logical = [line for line in logical if line]

    entries = []
    i = 0
    while i < len(logical):
        entry_line = logical[i]
        i += 1
        parts = [p.strip() for p in entry_line.split(":", 2)]
        if len(parts) != 3:
            raise ValueError(f"malformed disk catalog line: {entry_line!r}")
        drive_raw, prg, label = parts
        if not drive_raw.isdigit():
            raise ValueError(f"invalid drive token in disk catalog: {drive_raw!r}")
        drive = int(drive_raw, 10)
        prg_norm = normalize_catalog_prg_token(prg)
        if i >= len(logical):
            raise ValueError(f"missing description in disk catalog for: {entry_line!r}")
        desc = logical[i]
        i += 1
        entries.append({"drive": drive, "prg": prg_norm, "label": label, "desc": desc})

    return entries


def main():
    all_ok = True

    # --- Boot.prg ---
    print("=== boot.prg Structure ===")
    try:
        with open("boot.prg", "rb") as f:
            boot = f.read()
    except FileNotFoundError:
        print("  [FAIL] boot.prg not found. Run 'make' first.")
        return 1

    all_ok &= check("File size", 734 <= len(boot) <= 2050, f"{len(boot)} bytes (expect 734-2050)")
    boot_load_addr = struct.unpack_from("<H", boot, 0)[0]
    all_ok &= check("Load address", boot_load_addr == 0x0801, f"${boot_load_addr:04X} (expect $0801)")

    # Find shim_data by jump table pattern
    pattern = bytes([0x4C, 0x40, 0xC8, 0x4C, 0x60, 0xC8])
    offset = boot.find(pattern)
    all_ok &= check("Shim data found", offset >= 0, f"at file offset 0x{offset:04X}" if offset >= 0 else "NOT FOUND")
    if offset < 0:
        print("\nCannot continue without shim data.")
        return 1

    shim = boot[offset:]
    all_ok &= check("Shim size", len(shim) == 512, f"{len(shim)} bytes (expect 512)")
    all_ok &= check("Shim is last payload block", offset + 512 == len(boot),
                    f"shim_end=0x{offset + 512:04X}, file_len=0x{len(boot):04X}")

    boot_payload = len(boot) - 2
    boot_end = boot_load_addr + boot_payload - 1
    all_ok &= check("Boot image stays below app RAM", boot_end < APP_LOAD_START,
                    f"${boot_load_addr:04X}-${boot_end:04X} (app starts at ${APP_LOAD_START:04X})")

    # --- Jump Table ---
    print("\n=== Jump Table ($C800-$C817) ===")
    jt_checks = [
        (0x00, [0x4C, 0x40, 0xC8], "JMP $C840 (load_disk)"),
        (0x03, [0x4C, 0x60, 0xC8], "JMP $C860 (load_reu)"),
        (0x06, [0x4C, 0x00, 0x10], "JMP $1000 (run_app)"),
        (0x09, [0x4C, 0x80, 0xC8], "JMP $C880 (preload)"),
        (0x0C, [0x4C, 0x00, 0xC9], "JMP $C900 (return_to_launcher)"),
        (0x0F, [0x4C, 0x40, 0xC9], "JMP $C940 (switch_app)"),
        (0x12, [0x4C, 0xC0, 0xC8], "JMP $C8C0 (stash_current)"),
        (0x15, [0x4C, 0xF0, 0xC8], "JMP $C8F0 (fetch_bank)"),
    ]
    for off, expected, desc in jt_checks:
        actual = list(shim[off:off + 3])
        all_ok &= check(f"$C8{off:02X}", actual == expected,
                        f"{' '.join(f'{b:02X}' for b in actual)} = {desc}")

    # --- Data Area ---
    print("\n=== Data Area ($C820-$C83F) ===")
    all_ok &= check("target_bank", shim[0x20] == 0x00, f"${shim[0x20]:02X} (expect $00)")
    all_ok &= check("filename_len", shim[0x21] == 0x08, f"${shim[0x21]:02X} (expect $08)")
    all_ok &= check("current_bank", shim[0x34] == 0x00, f"${shim[0x34]:02X} (expect $00)")
    all_ok &= check("reu_bitmap_lo", shim[0x36] == 0x00, f"${shim[0x36]:02X} (expect $00)")
    all_ok &= check("reu_bitmap_hi", shim[0x37] == 0x00, f"${shim[0x37]:02X} (expect $00)")
    all_ok &= check("storage_drive", shim[0x39] == 0x08, f"${shim[0x39]:02X} (expect $08)")

    # --- Routine Alignment ---
    print("\n=== Routine Alignment ===")
    routines = [
        (0x040, 0xAD, "load_disk", "LDA abs"),
        (0x060, 0xAD, "load_reu", "LDA abs"),
        (0x080, 0xA9, "preload", "LDA imm"),
        (0x0E0, 0x20, "stash_to_bank", "JSR"),
        (0x0F0, 0x20, "fetch_bank", "JSR"),
        (0x100, 0xAD, "return_to_launcher", "LDA abs"),
        (0x140, 0xAD, "switch_app", "LDA abs"),
        (0x160, 0x8D, "debug_log_step", "STA abs"),
        (0x1A0, 0x8D, "reu_setup", "STA abs"),
        (0x1C0, 0xC9, "set_bitmap", "CMP imm"),
        (0x1E0, 0x48, "log_byte", "PHA"),
    ]
    for off, expected, name, desc in routines:
        actual = shim[off]
        addr = SHIM_START + off
        all_ok &= check(f"${addr:04X} {name}", actual == expected,
                        f"0x{actual:02X} (expect 0x{expected:02X} = {desc})")

    # --- LOAD End Address Save ---
    print("\n=== LOAD End Address Save ($C8AC) ===")
    load_end_save = list(shim[0xAC:0xAC + 6])
    all_ok &= check("$C8AC STX/STY", load_end_save == [0x8E, 0x30, 0xC8, 0x8C, 0x31, 0xC8],
                    f"{' '.join(f'{b:02X}' for b in load_end_save)} = STX $C830; STY $C831")

    # --- Critical Instruction Sequences ---
    print("\n=== Critical REU Sequences ===")
    reu = list(shim[0x1A0:0x1A0 + 3])
    all_ok &= check("$C9A0 reu_setup", reu == [0x8D, 0x06, 0xDF],
                    f"{' '.join(f'{b:02X}' for b in reu)} = STA $DF06")
    reu_len = list(shim[0x1A0 + 0x18:0x1A0 + 0x1D])
    all_ok &= check("$C9B5 transfer length", reu_len == [0xA9, 0xB6, 0x8D, 0x08, 0xDF],
                    f"{' '.join(f'{b:02X}' for b in reu_len)} = LDA #$B6; STA $DF08")

    stash = list(shim[0xE0:0xE0 + 3])
    all_ok &= check("$C8E0 stash→reu_setup", stash == [0x20, 0xA0, 0xC9],
                    f"{' '.join(f'{b:02X}' for b in stash)} = JSR $C9A0")

    fetch = list(shim[0xF0:0xF0 + 3])
    all_ok &= check("$C8F0 fetch→reu_setup", fetch == [0x20, 0xA0, 0xC9],
                    f"{' '.join(f'{b:02X}' for b in fetch)} = JSR $C9A0")

    stash_cmd = list(shim[0xE3:0xE3 + 5])
    all_ok &= check("$C8E3 STASH cmd", stash_cmd == [0xA9, 0x90, 0x8D, 0x01, 0xDF],
                    "LDA #$90; STA $DF01")

    fetch_cmd = list(shim[0xF3:0xF3 + 5])
    all_ok &= check("$C8F3 FETCH cmd", fetch_cmd == [0xA9, 0x91, 0x8D, 0x01, 0xDF],
                    "LDA #$91; STA $DF01")

    bitmap_guard = list(shim[0x1C0:0x1C0 + 4])
    all_ok &= check("$C9C0 bitmap bank guard", bitmap_guard == [0xC9, 0x10, 0xB0, 0x1B],
                    "CMP #$10; BCS done")
    bitmap_store = list(shim[0x1D9:0x1D9 + 6])
    all_ok &= check("$C9D9 bitmap indexed store", bitmap_store == [0x19, 0x36, 0xC8, 0x99, 0x36, 0xC8],
                    "ORA $C836,Y; STA $C836,Y")

    # --- Region Boundary Invariants ---
    print("\n=== Region Boundary Invariants ===")
    all_ok &= check("App snapshot before REU metadata", APP_SNAPSHOT_END < REU_META_START,
                    f"${APP_SNAPSHOT_END:04X} < ${REU_META_START:04X}")
    all_ok &= check("REU metadata before shim", REU_META_END < SHIM_START,
                    f"${REU_META_END:04X} < ${SHIM_START:04X}")
    all_ok &= check("Shim before I/O", SHIM_END < IO_START,
                    f"${SHIM_END:04X} < ${IO_START:04X}")

    # --- App Binaries ---
    print("\n=== App Binary Load Ranges ===")
    for app_name, prg in APP_PRGS:
        try:
            load, end, payload = parse_prg_range(prg)
            all_ok &= check(f"{prg} load", load == APP_LOAD_START,
                            f"${load:04X} (expect ${APP_LOAD_START:04X})")
            all_ok &= check(f"{prg} end<=snapshot", end <= APP_SNAPSHOT_END,
                            f"${load:04X}-${end:04X}, payload={payload} bytes")
            all_ok &= check(f"{prg} end<=linker", end <= APP_LINKER_END,
                            f"end=${end:04X}, linker_end=${APP_LINKER_END:04X}")
            file_headroom = APP_SNAPSHOT_END - end
            if file_headroom < APP_HEADROOM_WARN:
                warn(f"{prg} low file headroom", f"{file_headroom} bytes below ${APP_SNAPSHOT_END:04X}")
            if APP_HEADROOM_FAIL > 0:
                all_ok &= check(f"{prg} min headroom", file_headroom >= APP_HEADROOM_FAIL,
                                f"{file_headroom} bytes (threshold={APP_HEADROOM_FAIL})")
        except FileNotFoundError:
            all_ok &= check(prg, False, "NOT FOUND")
        except ValueError as ex:
            all_ok &= check(prg, False, str(ex))

    print("\n=== ReadyShell Overlay Payloads ===")
    for label, prg in READYSHELL_OVERLAY_PRGS:
        try:
            load, end, payload = parse_prg_range(prg)
            all_ok &= check(f"{prg} load>=app", load >= APP_LOAD_START,
                            f"${load:04X} (expect >= ${APP_LOAD_START:04X})")
            all_ok &= check(f"{prg} end<=snapshot", end <= APP_SNAPSHOT_END,
                            f"${load:04X}-${end:04X}, payload={payload} bytes")
        except FileNotFoundError:
            all_ok &= check(prg, False, "NOT FOUND")
        except ValueError as ex:
            all_ok &= check(prg, False, str(ex))

    # --- App Map Segment Bounds ---
    print("\n=== App Runtime Segment Bounds (Map Files) ===")
    for app_name, _prg in APP_PRGS:
        map_path = os.path.join("obj", f"{app_name}.map")
        if not os.path.exists(map_path):
            all_ok &= check(map_path, False, "map not found (build with Makefile map flags)")
            continue

        try:
            segs = parse_map_segments(map_path)
        except ValueError as ex:
            all_ok &= check(map_path, False, str(ex))
            continue

        load_ok = ("LOADADDR" in segs and segs["LOADADDR"][0] == 0x0FFE and segs["LOADADDR"][1] == 0x0FFF)
        all_ok &= check(f"{app_name}.map LOADADDR", load_ok,
                        f"{segs.get('LOADADDR', ('?', '?', '?'))}")

        runtime_segs = [(name, vals) for name, vals in segs.items() if name not in ("ZEROPAGE", "LOADADDR")]
        if not runtime_segs:
            all_ok &= check(f"{app_name}.map runtime segments", False, "no runtime segments found")
            continue

        runtime_start = min(vals[0] for _, vals in runtime_segs)
        runtime_end = max(vals[1] for _, vals in runtime_segs)

        all_ok &= check(f"{app_name}.map runtime start", runtime_start >= APP_LOAD_START,
                        f"start=${runtime_start:04X}")
        all_ok &= check(f"{app_name}.map runtime end<=snapshot", runtime_end <= APP_SNAPSHOT_END,
                        f"end=${runtime_end:04X}, snapshot_end=${APP_SNAPSHOT_END:04X}")
        all_ok &= check(f"{app_name}.map runtime end<=linker", runtime_end <= APP_LINKER_END,
                        f"end=${runtime_end:04X}, linker_end=${APP_LINKER_END:04X}")

        overlap_reu_meta = intersects(runtime_start, runtime_end, REU_META_START, REU_META_END)
        overlap_shim = intersects(runtime_start, runtime_end, SHIM_START, SHIM_END)
        overlap_io = intersects(runtime_start, runtime_end, IO_START, 0xFFFF)

        all_ok &= check(f"{app_name}.map no overlap REU metadata", not overlap_reu_meta,
                        f"${runtime_start:04X}-${runtime_end:04X} vs ${REU_META_START:04X}-${REU_META_END:04X}")
        all_ok &= check(f"{app_name}.map no overlap shim", not overlap_shim,
                        f"${runtime_start:04X}-${runtime_end:04X} vs ${SHIM_START:04X}-${SHIM_END:04X}")
        all_ok &= check(f"{app_name}.map no overlap I/O/ROM", not overlap_io,
                        f"${runtime_start:04X}-${runtime_end:04X} vs ${IO_START:04X}-$FFFF")
        runtime_headroom = APP_SNAPSHOT_END - runtime_end
        if runtime_headroom < APP_HEADROOM_WARN:
            warn(f"{app_name}.map low runtime headroom", f"{runtime_headroom} bytes below ${APP_SNAPSHOT_END:04X}")
        if APP_HEADROOM_FAIL > 0:
            all_ok &= check(f"{app_name}.map min runtime headroom", runtime_headroom >= APP_HEADROOM_FAIL,
                            f"{runtime_headroom} bytes (threshold={APP_HEADROOM_FAIL})")

    # --- Launcher Slot Contract ---
    print("\n=== Launcher Catalog Contract ===")
    slot_contract = None
    catalog_entries = None
    try:
        slot_contract = parse_launcher_runtime_contract(os.path.join("src", "apps", "launcher", "launcher.c"))
    except (FileNotFoundError, ValueError, AttributeError) as ex:
        all_ok &= check("launcher runtime contract parse", False, str(ex))
    else:
        all_ok &= check("MAX_APPS", slot_contract["max_apps"] == 16,
                        f'{slot_contract["max_apps"]} (expect 16)')
        all_ok &= check("MAX_FILE_LEN", slot_contract["max_file_len"] == 12,
                        f'{slot_contract["max_file_len"]} (expect 12)')
        all_ok &= check("DEFAULT_DRIVE", slot_contract["default_drive"] == 8,
                        f'{slot_contract["default_drive"]} (expect 8)')
        all_ok &= check("APP_CFG open spec", slot_contract["cfg_open_spec"] == "apps.cfg,s,r",
                        repr(slot_contract["cfg_open_spec"]))
        all_ok &= check("launcher has catalog parser", slot_contract["uses_cfg_parser"])
        all_ok &= check("launcher renders drive prefix", slot_contract["supports_drive_prefix"])
        norm_1 = None
        reject_comma = False
        reject_upper = False
        reject_prg_ext = False
        try:
            norm_1 = normalize_catalog_prg_token("editor")
            normalize_catalog_prg_token("editor,p")
        except ValueError:
            reject_comma = True
        try:
            normalize_catalog_prg_token("EDITOR")
        except ValueError:
            reject_upper = True
        try:
            normalize_catalog_prg_token("editor.prg")
        except ValueError:
            reject_prg_ext = True
        all_ok &= check("catalog accepts plain token", norm_1 == "editor", repr(norm_1))
        all_ok &= check("catalog rejects comma suffix", reject_comma)
        all_ok &= check("catalog rejects uppercase token", reject_upper)
        all_ok &= check("catalog rejects .prg extension", reject_prg_ext)

    try:
        catalog_entries = parse_apps_catalog(os.path.join("cfg", "apps_catalog.txt"))
    except (FileNotFoundError, ValueError) as ex:
        all_ok &= check("apps catalog parse", False, str(ex))
    else:
        drives = [e["drive"] for e in catalog_entries]
        prgs = [e["prg"] for e in catalog_entries]
        labels = [e["label"] for e in catalog_entries]
        descs = [e["desc"] for e in catalog_entries]
        all_ok &= check("catalog entries > 0", len(catalog_entries) > 0, f"{len(catalog_entries)} entries")
        all_ok &= check("catalog entries <= 15", len(catalog_entries) <= 15,
                        f"{len(catalog_entries)} entries")
        all_ok &= check("catalog drives in 8..11", all(8 <= d <= 11 for d in drives), f"{drives}")
        all_ok &= check("catalog prg names non-empty", all(0 < len(p) <= 12 for p in prgs), f"{prgs}")
        all_ok &= check("catalog prg names unique", len(set(prgs)) == len(prgs), f"{prgs}")
        all_ok &= check("catalog labels non-empty", all(len(l) > 0 for l in labels))
        all_ok &= check("catalog descriptions non-empty", all(len(d) > 0 for d in descs))
        all_ok &= check("catalog includes cal26 on drive 8",
                        any(e["prg"] == "cal26" and e["drive"] == 8 for e in catalog_entries))
        all_ok &= check("catalog includes dizzy on drive 8",
                        any(e["prg"] == "dizzy" and e["drive"] == 8 for e in catalog_entries))
        try:
            disk_catalog_raw = read_c1541_file_bytes(DISK_8, "apps.cfg,s")
            disk_catalog_entries = parse_apps_catalog_bytes(disk_catalog_raw)
        except ValueError as ex:
            all_ok &= check("disk8 apps.cfg read/parse", False, str(ex))
        else:
            all_ok &= check("disk8 apps.cfg bytes present", len(disk_catalog_raw) > 0, f"{len(disk_catalog_raw)} bytes")
            all_ok &= check("disk8 apps.cfg entry count", len(disk_catalog_entries) == len(catalog_entries),
                            f"disk={len(disk_catalog_entries)} src={len(catalog_entries)}")
            all_ok &= check("disk8 apps.cfg mirrors source",
                            disk_catalog_entries == catalog_entries)

    # --- Dual-disk content contract ---
    print("\n=== Dual-Disk Content Contract ===")
    try:
        disk8 = read_c1541_listing(DISK_8)
        disk9 = read_c1541_listing(DISK_9)
    except ValueError as ex:
        all_ok &= check("disk listing", False, str(ex))
        disk8 = {}
        disk9 = {}
    if disk8 and disk9:
        required_disk8 = {
            "preboot": "prg",
            "setd71": "prg",
            "showcfg": "prg",
            "boot": "prg",
            "launcher": "prg",
            "apps.cfg": "seq",
        }
        required_disk9 = {}
        if catalog_entries is not None:
            for entry in catalog_entries:
                if entry["drive"] == 8:
                    required_disk8[entry["prg"]] = "prg"
                elif entry["drive"] == 9:
                    required_disk9[entry["prg"]] = "prg"
        for name, ftype in required_disk8.items():
            all_ok &= check(f"disk8 has {name}", disk8.get(name) == ftype,
                            f"type={disk8.get(name)} expect={ftype}")
        for name, ftype in required_disk9.items():
            all_ok &= check(f"disk9 has {name}", disk9.get(name) == ftype,
                            f"type={disk9.get(name)} expect={ftype}")
        all_ok &= check("disk8 has rsovl1", disk8.get("rsovl1") == "prg",
                        f"type={disk8.get('rsovl1')} expect=prg")
        all_ok &= check("disk8 has rsovl2", disk8.get("rsovl2") == "prg",
                        f"type={disk8.get('rsovl2')} expect=prg")
        all_ok &= check("disk8 has rsovl3", disk8.get("rsovl3") == "prg",
                        f"type={disk8.get('rsovl3')} expect=prg")

    # --- Launcher shim patch contract ---
    print("\n=== Launcher Shim Patch Contract ===")
    try:
        with open(os.path.join("src", "apps", "launcher", "launcher.c"), "r", encoding="utf-8", errors="replace") as f:
            launcher_src = f.read()
    except FileNotFoundError:
        all_ok &= check("launcher source exists", False, "src/apps/launcher/launcher.c missing")
    else:
        all_ok &= check("launcher patches load_disk device", "SHIM_LOAD_DISK_DEV_IMM" in launcher_src)
        all_ok &= check("launcher patches preload device", "SHIM_PRELOAD_DEV_IMM" in launcher_src)

    print("\n=== App Switch Contract (F2/F4) ===")
    try:
        tui_contract = parse_tui_switch_contract([
            os.path.join("src", "lib", "tui_nav.c"),
            os.path.join("src", "lib", "tui.c"),
        ])
    except (FileNotFoundError, ValueError) as ex:
        all_ok &= check("tui switch contract parse", False, str(ex))
    else:
        all_ok &= check("tui switch contract source exists", os.path.exists(tui_contract["source_path"]),
                        tui_contract["source_path"])
        all_ok &= check("APP_BANK_MAX", tui_contract["app_bank_max"] == 15,
                        f'{tui_contract["app_bank_max"]} (expect 15)')
        all_ok &= check("tui checks bitmap low byte", tui_contract["uses_bitmap_lo"])
        all_ok &= check("tui checks bitmap high byte", tui_contract["uses_bitmap_hi"])
        all_ok &= check("tui has loaded-bank helper", tui_contract["has_bank_loaded_helper"])

    # --- REU Allocation Contract ---
    print("\n=== REU Allocation Contract ===")
    try:
        reu_contract = parse_reu_mgr_contract(os.path.join("src", "lib", "reu_mgr.h"))
    except (FileNotFoundError, ValueError) as ex:
        all_ok &= check("reu_mgr contract parse", False, str(ex))
    else:
        reu_total_banks = reu_contract["reu_total_banks"]
        reu_first_dynamic = reu_contract["reu_first_dynamic"]
        reu_type_reserved = reu_contract["reu_type_reserved"]

        all_ok &= check("REU_TOTAL_BANKS", reu_total_banks == 256,
                        f"{reu_total_banks} (expect 256)")
        all_ok &= check("REU_FIRST_DYNAMIC", reu_first_dynamic == 16,
                        f"{reu_first_dynamic} (expect 16)")
        all_ok &= check("REU_RESERVED type id", reu_type_reserved == 4,
                        f"{reu_type_reserved} (expect 4)")

        type_ids = [
            reu_contract["reu_type_free"],
            reu_contract["reu_type_app_state"],
            reu_contract["reu_type_clipboard"],
            reu_contract["reu_type_app_alloc"],
            reu_contract["reu_type_reserved"],
        ]
        all_ok &= check("bank type ids unique", len(set(type_ids)) == len(type_ids),
                        f"{type_ids}")

        if catalog_entries is not None:
            highest_used = len(catalog_entries)
            reserved_count = reu_first_dynamic - (highest_used + 1)
            all_ok &= check("apps fit before dynamic pool", highest_used < reu_first_dynamic,
                            f"highest={highest_used}, dynamic_base={reu_first_dynamic}")
            all_ok &= check("reserved app-slot headroom", reserved_count > 0,
                            f"{reserved_count} reserved slots ({highest_used + 1}-{reu_first_dynamic - 1})")

    # --- Warm Resume Contract ---
    print("\n=== Warm Resume Contract ===")
    try:
        resume_contract = parse_resume_contract(os.path.join("src", "lib", "resume_state.h"))
    except (FileNotFoundError, ValueError) as ex:
        all_ok &= check("resume header contract parse", False, str(ex))
    else:
        resume_end = resume_contract["resume_off"] + resume_contract["resume_tail_size"]
        all_ok &= check("resume snapshot size", resume_contract["snapshot_size"] == APP_SNAPSHOT_SIZE,
                        f'0x{resume_contract["snapshot_size"]:04X} (expect 0x{APP_SNAPSHOT_SIZE:04X})')
        all_ok &= check("resume offset equals snapshot size",
                        resume_contract["resume_off"] == resume_contract["snapshot_size"],
                        f'0x{resume_contract["resume_off"]:04X}')
        all_ok &= check("resume tail size", resume_contract["resume_tail_size"] == 0x4A00,
                        f'0x{resume_contract["resume_tail_size"]:04X} (expect 0x4A00)')
        all_ok &= check("resume region ends at bank boundary", resume_end == 0x10000,
                        f'0x{resume_end:05X} (expect 0x10000)')

    all_ok &= check("resume_state.c exists", os.path.exists(os.path.join("src", "lib", "resume_state.c")))

    try:
        with open("Makefile", "r", encoding="utf-8", errors="replace") as f:
            mk = f.read()
    except FileNotFoundError:
        all_ok &= check("Makefile exists", False, "missing")
        tasklist_links_resume = False
    else:
        resume_lib_vars = {
            "editor": "LIB_EDITOR",
            "calcplus": "LIB_CALCPLUS",
            "hexview": "LIB_HEXVIEW",
            "clipmgr": "LIB_CLIPMGR",
            "reuviewer": "LIB_REUVIEWER",
            "tasklist": "LIB_TASKLIST",
            "game2048": "LIB_GAME2048",
            "cal26": "LIB_CAL26",
            "dizzy": "LIB_DIZZY",
            "readme": "LIB_README",
            "readyshellpoc": "LIB_READYSHELL",
        }
        all_ok &= check("Makefile defines RESUME_STATE_SRC", "RESUME_STATE_SRC" in mk)
        tasklist_line = re.search(r"^LIB_TASKLIST\s*=.*$", mk, re.MULTILINE)
        tasklist_links_resume = tasklist_line is not None and "$(RESUME_STATE_SRC)" in tasklist_line.group(0)
        for app_name, var_name in resume_lib_vars.items():
            line = re.search(rf"^{var_name}\s*=.*$", mk, re.MULTILINE)
            if app_name == "tasklist":
                continue
            all_ok &= check(f"{var_name} links resume_state", line is not None and "$(RESUME_STATE_SRC)" in line.group(0))

    for app_name in ("editor", "calcplus", "hexview", "clipmgr", "reuviewer",
                     "tasklist", "game2048", "cal26", "dizzy", "readme",
                     "readyshellpoc"):
        path = os.path.join("src", "apps", app_name, f"{app_name}.c")
        try:
            hooks = parse_resume_hook_contract(path)
        except (FileNotFoundError, ValueError) as ex:
            all_ok &= check(f"{app_name} resume contract parse", False, str(ex))
            continue
        if app_name == "tasklist":
            all_ok &= check(
                f"{app_name} has warm-resume implementation",
                hooks["has_local_resume"] or
                (tasklist_links_resume and hooks["includes_resume"] and hooks["has_init"] and hooks["has_try_load"])
            )
        else:
            all_ok &= check(f"{app_name} includes resume header", hooks["includes_resume"])
            all_ok &= check(f"{app_name} initializes resume module", hooks["has_init"])
            all_ok &= check(f"{app_name} tries resume load", hooks["has_try_load"])
        all_ok &= check(f"{app_name} saves before launcher return", hooks["has_save_before_return"])
        all_ok &= check(f"{app_name} saves before app switch", hooks["has_save_before_switch"])

    # --- Summary ---
    print()
    if all_ok:
        print("ALL CHECKS PASSED")
        return 0

    print("SOME CHECKS FAILED")
    return 1


if __name__ == "__main__":
    sys.exit(main())
