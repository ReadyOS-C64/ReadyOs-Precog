#!/usr/bin/env python3
"""
verify_memory_map.py

Hard memory-map contract checks for ReadyOS:
- app/runtime RAM windows
- shim/REU-reserved boundary discipline
- cc65 ONCE/BSS disjointness for warm-resumed apps
- ReadyShell overlay window + overlay fit
- REU and resume constants
- fixed-address literal allowlist discipline
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC_PATH = ROOT / "build_support" / "memory_map_spec.json"


def parse_int(value):
    if isinstance(value, int):
        return value
    return int(str(value), 0)


def check(name, cond, detail=""):
    status = "OK" if cond else "FAIL"
    msg = f"[{status}] {name}"
    if detail:
        msg += f" - {detail}"
    print(msg)
    return cond


def intersects(a_start, a_end, b_start, b_end):
    return not (a_end < b_start or b_end < a_start)


def fmt_range(start, end):
    return f"${start:04X}..${end:04X}"


def parse_map_segments(map_path: Path):
    txt = map_path.read_text(encoding="utf-8", errors="replace")
    lines = txt.splitlines()
    seg_re = re.compile(
        r"^\s*([A-Z0-9_]+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+[0-9A-F]{5}\s*$"
    )

    segments = {}
    in_seg = False
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
        raise ValueError(f"segment list missing: {map_path}")
    return txt, segments


def parse_map_symbol(txt, symbol):
    m = re.search(
        rf"(?<!\S){re.escape(symbol)}\s+([0-9A-F]{{6}})\s+(?:REA|RLA|RLZ)\b",
        txt,
    )
    if not m:
        raise ValueError(f"symbol not found: {symbol}")
    return int(m.group(1), 16)


def parse_define(path: Path, name: str):
    src = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(rf"#define\s+{re.escape(name)}\s+([0-9xXa-fA-F]+)[uUlL]*\b", src)
    if not m:
        raise ValueError(f"{name} missing in {path}")
    return int(m.group(1), 0)


def parse_ready_app_window():
    cfg = (ROOT / "cfg" / "ready_app.cfg").read_text(encoding="utf-8", errors="replace")
    m = re.search(
        r"MAIN:\s*file\s*=\s*%O,\s*start\s*=\s*\$([0-9A-Fa-f]+),\s*size\s*=\s*\$([0-9A-Fa-f]+)",
        cfg,
    )
    if not m:
        raise ValueError("MAIN range missing in cfg/ready_app.cfg")
    start = int(m.group(1), 16)
    size = int(m.group(2), 16)
    return start, start + size - 1, size


def parse_overlay_himem_default():
    cfg = (ROOT / "cfg" / "ready_app_overlay.cfg").read_text(
        encoding="utf-8", errors="replace"
    )
    m = re.search(
        r"__HIMEM__:\s*type\s*=\s*weak,\s*value\s*=\s*\$([0-9A-Fa-f]+)",
        cfg,
    )
    if not m:
        raise ValueError("__HIMEM__ default missing in cfg/ready_app_overlay.cfg")
    return int(m.group(1), 16)


def parse_make_overlay_profiles():
    mk = (ROOT / "Makefile").read_text(encoding="utf-8", errors="replace")
    m_mode = re.search(
        r"^READYSHELL_PARSE_TRACE_DEBUG\s*\?=\s*([01])\s*$",
        mk,
        flags=re.MULTILINE,
    )
    if not m_mode:
        raise ValueError("READYSHELL_PARSE_TRACE_DEBUG default missing in Makefile")

    m_ovl = re.search(
        r"^READYSHELL_OVERLAYSIZE\s*\?=\s*"
        r"\$\(if\s+\$\(filter\s+1,\$\(READYSHELL_PARSE_TRACE_DEBUG\)\),"
        r"\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\)\s*$",
        mk,
        flags=re.MULTILINE,
    )
    if not m_ovl:
        raise ValueError("READYSHELL_OVERLAYSIZE conditional missing in Makefile")

    default_mode = int(m_mode.group(1), 0)
    debug_size = int(m_ovl.group(1), 0)
    release_size = int(m_ovl.group(2), 0)
    default_size = debug_size if default_mode == 1 else release_size
    return {
        "default_mode": default_mode,
        "default_size": default_size,
        "release_size": release_size,
        "debug_size": debug_size,
    }


def collect_fixed_addresses():
    pat = re.compile(r"\b0x([0-9A-Fa-f]{4})\b")
    out = set()
    for path in (ROOT / "src").rglob("*"):
        if path.suffix not in (".c", ".h", ".s"):
            continue
        txt = path.read_text(encoding="utf-8", errors="replace")
        for m in pat.finditer(txt):
            addr = int(m.group(0), 16)
            if 0xC000 <= addr <= 0xDFFF:
                out.add((path.relative_to(ROOT).as_posix(), addr))
    return out


def main():
    if not SPEC_PATH.exists():
        print(f"[FAIL] missing spec: {SPEC_PATH}")
        return 1

    spec = json.loads(SPEC_PATH.read_text(encoding="utf-8"))
    ok = True

    app_start = parse_int(spec["ram_windows"]["app_runtime"]["start"])
    app_end = parse_int(spec["ram_windows"]["app_runtime"]["end"])
    reu_meta_start = parse_int(spec["ram_windows"]["reu_metadata"]["start"])
    reu_meta_end = parse_int(spec["ram_windows"]["reu_metadata"]["end"])
    shim_start = parse_int(spec["ram_windows"]["shim"]["start"])
    shim_end = parse_int(spec["ram_windows"]["shim"]["end"])
    io_start = parse_int(spec["ram_windows"]["io"]["start"])
    io_end = parse_int(spec["ram_windows"]["io"]["end"])
    overlay_himem_expected = parse_int(spec["readyshell_overlay"]["himem"])
    overlay_profiles = spec["readyshell_overlay"]["overlay_size_profiles"]
    overlay_release_expected = parse_int(overlay_profiles["release"])
    overlay_debug_expected = parse_int(overlay_profiles["debug"])
    overlay_size_allow = {overlay_release_expected, overlay_debug_expected}
    overlay_default_mode_expected = int(spec["readyshell_overlay"]["default_parse_trace_debug"])

    print("=== Core Window Contract ===")
    try:
        cfg_start, cfg_end, cfg_size = parse_ready_app_window()
        ok &= check("cfg app start", cfg_start == app_start, hex(cfg_start))
        ok &= check("cfg app end", cfg_end == app_end, hex(cfg_end))
        ok &= check("cfg app size", cfg_size == (app_end - app_start + 1), hex(cfg_size))
    except ValueError as ex:
        ok &= check("ready_app.cfg parse", False, str(ex))

    try:
        himem_cfg = parse_overlay_himem_default()
        ok &= check(
            "overlay __HIMEM__ default",
            himem_cfg == overlay_himem_expected,
            hex(himem_cfg),
        )
    except ValueError as ex:
        ok &= check("ready_app_overlay.cfg parse", False, str(ex))

    try:
        make_overlay = parse_make_overlay_profiles()
        ok &= check(
            "Makefile default READYSHELL_PARSE_TRACE_DEBUG",
            make_overlay["default_mode"] == overlay_default_mode_expected,
            str(make_overlay["default_mode"]),
        )
        ok &= check(
            "Makefile READYSHELL_OVERLAYSIZE (release)",
            make_overlay["release_size"] == overlay_release_expected,
            hex(make_overlay["release_size"]),
        )
        ok &= check(
            "Makefile READYSHELL_OVERLAYSIZE (debug)",
            make_overlay["debug_size"] == overlay_debug_expected,
            hex(make_overlay["debug_size"]),
        )
        default_size_expected = (
            overlay_debug_expected
            if make_overlay["default_mode"] == 1
            else overlay_release_expected
        )
        ok &= check(
            "Makefile default READYSHELL_OVERLAYSIZE",
            make_overlay["default_size"] == default_size_expected,
            hex(make_overlay["default_size"]),
        )
    except ValueError as ex:
        ok &= check("Makefile overlay profile parse", False, str(ex))

    print("\n=== Linker Map Bounds ===")
    for rel in spec["map_files"]:
        map_path = ROOT / rel
        if not map_path.exists():
            ok &= check(f"{rel} exists", False, "run make all first")
            continue
        try:
            txt, segs = parse_map_segments(map_path)
        except ValueError as ex:
            ok &= check(f"{rel} parse", False, str(ex))
            continue

        for seg_name in spec["main_segments"]:
            if seg_name not in segs:
                continue
            start, end, _size = segs[seg_name]
            ok &= check(
                f"{rel}:{seg_name} inside app window",
                start >= app_start and end <= app_end,
                f"{hex(start)}..{hex(end)}",
            )
            ok &= check(
                f"{rel}:{seg_name} avoids REU meta",
                not intersects(start, end, reu_meta_start, reu_meta_end),
                f"{hex(start)}..{hex(end)} vs {hex(reu_meta_start)}..{hex(reu_meta_end)}",
            )
            ok &= check(
                f"{rel}:{seg_name} avoids shim",
                not intersects(start, end, shim_start, shim_end),
                f"{hex(start)}..{hex(end)} vs {hex(shim_start)}..{hex(shim_end)}",
            )
            ok &= check(
                f"{rel}:{seg_name} avoids I/O",
                not intersects(start, end, io_start, io_end),
                f"{hex(start)}..{hex(end)} vs {hex(io_start)}..{hex(io_end)}",
            )

        if "ONCE" in segs and "BSS" in segs:
            once_start, once_end, _once_size = segs["ONCE"]
            bss_start, bss_end, _bss_size = segs["BSS"]
            once_range = fmt_range(once_start, once_end)
            bss_range = fmt_range(bss_start, bss_end)
            ok &= check(
                f"{rel}:ONCE/BSS disjoint",
                not intersects(once_start, once_end, bss_start, bss_end),
                f"ONCE={once_range} BSS={bss_range}",
            )
            ok &= check(
                f"{rel}:BSS after ONCE",
                bss_start > once_end,
                f"ONCE={once_range} BSS={bss_range}",
            )

        if rel.endswith("readyshell.map"):
            try:
                overlay_start = parse_map_symbol(txt, "__OVERLAYSTART__")
                overlay_loadaddr = parse_map_symbol(txt, "__OVERLAY_LOADADDR__")
                himem = parse_map_symbol(txt, "__HIMEM__")
                rs_src = ROOT / "src" / "apps" / "readyshellpoc" / "readyshellpoc.c"
                rs_vars_h = ROOT / "src" / "apps" / "readyshellpoc" / "core" / "rs_vars.h"
                rs_runtime_addr = parse_define(rs_src, "RS_RUNTIME_ADDR")
                rs_runtime_limit = parse_define(rs_src, "RS_RUNTIME_LIMIT_ADDR")
                rs_vars_max = parse_define(rs_vars_h, "RS_VARS_MAX")
            except ValueError as ex:
                ok &= check("readyshell overlay symbols", False, str(ex))
                continue

            overlay_window = himem - overlay_start
            ok &= check("readyshell himem", himem == overlay_himem_expected, hex(himem))
            ok &= check(
                "readyshell overlay window size (profile)",
                overlay_window in overlay_size_allow,
                f"{hex(overlay_window)} allowed={','.join(hex(v) for v in sorted(overlay_size_allow))}",
            )
            ok &= check(
                "readyshell runtime high-RAM base",
                rs_runtime_addr >= shim_end + 1 and rs_runtime_addr < io_start,
                f"{fmt_range(rs_runtime_addr, rs_runtime_limit - 1)}",
            )
            ok &= check(
                "readyshell runtime high-RAM limit",
                rs_runtime_limit <= io_start,
                f"{fmt_range(rs_runtime_addr, rs_runtime_limit - 1)} vs I/O starts ${io_start:04X}",
            )
            ok &= check(
                "readyshell C64 variable slots",
                rs_vars_max >= 24,
                f"RS_VARS_MAX={rs_vars_max}",
            )
            if "BSS" in segs:
                bss_start, bss_end, _bss_size = segs["BSS"]
                rs_heap_addr = bss_end + 1
                if rs_heap_addr & 1:
                    rs_heap_addr += 1
                rs_heap_end = overlay_loadaddr - 1
                ok &= check(
                    "readyshell heap inside app window",
                    rs_heap_addr >= app_start and rs_heap_end <= app_end and rs_heap_addr <= rs_heap_end,
                    f"{fmt_range(rs_heap_addr, rs_heap_end)}",
                )
                ok &= check(
                    "readyshell heap below overlay load address",
                    rs_heap_end < overlay_loadaddr,
                    f"heap={fmt_range(rs_heap_addr, rs_heap_end)} overlay_load=${overlay_loadaddr:04X}",
                )
                ok &= check(
                    "readyshell BSS below overlay load address",
                    bss_end < overlay_loadaddr,
                    f"BSS ends ${bss_end:04X}; overlay load address starts ${overlay_loadaddr:04X}",
                )
                ok &= check(
                    "readyshell BSS below heap",
                    bss_end < rs_heap_addr,
                    f"BSS ends ${bss_end:04X}; heap starts ${rs_heap_addr:04X}",
                )
            for ovl_addr_name in ("OVL1ADDR", "OVL2ADDR", "OVL3ADDR",
                                  "OVL4ADDR", "OVL5ADDR", "OVL6ADDR", "OVL7ADDR"):
                if ovl_addr_name not in segs:
                    ok &= check(f"readyshell:{ovl_addr_name} exists", False)
                    continue
                start, end, size = segs[ovl_addr_name]
                ok &= check(
                    f"readyshell:{ovl_addr_name} size",
                    size == 2,
                    f"{fmt_range(start, end)} size=${size:04X}",
                )
                ok &= check(
                    f"readyshell:{ovl_addr_name} range",
                    start == overlay_loadaddr and end == overlay_start - 1,
                    f"{fmt_range(start, end)} expected {fmt_range(overlay_loadaddr, overlay_start - 1)}",
                )
            for ovl_name in ("OVERLAY1", "OVERLAY2", "OVERLAY3",
                             "OVERLAY4", "OVERLAY5", "OVERLAY6", "OVERLAY7"):
                if ovl_name not in segs:
                    ok &= check(f"readyshell:{ovl_name} exists", False)
                    continue
                start, end, _size = segs[ovl_name]
                ok &= check(f"readyshell:{ovl_name} start", start == overlay_start, hex(start))
                ok &= check(f"readyshell:{ovl_name} end < himem", end < himem, hex(end))

    print("\n=== REU/Resume Constants ===")
    try:
        reu_total = parse_define(ROOT / "src" / "lib" / "reu_mgr.h", "REU_TOTAL_BANKS")
        reu_first = parse_define(ROOT / "src" / "lib" / "reu_mgr.h", "REU_FIRST_DYNAMIC")
        reu_rs_ovl1 = parse_define(ROOT / "src" / "lib" / "reu_mgr.h", "REU_BANK_RS_OVL1")
        reu_rs_ovl2 = parse_define(ROOT / "src" / "lib" / "reu_mgr.h", "REU_BANK_RS_OVL2")
        reu_rs_ovl3 = parse_define(ROOT / "src" / "lib" / "reu_mgr.h", "REU_BANK_RS_OVL3")
        reu_rs_debug = parse_define(ROOT / "src" / "lib" / "reu_mgr.h", "REU_BANK_RS_DEBUG")
        ok &= check("REU total banks", reu_total == int(spec["reu_contract"]["total_banks"]), str(reu_total))
        ok &= check("REU first dynamic", reu_first == int(spec["reu_contract"]["first_dynamic"]), str(reu_first))
        ok &= check("REU bank RS ovl1", reu_rs_ovl1 == int(spec["reu_contract"]["bank_rs_ovl1"]), str(reu_rs_ovl1))
        ok &= check("REU bank RS ovl2", reu_rs_ovl2 == int(spec["reu_contract"]["bank_rs_ovl2"]), str(reu_rs_ovl2))
        ok &= check("REU bank RS ovl3", reu_rs_ovl3 == int(spec["reu_contract"]["bank_rs_ovl3"]), str(reu_rs_ovl3))
        ok &= check("REU bank RS debug", reu_rs_debug == int(spec["reu_contract"]["bank_rs_debug"]), str(reu_rs_debug))
    except ValueError as ex:
        ok &= check("reu_mgr.h constants", False, str(ex))

    try:
        bank_system = parse_define(ROOT / "src" / "shim" / "reu.h", "REU_BANK_SYSTEM")
        bank_clip = parse_define(ROOT / "src" / "shim" / "reu.h", "REU_BANK_CLIPBOARD")
        bank_base = parse_define(ROOT / "src" / "shim" / "reu.h", "REU_BANK_APP_BASE")
        bank_count = parse_define(ROOT / "src" / "shim" / "reu.h", "REU_BANK_APP_COUNT")
        ok &= check("shim REU bank system", bank_system == int(spec["reu_contract"]["bank_system"]), str(bank_system))
        ok &= check("shim REU bank clipboard", bank_clip == int(spec["reu_contract"]["bank_clipboard"]), str(bank_clip))
        ok &= check("shim REU bank app base", bank_base == int(spec["reu_contract"]["bank_app_base"]), str(bank_base))
        ok &= check("shim REU bank app count", bank_count == int(spec["reu_contract"]["bank_app_count"]), str(bank_count))
    except ValueError as ex:
        ok &= check("shim/reu.h constants", False, str(ex))

    try:
        snapshot = parse_define(ROOT / "src" / "lib" / "resume_state.h", "REU_APP_SNAPSHOT_SIZE")
        resume_off = parse_define(ROOT / "src" / "lib" / "resume_state.h", "REU_RESUME_OFF")
        resume_tail = parse_define(ROOT / "src" / "lib" / "resume_state.h", "REU_RESUME_TAIL_SIZE")
        ok &= check("resume snapshot size", snapshot == parse_int(spec["resume_contract"]["snapshot_size"]), hex(snapshot))
        ok &= check("resume offset", resume_off == parse_int(spec["resume_contract"]["resume_offset"]), hex(resume_off))
        ok &= check("resume tail size", resume_tail == parse_int(spec["resume_contract"]["resume_tail"]), hex(resume_tail))
        ok &= check("resume tail reaches 64KB", resume_off + resume_tail == 0x10000, hex(resume_off + resume_tail))
    except ValueError as ex:
        ok &= check("resume_state.h constants", False, str(ex))

    print("\n=== Fixed Address Discipline ===")
    allow = {int(v, 16) for v in spec["fixed_address_allowlist"]}
    found = collect_fixed_addresses()
    unexpected = sorted([(p, a) for (p, a) in found if a not in allow], key=lambda x: (x[1], x[0]))
    ok &= check("fixed-address literals documented", len(unexpected) == 0, f"unexpected={len(unexpected)}")
    if unexpected:
        for path, addr in unexpected[:64]:
            print(f"  [FAIL] undocumented fixed address {hex(addr)} in {path}")
        if len(unexpected) > 64:
            print(f"  [FAIL] ... and {len(unexpected) - 64} more")

    print()
    if ok:
        print("MEMORY MAP VERIFICATION PASSED")
        return 0
    print("MEMORY MAP VERIFICATION FAILED")
    return 1


if __name__ == "__main__":
    sys.exit(main())
