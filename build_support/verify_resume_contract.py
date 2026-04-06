#!/usr/bin/env python3
"""
verify_resume_contract.py

Static warm-resume contract checks:
- resume tail constants
- app wiring (all switchable apps)
- shim snapshot window expectation in boot_asm source
"""

import os
import re
import sys


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def check(name, cond, detail=""):
    status = "OK" if cond else "FAIL"
    msg = f"[{status}] {name}"
    if detail:
        msg += f" - {detail}"
    print(msg)
    return cond


def parse_hex_define(src, name):
    m = re.search(rf"#define\s+{name}\s+0x([0-9A-Fa-f]+)", src)
    if not m:
        raise ValueError(f"missing define {name}")
    return int(m.group(1), 16)


def parse_resume_header():
    path = os.path.join(ROOT, "src", "lib", "resume_state.h")
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()
    return {
        "path": path,
        "snapshot": parse_hex_define(src, "REU_APP_SNAPSHOT_SIZE"),
        "offset": parse_hex_define(src, "REU_RESUME_OFF"),
        "tail": parse_hex_define(src, "REU_RESUME_TAIL_SIZE"),
    }


def has_before(src, first, second):
    pattern = rf"{first}\s*\(\s*\)\s*;\s*{second}\s*\("
    return re.search(pattern, src, re.MULTILINE) is not None


def parse_app_hooks(app_name):
    path = os.path.join(ROOT, "src", "apps", app_name, f"{app_name}.c")
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()
    return {
        "path": path,
        "include": '#include "../../lib/resume_state.h"' in src,
        "init": "resume_init_for_app(" in src,
        "load": ("resume_try_load(" in src) or ("resume_load_segments(" in src),
        "local": ("TASKLIST_RESUME_OFF" in src and "resume_stash_segments(" in src and "resume_fetch_segments(" in src),
        "save_return": has_before(src, "resume_save_state", "tui_return_to_launcher"),
        "save_switch": has_before(src, "resume_save_state", "tui_switch_to_app"),
        "snapshot_overlay_reuse": "if (rs_overlay_active())" in src,
        "schema_v1": "RESUME_SCHEMA_V1" in src and "READYSHELL_RESUME_SCHEMA" not in src,
    }


def parse_boot_snapshot_contract():
    path = os.path.join(ROOT, "src", "boot", "boot_asm.s")
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()

    has_len_lo = "STA $DF07" in src
    has_len_hi_b6 = "LDA #$B6" in src and "STA $DF08" in src
    return path, has_len_lo, has_len_hi_b6


def main():
    os.chdir(ROOT)
    all_ok = True

    print("=== Resume Tail Constants ===")
    try:
        hdr = parse_resume_header()
    except (FileNotFoundError, ValueError) as ex:
        all_ok &= check("resume_state.h parse", False, str(ex))
    else:
        resume_end = hdr["offset"] + hdr["tail"]
        all_ok &= check("snapshot size is $B600", hdr["snapshot"] == 0xB600, hex(hdr["snapshot"]))
        all_ok &= check("offset equals snapshot", hdr["offset"] == hdr["snapshot"], hex(hdr["offset"]))
        all_ok &= check("tail size is $4A00", hdr["tail"] == 0x4A00, hex(hdr["tail"]))
        all_ok &= check("tail reaches 64KB boundary", resume_end == 0x10000, hex(resume_end))

    print("\n=== App Hooks ===")
    for app_name in ("editor", "calcplus", "hexview", "clipmgr", "reuviewer",
                     "tasklist", "simplefiles", "game2048", "deminer", "cal26", "dizzy", "readme",
                     "readyshellpoc"):
        try:
            hooks = parse_app_hooks(app_name)
        except (FileNotFoundError, ValueError) as ex:
            all_ok &= check(f"{app_name} parse", False, str(ex))
            continue
        if app_name == "tasklist":
            all_ok &= check(f"{app_name} has warm-resume implementation",
                            hooks["local"] or (hooks["include"] and hooks["init"] and hooks["load"]))
        else:
            all_ok &= check(f"{app_name} includes resume header", hooks["include"])
            all_ok &= check(f"{app_name} init hook", hooks["init"])
            all_ok &= check(f"{app_name} load hook", hooks["load"])
        if app_name == "readyshellpoc":
            all_ok &= check(f"{app_name} snapshot overlay reuse", hooks["snapshot_overlay_reuse"])
            all_ok &= check(f"{app_name} schema v1 constant", hooks["schema_v1"])
        all_ok &= check(f"{app_name} save before launcher return", hooks["save_return"])
        all_ok &= check(f"{app_name} save before app switch", hooks["save_switch"])

    print("\n=== Shim Snapshot Contract ===")
    try:
        path, has_len_lo, has_len_hi_b6 = parse_boot_snapshot_contract()
    except FileNotFoundError as ex:
        all_ok &= check("boot_asm.s exists", False, str(ex))
    else:
        all_ok &= check("boot_asm sets REU length low", has_len_lo, path)
        all_ok &= check("boot_asm sets REU length high to $B6", has_len_hi_b6, path)

    print()
    if all_ok:
        print("RESUME CONTRACT CHECKS PASSED")
        return 0
    print("RESUME CONTRACT CHECKS FAILED")
    return 1


if __name__ == "__main__":
    sys.exit(main())
