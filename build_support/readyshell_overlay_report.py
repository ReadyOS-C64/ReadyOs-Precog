#!/usr/bin/env python3
"""
Artifact-backed ReadyShell overlay inventory report.
"""

from __future__ import annotations

import argparse
import html
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

from readyos_profiles import resolve_profile


ROOT = Path(__file__).resolve().parents[1]


@dataclass
class OverlaySpec:
    number: int
    title: str
    purpose: str
    build_prg: str
    disk_staging_prg: str
    disk_name: str
    command_summary: str
    command_list: tuple[str, ...]
    reu_policy: str
    ram_notes: str


OVERLAY_SPECS: dict[int, OverlaySpec] = {
    1: OverlaySpec(
        1,
        "Parser / Lexer",
        "Lexer, parser, AST construction, and parse cleanup.",
        "rsparser.prg",
        "obj/rsparser.prg",
        "rsparser",
        "None directly; parse phase support.",
        (),
        "Boot-loaded from disk, then cached in the shared core-overlay REU bank.",
        "Lives entirely inside the shared overlay window while active.",
    ),
    2: OverlaySpec(
        2,
        "Execution Core",
        "Values, variables, formatting, pipes, and shared execution helpers.",
        "rsvm.prg",
        "obj/rsvm.prg",
        "rsvm",
        "PRT, MORE, TOP, SEL, GEN, TAP and the shared execution paths that command overlays return to.",
        ("PRT", "MORE", "TOP", "SEL", "GEN", "TAP"),
        "Boot-loaded from disk, then cached in the shared core-overlay REU bank.",
        "Includes rs_vm_fmt_buf[128] and rs_vm_line_buf[384] inside the overlay image.",
    ),
    3: OverlaySpec(
        3,
        "Drive Info + Directory Listing",
        "Shared command overlay for DRVI and LST.",
        "rsdrvilst.prg",
        "obj/rsdrvilst.prg",
        "rsdrvilst",
        "DRVI, LST",
        ("DRVI", "LST"),
        "Loaded from disk on demand for each command call. No dedicated REU cache.",
        "Shares the inter-command REU handoff area at 0x480000-0x487FFF.",
    ),
    4: OverlaySpec(
        4,
        "Load Value",
        "Single-command overlay for LDV.",
        "rsldv.prg",
        "obj/rsldv.prg",
        "rsldv",
        "LDV",
        ("LDV",),
        "Loaded from disk on demand for each command call. No dedicated REU cache.",
        "Uses the shared handoff region plus the REU-backed value arena in bank 0x48 when hydrating pointer-backed values.",
    ),
    5: OverlaySpec(
        5,
        "Store Value",
        "Single-command overlay for STV.",
        "rsstv.prg",
        "obj/rsstv.prg",
        "rsstv",
        "STV",
        ("STV",),
        "Loaded from disk on demand for each command call. No dedicated REU cache.",
        "Uses the shared handoff region plus the REU-backed value arena in bank 0x48 when serializing pointer-backed values.",
    ),
    6: OverlaySpec(
        6,
        "File Delete / Rename / Write",
        "Shared command overlay for DEL, REN, PUT, and ADD.",
        "rsfops.prg",
        "obj/rsfops.prg",
        "rsfops",
        "DEL, REN, PUT, ADD",
        ("DEL", "REN", "PUT", "ADD"),
        "Loaded from disk on demand for each command call. No dedicated REU cache.",
        "Keeps file-operation staging and transient command state in overlay-local code plus the shared REU scratch region.",
    ),
    7: OverlaySpec(
        7,
        "File Read / Copy",
        "Shared command overlay for CAT and COPY.",
        "rscat.prg",
        "obj/rscat.prg",
        "rscat",
        "CAT, COPY",
        ("CAT", "COPY"),
        "Loaded from disk on demand for each command call. No dedicated REU cache.",
        "Uses overlay-local file I/O logic plus shared REU scratch when line staging or copy state is needed.",
    ),
}

RESIDENT_COMMANDS = "Resident app shell loop plus vm/overlay runtime. Command tokens resolved here, then dispatched to overlay 2 or command overlays."


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def parse_define(text: str, name: str) -> int:
    match = re.search(
        rf"#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+|[0-9]+)(?:u|ul|U|UL|l|L)?",
        text,
    )
    if not match:
        raise ValueError(f"missing define {name}")
    return int(match.group(1), 0)


def parse_map_segments(map_text: str) -> dict[str, tuple[int, int, int]]:
    segments: dict[str, tuple[int, int, int]] = {}
    in_list = False
    for line in map_text.splitlines():
        if line.strip() == "Segment list:":
            in_list = True
            continue
        if not in_list:
            continue
        match = re.match(
            r"^\s*([A-Z0-9_]+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+[0-9A-F]{5}\s*$",
            line,
        )
        if match:
            segments[match.group(1)] = (
                int(match.group(2), 16),
                int(match.group(3), 16),
                int(match.group(4), 16),
            )
            continue
        if segments and not line.strip():
            break
    if not segments:
        raise ValueError("segment list missing from map")
    return segments


def parse_map_symbol(map_text: str, name: str) -> int:
    match = re.search(rf"(?:^|\s){re.escape(name)}\s+([0-9A-F]{{6}})\s", map_text, re.MULTILINE)
    if not match:
        raise ValueError(f"missing map symbol {name}")
    return int(match.group(1), 16)


def parse_build_version(text: str) -> str:
    match = re.search(r'#define\s+READYOS_BOOT_VERSION_TEXT\s+"([^"]+)"', text)
    return match.group(1) if match else "unknown"


def parse_makefile_block(makefile_text: str, var_name: str) -> list[str]:
    lines = makefile_text.splitlines()
    capture = False
    items: list[str] = []
    for line in lines:
        if not capture:
            if re.match(rf"^{re.escape(var_name)}\s*=\s*\\\s*$", line):
                capture = True
            elif re.match(rf"^{re.escape(var_name)}\s*=\s*(.+)$", line):
                value = line.split("=", 1)[1].strip()
                if value:
                    items.append(value.rstrip("\\").strip())
                break
            continue
        stripped = line.strip()
        if not stripped:
            break
        items.append(stripped.rstrip("\\").strip())
        if not line.rstrip().endswith("\\"):
            break
    return [item for item in items if item]


def parse_makefile_scalar(makefile_text: str, var_name: str) -> str | None:
    for line in makefile_text.splitlines():
        match = re.match(rf"^{re.escape(var_name)}\s*(?:\?|)?=\s*(.+)$", line)
        if match:
            return match.group(1).strip()
    return None


def expand_make_vars(raw: str) -> str:
    replacements = {
        "$(LIB_DIR)": "src/lib",
        "$(READYSHELL_DIR)": "src/apps/readyshellpoc",
        "$(READYSHELL_CORE_DIR)": "src/apps/readyshellpoc/core",
        "$(READYSHELL_PLATFORM_DIR)": "src/apps/readyshellpoc/platform",
        "$(READYSHELL_PLATFORM_C64_DIR)": "src/apps/readyshellpoc/platform/c64",
        "$(OBJ_DIR)": "obj",
    }
    out = raw
    for src, dst in replacements.items():
        out = out.replace(src, dst)
    return out


def resolve_make_tokens(token: str, scalar_map: dict[str, str]) -> list[str]:
    match = re.fullmatch(r"\$\(([^)]+)\)", token)
    if not match:
        return [expand_make_vars(token)]
    name = match.group(1)
    value = scalar_map.get(name)
    if not value:
        return [expand_make_vars(token)]
    resolved: list[str] = []
    for part in value.split():
        resolved.extend(resolve_make_tokens(part, scalar_map))
    return resolved


def parse_disk_listing(disk_path: Path) -> tuple[str, dict[str, int], int]:
    proc = subprocess.run(
        ["c1541", str(disk_path), "-list"],
        capture_output=True,
        text=True,
        check=True,
    )
    listing = proc.stdout
    header_name = "unknown"
    blocks: dict[str, int] = {}
    free_blocks = -1
    for line in listing.splitlines():
        header_match = re.match(r'^\s*0\s+"([^"]+)"\s+([A-Za-z0-9 ]+)\s+[A-Za-z0-9]+$', line)
        if header_match:
            header_name = header_match.group(1).rstrip()
            continue
        file_match = re.match(r'^\s*(\d+)\s+"([^"]+)"\s+([a-z]+)', line)
        if file_match:
            blocks[file_match.group(2).strip().lower()] = int(file_match.group(1))
            continue
        free_match = re.match(r"^\s*(\d+)\s+blocks free\.", line, re.IGNORECASE)
        if free_match:
            free_blocks = int(free_match.group(1))
    return header_name, blocks, free_blocks


def fmt_hex(value: int) -> str:
    return f"${value:04X}"


def fmt_hex24(value: int) -> str:
    return f"${value:06X}"


def fmt_pct(value: float) -> str:
    return f"{value:.1f}%"


def fmt_range(start: int, end: int) -> str:
    return f"{fmt_hex(start)}-{fmt_hex(end)}"


def fmt_overlay_nums(items: list[dict[str, object]]) -> str:
    nums = [int(item["num"]) for item in items]
    if not nums:
        return "none"
    if len(nums) == 1:
        return str(nums[0])
    ranges: list[str] = []
    start = nums[0]
    prev = nums[0]
    for num in nums[1:]:
        if num == prev + 1:
            prev = num
            continue
        ranges.append(f"{start}-{prev}" if start != prev else str(start))
        start = prev = num
    ranges.append(f"{start}-{prev}" if start != prev else str(start))
    return ", ".join(ranges)


def disk_image_label(path: str) -> str:
    suffix = Path(path).suffix.upper().lstrip(".")
    return f"{suffix} disk image" if suffix else "disk image"


def resolve_default_disk_path(root: Path, profile_id: str) -> Path:
    manifest = resolve_profile(profile_id, None, latest=True)
    disks = manifest.get("disks", [])
    if not disks:
        raise ValueError(f"manifest for profile {profile_id} has no disks")
    preferred = None
    for disk in disks:
        if int(disk.get("drive", 0)) == 8:
            preferred = disk
            break
    if preferred is None:
        preferred = disks[0]
    disk_path = Path(str(preferred["path"]))
    if not disk_path.is_absolute():
        disk_path = root / disk_path
    return disk_path.resolve()


def short_sources(items: list[str]) -> str:
    return ", ".join(Path(item).name for item in items)


def command_chips(commands: tuple[str, ...]) -> str:
    if not commands:
        return "none"
    return " | ".join(commands)


def overlay_kind_label(row: dict[str, object]) -> str:
    spec = row["spec"]
    if row["num"] == 2:
        return "shared execution core"
    if len(spec.command_list) > 1:
        return "shared command overlay"
    if len(spec.command_list) == 1:
        return "single-command overlay"
    return "support overlay"


def command_topology_block(ctx: dict[str, object]) -> list[str]:
    rows: list[str] = ["Resident ReadyShell dispatcher", "  |"]
    command_rows = [row for row in ctx["overlays"] if row["spec"].command_list]
    for idx, row in enumerate(command_rows):
        spec = row["spec"]
        connector = "`--" if idx == len(command_rows) - 1 else "+--"
        branch_indent = "       " if idx == len(command_rows) - 1 else "|      "
        rows.append(
            f"  {connector} Overlay {row['num']}  {spec.disk_name:<10} [{overlay_kind_label(row)}]"
        )
        rows.append(f"  {branch_indent}commands: {command_chips(spec.command_list)}")
        if row["num"] == 2:
            rows.append(f"  {branch_indent}note: shared execution paths that command overlays return to")
        if row["num"] != 2 and len(spec.command_list) > 1:
            rows.append(f"  {branch_indent}note: multiple commands share one disk sidecar and one RAM image")
        if idx != len(command_rows) - 1:
            rows.append("  |")
    return rows


def build_report_context(args: argparse.Namespace) -> dict[str, object]:
    makefile_text = read_text(args.makefile)
    map_text = read_text(args.map)
    build_version_text = read_text(args.build_version)
    scalar_map = {
        name: value
        for name in (
            "LIB_DIR",
            "TUI_NAV_SRC",
            "REU_DMA_SRC",
            "RESUME_STATE_CTX_SRC",
            "RESUME_STATE_CORE_SRC",
            "RESUME_STATE_SIMPLE_SRCS",
        )
        if (value := parse_makefile_scalar(makefile_text, name))
    }
    overlay_source_map = {
        num: [expand_make_vars(item) for item in parse_makefile_block(makefile_text, f"READYSHELL_OVERLAY{num}_SRCS")]
        for num in OVERLAY_SPECS
    }
    resident_sources: list[str] = []
    for item in parse_makefile_block(makefile_text, "READYSHELL_RESIDENT_SRCS"):
        resident_sources.extend(resolve_make_tokens(item, scalar_map))
    resident_asm_sources = [expand_make_vars(item) for item in parse_makefile_block(makefile_text, "READYSHELL_RESIDENT_ASM_SRCS")]

    segments = parse_map_segments(map_text)
    himem = parse_map_symbol(map_text, "__HIMEM__")
    overlay_start = parse_map_symbol(map_text, "__OVERLAYSTART__")
    overlay_loadaddr = parse_map_symbol(map_text, "__OVERLAY_LOADADDR__")
    window_size = himem - overlay_start
    bss_start, bss_end, bss_size = segments["BSS"]
    heap_start = bss_end + 1
    if heap_start & 1:
        heap_start += 1
    heap_end = overlay_loadaddr - 1
    heap_size = overlay_loadaddr - heap_start

    cmd_overlay_h = read_text(args.cmd_overlay_header)
    value_c = read_text(args.value_c)
    overlay_c = read_text(args.overlay_c)
    shell_c = read_text(args.shell_c)
    ui_state_h = read_text(args.ui_state_h)

    scratch_off = parse_define(cmd_overlay_h, "RS_CMD_SCRATCH_OFF")
    scratch_len = parse_define(cmd_overlay_h, "RS_CMD_SCRATCH_LEN")
    heap_bank_base = parse_define(value_c, "RS_REU_BANK_BASE_OFF")
    heap_meta_rel = parse_define(value_c, "RS_REU_HEAP_META_REL")
    heap_arena_rel = parse_define(value_c, "RS_REU_HEAP_ARENA_REL")
    heap_arena_end_rel = parse_define(value_c, "RS_REU_HEAP_ARENA_END")
    runtime_addr = parse_define(shell_c, "RS_RUNTIME_ADDR")
    runtime_limit = parse_define(shell_c, "RS_RUNTIME_LIMIT_ADDR")
    dbg_head_off = parse_define(overlay_c, "RS_REU_DBG_HEAD_OFF")
    dbg_data_off = parse_define(overlay_c, "RS_REU_DBG_DATA_OFF")
    dbg_data_len = parse_define(overlay_c, "RS_REU_DBG_DATA_LEN")
    shared_meta_off = parse_define(ui_state_h, "RS_REU_SHARED_META_OFF")
    cmd_reg_hdr_off = parse_define(ui_state_h, "RS_REU_CMD_REG_HDR_OFF")
    cmd_reg_hdr_len = parse_define(ui_state_h, "RS_REU_CMD_REG_HDR_LEN")
    cmd_reg_desc_off = parse_define(ui_state_h, "RS_REU_CMD_REG_DESC_OFF")
    cmd_reg_desc_len = parse_define(ui_state_h, "RS_REU_CMD_REG_DESC_LEN")
    cmd_reg_desc_cap = parse_define(ui_state_h, "RS_REU_CMD_REG_DESC_CAP")
    cmd_reg_state_off = parse_define(ui_state_h, "RS_REU_CMD_REG_STATE_OFF")
    cmd_reg_state_len = parse_define(ui_state_h, "RS_REU_CMD_REG_STATE_LEN")
    cmd_reg_state_cap = parse_define(ui_state_h, "RS_REU_CMD_REG_STATE_CAP")
    ovl_meta_off = shared_meta_off
    ovl_meta_len = parse_define(ui_state_h, "RS_REU_OVL_CACHE_META_LEN")
    ovl_cache_bank = parse_define(ui_state_h, "RS_REU_OVL_CACHE_BANK")
    ovl_parse_rel = parse_define(ui_state_h, "RS_REU_OVL_CACHE_PARSE_REL")
    ovl_exec_rel = parse_define(ui_state_h, "RS_REU_OVL_CACHE_EXEC_REL")
    ovl_slot_len = parse_define(ui_state_h, "RS_REU_OVL_CACHE_SLOT_LEN")
    ui_flags_off = parse_define(ui_state_h, "RS_REU_UI_FLAGS_OFF")
    ovl_cache_base = parse_define(overlay_c, "RS_REU_OVL_CACHE_BASE")

    code_start, code_end, code_size = segments["CODE"]
    ro_start, ro_end, ro_size = segments["RODATA"]
    data_start, data_end, data_size = segments["DATA"]
    init_start, init_end, init_size = segments["INIT"]
    once_start, once_end, once_size = segments["ONCE"]

    bank_size = 0x10000
    parse_abs = ovl_cache_base + ovl_parse_rel
    exec_abs = ovl_cache_base + ovl_exec_rel
    cache_tail_start = exec_abs + ovl_slot_len
    cache_tail_size = bank_size - (ovl_exec_rel + ovl_slot_len)

    disk_label, disk_blocks, free_blocks = parse_disk_listing(args.disk)

    resident_prg_size = args.readyshell_prg.stat().st_size
    resident_disk_blocks = disk_blocks.get("readyshell", 0)
    overlays: list[dict[str, object]] = []
    for num, spec in OVERLAY_SPECS.items():
        prg_path = args.root / spec.disk_staging_prg
        file_size = prg_path.stat().st_size
        live_start, live_end, live_size = segments[f"OVERLAY{num}"]
        window_free = window_size - live_size
        reu_off = None
        reu_slot_end = None
        if num == 1:
            reu_off = parse_abs
            reu_slot_end = parse_abs + ovl_slot_len - 1
        elif num == 2:
            reu_off = exec_abs
            reu_slot_end = exec_abs + ovl_slot_len - 1
        overlays.append(
            {
                "num": num,
                "spec": spec,
                "sources": overlay_source_map[num],
                "file_size": file_size,
                "disk_blocks": disk_blocks.get(spec.disk_name, 0),
                "live_start": live_start,
                "live_end": live_end,
                "live_size": live_size,
                "window_free": window_free,
                "window_pct": (live_size / window_size) * 100.0,
                "reu_off": reu_off,
                "reu_slot_end": reu_slot_end,
            }
        )

    cached_overlays = [row for row in overlays if row["reu_off"] is not None]
    demand_overlays = [row for row in overlays if row["reu_off"] is None]

    return {
        "profile": args.profile,
        "version": parse_build_version(build_version_text),
        "disk_path": str(args.disk.relative_to(args.root)),
        "disk_image_label": disk_image_label(str(args.disk)),
        "disk_label": disk_label,
        "disk_free_blocks": free_blocks,
        "resident_prg_size": resident_prg_size,
        "resident_disk_blocks": resident_disk_blocks,
        "resident_sources": resident_sources,
        "resident_asm_sources": resident_asm_sources,
        "himem": himem,
        "overlay_start": overlay_start,
        "overlay_loadaddr": overlay_loadaddr,
        "window_size": window_size,
        "bss_start": bss_start,
        "bss_end": bss_end,
        "bss_size": bss_size,
        "heap_start": heap_start,
        "heap_end": heap_end,
        "heap_size": heap_size,
        "runtime_addr": runtime_addr,
        "runtime_limit": runtime_limit,
        "code_size": code_size,
        "rodata_size": ro_size,
        "data_size": data_size,
        "init_size": init_size,
        "once_size": once_size,
        "scratch_off": scratch_off,
        "scratch_len": scratch_len,
        "dbg_head_off": dbg_head_off,
        "dbg_data_off": dbg_data_off,
        "dbg_data_len": dbg_data_len,
        "dbg_end_off": dbg_data_off + dbg_data_len - 1,
        "dbg_span_len": dbg_data_off + dbg_data_len - dbg_head_off,
        "heap_bank_base": heap_bank_base,
        "shared_meta_off": shared_meta_off,
        "cmd_reg_hdr_off": cmd_reg_hdr_off,
        "cmd_reg_hdr_len": cmd_reg_hdr_len,
        "cmd_reg_desc_off": cmd_reg_desc_off,
        "cmd_reg_desc_len": cmd_reg_desc_len,
        "cmd_reg_desc_cap": cmd_reg_desc_cap,
        "cmd_reg_state_off": cmd_reg_state_off,
        "cmd_reg_state_len": cmd_reg_state_len,
        "cmd_reg_state_cap": cmd_reg_state_cap,
        "ovl_meta_off": ovl_meta_off,
        "ovl_meta_len": ovl_meta_len,
        "ovl_cache_bank": ovl_cache_bank,
        "ovl_parse_rel": ovl_parse_rel,
        "ovl_exec_rel": ovl_exec_rel,
        "ovl_slot_len": ovl_slot_len,
        "ovl_cache_base": ovl_cache_base,
        "ovl_parse_abs": parse_abs,
        "ovl_exec_abs": exec_abs,
        "cache_tail_start": cache_tail_start,
        "cache_tail_size": cache_tail_size,
        "ui_flags_off": ui_flags_off,
        "heap_meta_abs": heap_bank_base + heap_meta_rel,
        "heap_arena_abs": heap_bank_base + heap_arena_rel,
        "heap_arena_end_abs": heap_bank_base + heap_arena_end_rel - 1,
        "heap_arena_size": heap_arena_end_rel - heap_arena_rel,
        "overlays": overlays,
        "cached_overlays": cached_overlays,
        "demand_overlays": demand_overlays,
    }


def render_markdown(ctx: dict[str, object]) -> str:
    overlay_rows = []
    for row in ctx["overlays"]:
        spec = row["spec"]
        reu_loc = (
            f"bank `0x{ctx['ovl_cache_bank']:02X}` slot `{fmt_hex24(row['reu_off'])}-{fmt_hex24(row['reu_slot_end'])}`"
            if row["reu_off"] is not None
            else "disk-only"
        )
        overlay_rows.append(
            f"| {row['num']} | {spec.title} | `{spec.build_prg}` | `{spec.disk_name}` | "
            f"`{row['file_size']}` | `{row['disk_blocks']}` | `{row['live_size']}` | `{fmt_pct(row['window_pct'])}` | "
            f"{reu_loc} | {spec.command_summary} |"
        )

    details = []
    for row in ctx["overlays"]:
        spec = row["spec"]
        reu_line = (
            f"Cached in shared bank `0x{ctx['ovl_cache_bank']:02X}`, slot `{fmt_hex24(row['reu_off'])}-{fmt_hex24(row['reu_slot_end'])}`, as a full `{ctx['ovl_slot_len']:#06x}`-byte overlay-window snapshot."
            if row["reu_off"] is not None
            else "Not cached in a dedicated REU overlay bank; loaded from disk on demand."
        )
        details.append(
            "\n".join(
                [
                    f"### Overlay {row['num']}: {spec.title}",
                    "",
                    f"- Purpose: {spec.purpose}",
                    f"- Build PRG: `{spec.build_prg}`",
                    f"- Disk staging PRG: `{spec.disk_staging_prg}`",
                    f"- Disk filename: `{spec.disk_name}`",
                    f"- Source files: `{short_sources(row['sources'])}`",
                    f"- Commands: {spec.command_summary}",
                    f"- Runtime bytes in overlay window: `{row['live_size']}` at `{fmt_range(row['live_start'], row['live_end'])}`",
                    f"- Window share: `{fmt_pct(row['window_pct'])}` used, `{row['window_free']}` bytes free",
                    f"- Disk footprint: `{row['file_size']}` bytes, `{row['disk_blocks']}` D71 blocks",
                    f"- REU policy: {reu_line}",
                    f"- RAM notes: {spec.ram_notes}",
                ]
            )
        )

    reu_rows = []
    reu_rows.extend(
        [
            f"| Shared cache bank | `{fmt_hex24(ctx['ovl_cache_base'])}-{fmt_hex24(ctx['ovl_cache_base'] + 0xFFFF)}` | `65536` | One bank reserved for both core overlays and their free tail. |",
            f"| Overlay 1 parse slot | `{fmt_hex24(ctx['ovl_parse_abs'])}-{fmt_hex24(ctx['ovl_parse_abs'] + ctx['ovl_slot_len'] - 1)}` | `{ctx['ovl_slot_len']}` | Full overlay-window snapshot for overlay 1. |",
            f"| Overlay 2 exec slot | `{fmt_hex24(ctx['ovl_exec_abs'])}-{fmt_hex24(ctx['ovl_exec_abs'] + ctx['ovl_slot_len'] - 1)}` | `{ctx['ovl_slot_len']}` | Full overlay-window snapshot for overlay 2. |",
            f"| Shared bank free tail | `{fmt_hex24(ctx['cache_tail_start'])}-{fmt_hex24(ctx['ovl_cache_base'] + 0xFFFF)}` | `{ctx['cache_tail_size']}` | Currently unused tail after the two core overlay slots. |",
            f"| Debug trace ring | `{fmt_hex24(ctx['dbg_head_off'])}-{fmt_hex24(ctx['dbg_end_off'])}` | `{ctx['dbg_span_len']}` | Overlay debug markers and verification state. |",
            f"| Command scratch | `{fmt_hex24(ctx['scratch_off'])}-{fmt_hex24(ctx['scratch_off'] + ctx['scratch_len'] - 1)}` | `{ctx['scratch_len']}` | Inter-overlay handoff area for command frames and streaming state. |",
            f"| Command registry header | `{fmt_hex24(ctx['cmd_reg_hdr_off'])}-{fmt_hex24(ctx['cmd_reg_hdr_off'] + ctx['cmd_reg_hdr_len'] - 1)}` | `{ctx['cmd_reg_hdr_len']}` | REU-backed external-command registry header. |",
            f"| Command descriptor table | `{fmt_hex24(ctx['cmd_reg_desc_off'])}-{fmt_hex24(ctx['cmd_reg_desc_off'] + (ctx['cmd_reg_desc_len'] * ctx['cmd_reg_desc_cap']) - 1)}` | `{ctx['cmd_reg_desc_len'] * ctx['cmd_reg_desc_cap']}` | Fixed-capacity external-command descriptor table in REU metadata. |",
            f"| Overlay state table | `{fmt_hex24(ctx['cmd_reg_state_off'])}-{fmt_hex24(ctx['cmd_reg_state_off'] + (ctx['cmd_reg_state_len'] * ctx['cmd_reg_state_cap']) - 1)}` | `{ctx['cmd_reg_state_len'] * ctx['cmd_reg_state_cap']}` | Fixed-capacity overlay load/cache state table for external command overlays. |",
            f"| Shared ReadyShell metadata | `{fmt_hex24(ctx['ovl_meta_off'])}-{fmt_hex24(ctx['ovl_meta_off'] + ctx['ovl_meta_len'] - 1)}` | `{ctx['ovl_meta_len']}` | Shared core-overlay cache metadata record. |",
            f"| Pause flag | `{fmt_hex24(ctx['ui_flags_off'])}` | `1` | Shared output-pause bit used by resident output and `MORE`. |",
            f"| REU heap metadata | `{fmt_hex24(ctx['heap_meta_abs'])}-{fmt_hex24(ctx['heap_meta_abs'] + 0xFF)}` | `256` | ReadyShell REU heap header region, including shared metadata bytes. |",
            f"| REU heap arena | `{fmt_hex24(ctx['heap_arena_abs'])}-{fmt_hex24(ctx['heap_arena_end_abs'])}` | `{ctx['heap_arena_size']}` | Persistent value payload arena for REU-backed strings/arrays/objects. |",
        ]
    )

    return "\n".join(
        [
            f"# ReadyShell Overlay Inventory Report ({ctx['version']})",
            "",
            f"Artifact-backed report generated from the current local ReadyShell build, linker map, and {ctx['disk_image_label']}.",
            "",
            "## Executive Summary",
            "",
            f"- Profile / disk source: `{ctx['profile']}` using `{ctx['disk_path']}` (disk label `{ctx['disk_label']}`, `{ctx['disk_free_blocks']}` blocks free).",
            f"- Resident ReadyShell PRG: `readyshell.prg` on disk as `readyshell`, `{ctx['resident_prg_size']}` bytes and `{ctx['resident_disk_blocks']}` D71 blocks.",
            f"- Overlay execution window: `{fmt_range(ctx['overlay_start'], ctx['himem'] - 1)}` for `{ctx['window_size']}` bytes, with PRG load-address bytes at `{fmt_range(ctx['overlay_loadaddr'], ctx['overlay_start'] - 1)}`.",
            f"- Resident BSS / heap below overlays: BSS `{fmt_range(ctx['bss_start'], ctx['bss_end'])}` (`{ctx['bss_size']}` bytes), heap `{fmt_range(ctx['heap_start'], ctx['heap_end'])}` (`{ctx['heap_size']}` bytes).",
            f"- High RAM runtime region outside the app window: `{fmt_range(ctx['runtime_addr'], ctx['runtime_limit'] - 1)}`.",
            "- REU policy split:",
            f"  - overlays {fmt_overlay_nums(ctx['cached_overlays'])} are boot-loaded from disk and cached into one shared REU bank using fixed full-window slots",
            f"  - overlays {fmt_overlay_nums(ctx['demand_overlays'])} are loaded from disk on demand for each command call",
            "  - bank 0x48 is shared for the external-command registry, overlay metadata, pause state, command handoff scratch, and the REU-backed ReadyShell value arena",
            "",
            "## Runtime Memory Map",
            "",
            "| Region | Range | Size | Notes |",
            "| --- | --- | ---: | --- |",
            f"| Resident app window | `$1000-$C5FF` | `{0xB600}` | ReadyOS app-owned RAM window for ReadyShell. |",
            f"| Overlay load address bytes | `{fmt_range(ctx['overlay_loadaddr'], ctx['overlay_start'] - 1)}` | `2` | PRG load address emitted ahead of each overlay sidecar file. |",
            f"| Overlay execution window | `{fmt_range(ctx['overlay_start'], ctx['himem'] - 1)}` | `{ctx['window_size']}` | Shared live area for whichever overlay is active. |",
            f"| Resident BSS | `{fmt_range(ctx['bss_start'], ctx['bss_end'])}` | `{ctx['bss_size']}` | Resident writable data below the overlay load address. |",
            f"| Resident heap | `{fmt_range(ctx['heap_start'], ctx['heap_end'])}` | `{ctx['heap_size']}` | cc65 heap carved below the overlay load address. |",
            f"| High-RAM runtime | `{fmt_range(ctx['runtime_addr'], ctx['runtime_limit'] - 1)}` | `{ctx['runtime_limit'] - ctx['runtime_addr']}` | Fixed ReadyShell runtime state outside the app snapshot window. |",
            "",
            "## REU Layout And Loading Model",
            "",
            "| Use | REU range | Size | How it is used |",
            "| --- | --- | ---: | --- |",
            *reu_rows,
            "",
            "## Shared Core Overlay Cache Visual",
            "",
            "```text",
            f"REU bank 0x{ctx['ovl_cache_bank']:02X}",
            "",
            f"+----------------------------------------+ {fmt_hex24(ctx['ovl_parse_abs'])}",
            "| overlay 1 parse slot                   |",
            f"| full overlay-window image: {ctx['ovl_slot_len']:#06x}      |",
            "| active file: rsparser.prg              |",
            f"+----------------------------------------+ {fmt_hex24(ctx['ovl_exec_abs'])}",
            "| overlay 2 exec slot                    |",
            f"| full overlay-window image: {ctx['ovl_slot_len']:#06x}      |",
            "| active file: rsvm.prg                  |",
            f"+----------------------------------------+ {fmt_hex24(ctx['cache_tail_start'])}",
            "| free tail                              |",
            f"| {ctx['cache_tail_size']:#06x} bytes                           |",
            f"+----------------------------------------+ {fmt_hex24(ctx['ovl_cache_base'] + 0xFFFF)}",
            "```",
            "",
            "## Overlay Inventory",
            "",
            "| Ovl | Role | Build PRG | Disk name | PRG bytes | Disk blocks | Live bytes | Window use | REU cache | Commands |",
            "| ---: | --- | --- | --- | ---: | ---: | ---: | ---: | --- | --- |",
            *overlay_rows,
            "",
            "## Command Topology",
            "",
            "```text",
            *command_topology_block(ctx),
            "```",
            "",
            "- `DRVI` and `LST` now co-reside in `rsdrvilst`, so both commands load the same disk sidecar and the same overlay image.",
            f"- Overlays {fmt_overlay_nums(ctx['demand_overlays'])} remain disk-loaded command overlays; only overlays {fmt_overlay_nums(ctx['cached_overlays'])} are REU-cached.",
            "",
            "## Resident Program",
            "",
            f"- Build PRG: `readyshell.prg`",
            f"- Disk filename: `readyshell`",
            f"- Disk staging comes from the main ReadyShell build artifact, not an overlay copy.",
            f"- Resident sources: `{short_sources(ctx['resident_sources'])}`",
            f"- Resident asm/runtime support: `{short_sources(ctx['resident_asm_sources'])}`",
            f"- Command role: {RESIDENT_COMMANDS}",
            "- Current linker-visible resident footprint:",
            f"  - `CODE` `0x{ctx['code_size']:04X}`",
            f"  - `RODATA` `0x{ctx['rodata_size']:04X}`",
            f"  - `DATA` `0x{ctx['data_size']:04X}`",
            f"  - `INIT` `0x{ctx['init_size']:04X}`",
            f"  - `ONCE` `0x{ctx['once_size']:04X}`",
            f"  - `BSS` `0x{ctx['bss_size']:04X}`",
            "",
            "## Per-Overlay Details",
            "",
            "\n\n".join(details),
            "",
            "## Observations",
            "",
            f"- Overlay 2 is effectively full: `{ctx['overlays'][1]['live_size']}` of `{ctx['window_size']}` bytes (`{fmt_pct(ctx['overlays'][1]['window_pct'])}`).",
            f"- Overlay 1 is also large at `{ctx['overlays'][0]['live_size']}` bytes (`{fmt_pct(ctx['overlays'][0]['window_pct'])}`).",
            f"- The resident heap below the overlay load address is only `{ctx['heap_size']}` bytes, so large transient work must lean on overlays and REU-backed storage.",
            f"- Overlays {fmt_overlay_nums(ctx['cached_overlays'])} no longer consume separate REU banks; they share bank `0x{ctx['ovl_cache_bank']:02X}` and leave `{ctx['cache_tail_size']}` bytes free at the tail of that bank.",
            f"- Command overlays {fmt_overlay_nums(ctx['demand_overlays'])} stay smaller on disk and in RAM, but they pay the disk-load cost per command because they are not REU-cached today.",
            "- Overlay 2 carries the shared formatting buffers, so its footprint reflects both command support code and the text-rendering scratch it owns.",
        ]
    )


def render_html(ctx: dict[str, object]) -> str:
    overlay_rows = []
    detail_cards = []
    for row in ctx["overlays"]:
        spec = row["spec"]
        reu_html = (
            f'bank <code>0x{ctx["ovl_cache_bank"]:02X}</code> slot <code>{fmt_hex24(row["reu_off"])}-{fmt_hex24(row["reu_slot_end"])}</code>'
            if row["reu_off"] is not None
            else "disk only"
        )
        overlay_rows.append(
            "<tr>"
            f"<td>{row['num']}</td>"
            f"<td>{html.escape(spec.title)}</td>"
            f"<td><code>{html.escape(spec.build_prg)}</code></td>"
            f"<td><code>{html.escape(spec.disk_name)}</code></td>"
            f"<td class='num'>{row['file_size']}</td>"
            f"<td class='num'>{row['disk_blocks']}</td>"
            f"<td class='num'>{row['live_size']}</td>"
            f"<td class='num'>{fmt_pct(row['window_pct'])}</td>"
            f"<td>{html.escape(spec.command_summary)}</td>"
            f"<td>{reu_html}</td>"
            "</tr>"
        )
        detail_cards.append(
            "<section class='card'>"
            f"<h3>Overlay {row['num']}: {html.escape(spec.title)}</h3>"
            f"<p>{html.escape(spec.purpose)}</p>"
            "<ul>"
            f"<li><strong>Build PRG:</strong> <code>{html.escape(spec.build_prg)}</code></li>"
            f"<li><strong>Disk staging PRG:</strong> <code>{html.escape(spec.disk_staging_prg)}</code></li>"
            f"<li><strong>Disk filename:</strong> <code>{html.escape(spec.disk_name)}</code></li>"
            f"<li><strong>Source files:</strong> <code>{html.escape(short_sources(row['sources']))}</code></li>"
            f"<li><strong>Commands:</strong> {html.escape(spec.command_summary)}</li>"
            f"<li><strong>Live window range:</strong> <code>{fmt_range(row['live_start'], row['live_end'])}</code> ({row['live_size']} bytes)</li>"
            f"<li><strong>Window share:</strong> {fmt_pct(row['window_pct'])} used, {row['window_free']} bytes free</li>"
            f"<li><strong>Disk footprint:</strong> {row['file_size']} bytes, {row['disk_blocks']} D71 blocks</li>"
            f"<li><strong>REU policy:</strong> {html.escape(spec.reu_policy)}</li>"
            f"<li><strong>RAM notes:</strong> {html.escape(spec.ram_notes)}</li>"
            "</ul>"
            "</section>"
        )

    reu_items = [
        f"<li><strong>Shared cache bank:</strong> <code>{fmt_hex24(ctx['ovl_cache_base'])}-{fmt_hex24(ctx['ovl_cache_base'] + 0xFFFF)}</code> (65536 bytes)</li>",
        f"<li><strong>Overlay 1 parse slot:</strong> <code>{fmt_hex24(ctx['ovl_parse_abs'])}-{fmt_hex24(ctx['ovl_parse_abs'] + ctx['ovl_slot_len'] - 1)}</code> ({ctx['ovl_slot_len']} bytes)</li>",
        f"<li><strong>Overlay 2 exec slot:</strong> <code>{fmt_hex24(ctx['ovl_exec_abs'])}-{fmt_hex24(ctx['ovl_exec_abs'] + ctx['ovl_slot_len'] - 1)}</code> ({ctx['ovl_slot_len']} bytes)</li>",
        f"<li><strong>Shared bank free tail:</strong> <code>{fmt_hex24(ctx['cache_tail_start'])}-{fmt_hex24(ctx['ovl_cache_base'] + 0xFFFF)}</code> ({ctx['cache_tail_size']} bytes)</li>",
    ]
    reu_items.extend(
        [
            f"<li><strong>Debug trace ring:</strong> <code>{fmt_hex24(ctx['dbg_head_off'])}-{fmt_hex24(ctx['dbg_end_off'])}</code> ({ctx['dbg_span_len']} bytes)</li>",
            f"<li><strong>Command scratch:</strong> <code>{fmt_hex24(ctx['scratch_off'])}-{fmt_hex24(ctx['scratch_off'] + ctx['scratch_len'] - 1)}</code> ({ctx['scratch_len']} bytes)</li>",
            f"<li><strong>Command registry header:</strong> <code>{fmt_hex24(ctx['cmd_reg_hdr_off'])}-{fmt_hex24(ctx['cmd_reg_hdr_off'] + ctx['cmd_reg_hdr_len'] - 1)}</code> ({ctx['cmd_reg_hdr_len']} bytes)</li>",
            f"<li><strong>Command descriptor table:</strong> <code>{fmt_hex24(ctx['cmd_reg_desc_off'])}-{fmt_hex24(ctx['cmd_reg_desc_off'] + (ctx['cmd_reg_desc_len'] * ctx['cmd_reg_desc_cap']) - 1)}</code> ({ctx['cmd_reg_desc_len'] * ctx['cmd_reg_desc_cap']} bytes)</li>",
            f"<li><strong>Overlay state table:</strong> <code>{fmt_hex24(ctx['cmd_reg_state_off'])}-{fmt_hex24(ctx['cmd_reg_state_off'] + (ctx['cmd_reg_state_len'] * ctx['cmd_reg_state_cap']) - 1)}</code> ({ctx['cmd_reg_state_len'] * ctx['cmd_reg_state_cap']} bytes)</li>",
            f"<li><strong>Shared metadata:</strong> <code>{fmt_hex24(ctx['ovl_meta_off'])}-{fmt_hex24(ctx['ovl_meta_off'] + ctx['ovl_meta_len'] - 1)}</code> ({ctx['ovl_meta_len']} bytes)</li>",
            f"<li><strong>Pause flag:</strong> <code>{fmt_hex24(ctx['ui_flags_off'])}</code> (1 byte)</li>",
            f"<li><strong>Heap metadata:</strong> <code>{fmt_hex24(ctx['heap_meta_abs'])}-{fmt_hex24(ctx['heap_meta_abs'] + 0xFF)}</code></li>",
            f"<li><strong>Heap arena:</strong> <code>{fmt_hex24(ctx['heap_arena_abs'])}-{fmt_hex24(ctx['heap_arena_end_abs'])}</code> ({ctx['heap_arena_size']} bytes)</li>",
        ]
    )

    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>ReadyShell Overlay Inventory Report ({html.escape(ctx['version'])})</title>
  <style>
    :root {{
      --bg: #091017;
      --panel: #10202c;
      --panel-2: #132734;
      --ink: #e6f1fb;
      --muted: #9cb7ca;
      --line: #33546c;
      --accent: #7ad0c0;
      --accent-2: #f2be6b;
      --ok: #83e3a1;
      --hot: #8bb8ff;
      --warn: #ff9f7a;
      --mono: "Menlo", "Consolas", monospace;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      color: var(--ink);
      font-family: "Avenir Next", "Segoe UI", "Trebuchet MS", Arial, sans-serif;
      background:
        radial-gradient(1000px 700px at 0% -10%, rgba(79, 143, 180, 0.22), transparent 55%),
        radial-gradient(900px 700px at 100% 0%, rgba(122, 208, 192, 0.15), transparent 48%),
        linear-gradient(180deg, #091017, #0c141b);
    }}
    header {{
      padding: 28px 24px 16px;
      border-bottom: 1px solid var(--line);
      background: linear-gradient(180deg, rgba(122,208,192,0.09), rgba(0,0,0,0));
    }}
    h1 {{
      margin: 0;
      font-size: clamp(1.5rem, 4.2vw, 2.4rem);
      letter-spacing: .02em;
    }}
    .sub {{
      margin: 10px 0 0;
      max-width: 112ch;
      line-height: 1.5;
      color: var(--muted);
    }}
    main {{
      display: grid;
      gap: 12px;
      padding: 16px;
    }}
    section {{
      border: 1px solid var(--line);
      border-radius: 14px;
      padding: 12px;
      background: linear-gradient(180deg, var(--panel), var(--panel-2));
      box-shadow: 0 10px 30px rgba(0, 0, 0, 0.20);
    }}
    h2 {{ margin: 2px 0 8px; font-size: 1.06rem; }}
    h3 {{ margin: 2px 0 8px; font-size: .98rem; }}
    p, li {{ line-height: 1.45; }}
    code {{ font-family: var(--mono); background: rgba(10, 21, 32, 0.72); padding: 1px 4px; border-radius: 4px; }}
    .cards {{
      display: grid;
      grid-template-columns: repeat(5, minmax(150px, 1fr));
      gap: 9px;
    }}
    .card {{
      border: 1px solid #385b74;
      border-radius: 10px;
      padding: 9px 10px;
      background: rgba(8, 17, 24, 0.32);
    }}
    .k {{
      font-size: .74rem;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: .04em;
    }}
    .v {{ margin-top: 4px; font-size: 1rem; font-weight: 700; }}
    .grid-2 {{
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
    }}
    .table-wrap {{ overflow-x: auto; }}
    table {{
      width: 100%;
      border-collapse: collapse;
      font-size: .84rem;
    }}
    th, td {{
      border: 1px solid #2d4b64;
      padding: 6px 7px;
      text-align: left;
      vertical-align: top;
      white-space: nowrap;
    }}
    th {{ background: #13293d; color: #dbedff; }}
    td.num {{ text-align: right; font-family: var(--mono); }}
    .detail-grid {{
      display: grid;
      grid-template-columns: repeat(2, minmax(320px, 1fr));
      gap: 12px;
    }}
    .topology-grid {{
      display: grid;
      grid-template-columns: repeat(4, minmax(220px, 1fr));
      gap: 12px;
      margin-top: 10px;
    }}
    .overlay-card {{
      border: 1px solid #375770;
      border-radius: 12px;
      padding: 12px;
      background: rgba(7, 15, 22, 0.48);
    }}
    .overlay-card.shared {{
      border-color: rgba(122,208,192,0.55);
      box-shadow: inset 0 0 0 1px rgba(122,208,192,0.18);
    }}
    .overlay-card.exec {{
      border-color: rgba(139,184,255,0.55);
      box-shadow: inset 0 0 0 1px rgba(139,184,255,0.18);
    }}
    .overlay-head {{
      display: flex;
      justify-content: space-between;
      gap: 8px;
      align-items: baseline;
      margin-bottom: 8px;
    }}
    .overlay-name {{
      font-weight: 700;
      font-size: 1rem;
    }}
    .overlay-kind {{
      font-size: .72rem;
      color: var(--muted);
      text-transform: uppercase;
      letter-spacing: .04em;
    }}
    .overlay-meta {{
      color: var(--muted);
      font-size: .82rem;
      margin-bottom: 10px;
      line-height: 1.4;
    }}
    .command-pills {{
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
    }}
    .pill {{
      display: inline-flex;
      align-items: center;
      padding: 4px 9px;
      border-radius: 999px;
      font-family: var(--mono);
      font-size: .8rem;
      background: rgba(122,208,192,0.16);
      border: 1px solid rgba(122,208,192,0.42);
      color: var(--ink);
    }}
    .pill.exec {{
      background: rgba(139,184,255,0.16);
      border-color: rgba(139,184,255,0.42);
    }}
    .note {{
      border-left: 4px solid var(--accent-2);
      background: rgba(242, 190, 107, 0.10);
      padding: 10px 12px;
      border-radius: 8px;
    }}
    .bank-visual {{
      display: grid;
      gap: 10px;
      margin-top: 10px;
    }}
    .bank {{
      border: 1px solid #375770;
      border-radius: 12px;
      padding: 10px;
      background: rgba(7, 15, 22, 0.48);
    }}
    .bank-title {{
      font-weight: 700;
      margin-bottom: 8px;
    }}
    .stack {{
      display: grid;
      gap: 8px;
    }}
    .slot {{
      border-radius: 10px;
      padding: 10px 12px;
      border: 1px solid rgba(255,255,255,0.18);
    }}
    .slot.parse {{
      background: linear-gradient(180deg, rgba(122,208,192,0.32), rgba(122,208,192,0.14));
      border-color: rgba(122,208,192,0.55);
    }}
    .slot.exec {{
      background: linear-gradient(180deg, rgba(139,184,255,0.32), rgba(139,184,255,0.14));
      border-color: rgba(139,184,255,0.55);
    }}
    .slot.free {{
      background: linear-gradient(180deg, rgba(255,159,122,0.20), rgba(255,159,122,0.08));
      border-color: rgba(255,159,122,0.45);
    }}
    .slot.meta {{
      background: linear-gradient(180deg, rgba(242,190,107,0.28), rgba(242,190,107,0.12));
      border-color: rgba(242,190,107,0.55);
    }}
    .slot.heap {{
      background: linear-gradient(180deg, rgba(131,227,161,0.28), rgba(131,227,161,0.10));
      border-color: rgba(131,227,161,0.52);
    }}
    .slot small {{
      display: block;
      color: var(--muted);
      margin-top: 4px;
      line-height: 1.4;
    }}
    @media (max-width: 1100px) {{
      .cards {{ grid-template-columns: repeat(2, minmax(150px, 1fr)); }}
      .topology-grid {{ grid-template-columns: 1fr; }}
      .grid-2, .detail-grid {{ grid-template-columns: 1fr; }}
    }}
  </style>
</head>
<body>
<header>
  <h1>ReadyShell Overlay Inventory Report ({html.escape(ctx['version'])})</h1>
  <p class="sub">
    Artifact-backed inventory for the current ReadyShell overlay system. This report is generated from the live build
    outputs, the ReadyShell linker map, and the active {html.escape(ctx['disk_image_label'])} instead of hand-maintained notes.
  </p>
</header>
<main>
  <section>
    <h2>Executive Summary</h2>
    <div class="cards">
      <div class="card"><div class="k">Profile</div><div class="v"><code>{html.escape(ctx['profile'])}</code></div></div>
      <div class="card"><div class="k">Disk Source</div><div class="v"><code>{html.escape(ctx['disk_path'])}</code></div></div>
      <div class="card"><div class="k">Overlay Window</div><div class="v"><code>{fmt_range(ctx['overlay_start'], ctx['himem'] - 1)}</code></div></div>
      <div class="card"><div class="k">Window Size</div><div class="v">{ctx['window_size']} bytes</div></div>
      <div class="card"><div class="k">Resident Heap</div><div class="v">{ctx['heap_size']} bytes</div></div>
    </div>
      <div class="note">
      Overlays {html.escape(fmt_overlay_nums(ctx['cached_overlays']))} are boot-loaded once and cached in one shared REU bank using
      two full-window slots.
      Overlays {html.escape(fmt_overlay_nums(ctx['demand_overlays']))} are loaded from disk on demand. Bank
      <code>0x48</code> is shared for the external-command registry, overlay metadata, pause state, command handoff scratch,
      and the REU-backed ReadyShell value arena.
    </div>
  </section>

  <section class="grid-2">
    <div>
      <h2>Runtime Memory Map</h2>
      <ul>
        <li><strong>Resident app window:</strong> <code>$1000-$C5FF</code> ({0xB600} bytes)</li>
        <li><strong>Overlay load bytes:</strong> <code>{fmt_range(ctx['overlay_loadaddr'], ctx['overlay_start'] - 1)}</code></li>
        <li><strong>Overlay execution window:</strong> <code>{fmt_range(ctx['overlay_start'], ctx['himem'] - 1)}</code> ({ctx['window_size']} bytes)</li>
        <li><strong>Resident BSS:</strong> <code>{fmt_range(ctx['bss_start'], ctx['bss_end'])}</code> ({ctx['bss_size']} bytes)</li>
        <li><strong>Resident heap:</strong> <code>{fmt_range(ctx['heap_start'], ctx['heap_end'])}</code> ({ctx['heap_size']} bytes)</li>
        <li><strong>High RAM runtime:</strong> <code>{fmt_range(ctx['runtime_addr'], ctx['runtime_limit'] - 1)}</code> ({ctx['runtime_limit'] - ctx['runtime_addr']} bytes)</li>
      </ul>
    </div>
    <div>
      <h2>REU Layout</h2>
      <ul>
        {''.join(reu_items)}
      </ul>
    </div>
  </section>

  <section>
    <h2>Shared REU Bank Visual</h2>
    <div class="bank-visual">
      <div class="bank">
        <div class="bank-title">Bank <code>0x{ctx['ovl_cache_bank']:02X}</code>: Core Overlay Cache</div>
        <div class="stack">
          <div class="slot parse"><strong>Overlay 1 parse slot</strong> <code>{fmt_hex24(ctx['ovl_parse_abs'])}-{fmt_hex24(ctx['ovl_parse_abs'] + ctx['ovl_slot_len'] - 1)}</code><small>Full <code>0x{ctx['ovl_slot_len']:04X}</code>-byte window image for <code>rsparser.prg</code>. Live payload is <code>{ctx['overlays'][0]['live_size']}</code> bytes, but the cache stores the whole overlay window so writable overlay data survives phase switching.</small></div>
          <div class="slot exec"><strong>Overlay 2 exec slot</strong> <code>{fmt_hex24(ctx['ovl_exec_abs'])}-{fmt_hex24(ctx['ovl_exec_abs'] + ctx['ovl_slot_len'] - 1)}</code><small>Full <code>0x{ctx['ovl_slot_len']:04X}</code>-byte window image for <code>rsvm.prg</code>. Live payload is <code>{ctx['overlays'][1]['live_size']}</code> bytes.</small></div>
          <div class="slot free"><strong>Free tail</strong> <code>{fmt_hex24(ctx['cache_tail_start'])}-{fmt_hex24(ctx['ovl_cache_base'] + 0xFFFF)}</code><small><code>{ctx['cache_tail_size']}</code> bytes currently left free because overlays 1 and 2 now share one bank instead of consuming separate banks.</small></div>
        </div>
      </div>
      <div class="bank">
        <div class="bank-title">Bank <code>0x48</code>: Shared Shell State</div>
        <div class="stack">
          <div class="slot meta"><strong>Command registry</strong> <code>{fmt_hex24(ctx['cmd_reg_hdr_off'])}-{fmt_hex24(ctx['cmd_reg_state_off'] + (ctx['cmd_reg_state_len'] * ctx['cmd_reg_state_cap']) - 1)}</code><small>Registry header, external-command descriptors, and per-overlay load/cache state stored in REU metadata so new external commands do not consume resident BSS.</small></div>
          <div class="slot meta"><strong>Shared metadata + pause</strong> <code>{fmt_hex24(ctx['ovl_meta_off'])}-{fmt_hex24(ctx['ui_flags_off'])}</code><small>Overlay cache metadata record plus the shared pause bit used by the resident line printer and <code>MORE</code>.</small></div>
          <div class="slot heap"><strong>Command scratch + value arena</strong> <code>{fmt_hex24(ctx['scratch_off'])}-{fmt_hex24(ctx['heap_arena_end_abs'])}</code><small>Command handoff scratch, REU heap metadata, and the persistent value arena for REU-backed strings, arrays, and objects.</small></div>
        </div>
      </div>
    </div>
  </section>

  <section>
    <h2>Overlay Inventory</h2>
    <div class="table-wrap">
      <table>
        <thead>
          <tr>
            <th>Ovl</th>
            <th>Role</th>
            <th>Build PRG</th>
            <th>Disk Name</th>
            <th>PRG Bytes</th>
            <th>Disk Blocks</th>
            <th>Live Bytes</th>
            <th>Window Use</th>
            <th>Commands</th>
            <th>REU</th>
          </tr>
        </thead>
        <tbody>
          {''.join(overlay_rows)}
        </tbody>
      </table>
    </div>
  </section>

  <section>
    <h2>Command Topology</h2>
    <div class="note">
      This view is command-centric rather than file-centric. Shared overlays stand out here because multiple command pills sit inside one overlay card.
    </div>
    <div class="topology-grid">
      {''.join(
          (
              "<div class='overlay-card exec'>"
              "<div class='overlay-head'>"
              "<div class='overlay-name'>Overlay 2</div>"
              "<div class='overlay-kind'>shared execution core</div>"
              "</div>"
              "<div class='overlay-meta'><code>rsvm</code><br />Shared execution paths that command overlays return to.</div>"
              "<div class='command-pills'>"
              + "".join(f"<span class='pill exec'>{html.escape(cmd)}</span>" for cmd in ctx['overlays'][1]['spec'].command_list)
              + "</div></div>"
          )
          + "".join(
              "<div class='overlay-card "
              + ("shared" if len(row['spec'].command_list) > 1 else "")
              + "'>"
              "<div class='overlay-head'>"
              f"<div class='overlay-name'>Overlay {row['num']}</div>"
              f"<div class='overlay-kind'>{html.escape(overlay_kind_label(row))}</div>"
              "</div>"
              f"<div class='overlay-meta'><code>{html.escape(row['spec'].disk_name)}</code><br />{html.escape(row['spec'].purpose)}</div>"
              "<div class='command-pills'>"
              + "".join(f"<span class='pill'>{html.escape(cmd)}</span>" for cmd in row['spec'].command_list)
              + "</div></div>"
              for row in ctx['demand_overlays']
          )
      )}
    </div>
  </section>

  <section>
    <h2>Resident Program</h2>
    <ul>
      <li><strong>Build PRG:</strong> <code>readyshell.prg</code></li>
      <li><strong>Disk name:</strong> <code>readyshell</code></li>
      <li><strong>Disk footprint:</strong> {ctx['resident_prg_size']} bytes, {ctx['resident_disk_blocks']} D71 blocks</li>
      <li><strong>Resident sources:</strong> <code>{html.escape(short_sources(ctx['resident_sources']))}</code></li>
      <li><strong>Resident asm/runtime:</strong> <code>{html.escape(short_sources(ctx['resident_asm_sources']))}</code></li>
      <li><strong>Role:</strong> {html.escape(RESIDENT_COMMANDS)}</li>
      <li><strong>Current resident segments:</strong> <code>CODE ${ctx['code_size']:04X}</code>, <code>RODATA ${ctx['rodata_size']:04X}</code>, <code>DATA ${ctx['data_size']:04X}</code>, <code>INIT ${ctx['init_size']:04X}</code>, <code>ONCE ${ctx['once_size']:04X}</code>, <code>BSS ${ctx['bss_size']:04X}</code></li>
    </ul>
  </section>

  <section>
    <h2>Per-Overlay Detail</h2>
    <div class="detail-grid">
      {''.join(detail_cards)}
    </div>
  </section>

  <section>
    <h2>Observations</h2>
    <ul>
      <li>Overlay 2 is effectively full at {ctx['overlays'][1]['live_size']} bytes of {ctx['window_size']} ({fmt_pct(ctx['overlays'][1]['window_pct'])}).</li>
      <li>Overlay 1 is also large at {ctx['overlays'][0]['live_size']} bytes ({fmt_pct(ctx['overlays'][0]['window_pct'])}).</li>
      <li>The resident heap below the overlay load address is only {ctx['heap_size']} bytes, so large working sets depend on overlays and REU-backed storage.</li>
      <li>Overlays {html.escape(fmt_overlay_nums(ctx['cached_overlays']))} now share bank <code>0x{ctx['ovl_cache_bank']:02X}</code> and leave a free tail of {ctx['cache_tail_size']} bytes in that bank instead of consuming separate banks.</li>
      <li>Command overlays {html.escape(fmt_overlay_nums(ctx['demand_overlays']))} are smaller, but they pay a disk-load cost on every command invocation because they are not cached in fixed REU banks today.</li>
      <li>Overlay 2 owns the shared formatting buffers, which is why it consumes almost the entire overlay window.</li>
    </ul>
  </section>
</main>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--profile", default="precog-dual-d71")
    parser.add_argument("--disk", type=Path)
    parser.add_argument("--makefile", type=Path, default=ROOT / "Makefile")
    parser.add_argument("--map", type=Path, default=ROOT / "obj" / "readyshell.map")
    parser.add_argument("--readyshell-prg", type=Path, default=ROOT / "readyshell.prg")
    parser.add_argument("--build-version", type=Path, default=ROOT / "src" / "generated" / "build_version.h")
    parser.add_argument(
        "--cmd-overlay-header",
        type=Path,
        default=ROOT / "src" / "apps" / "readyshellpoc" / "core" / "rs_cmd_overlay.h",
    )
    parser.add_argument(
        "--value-c",
        type=Path,
        default=ROOT / "src" / "apps" / "readyshellpoc" / "core" / "rs_value.c",
    )
    parser.add_argument(
        "--overlay-c",
        type=Path,
        default=ROOT / "src" / "apps" / "readyshellpoc" / "platform" / "c64" / "rs_overlay_c64.c",
    )
    parser.add_argument(
        "--shell-c",
        type=Path,
        default=ROOT / "src" / "apps" / "readyshellpoc" / "readyshellpoc.c",
    )
    parser.add_argument(
        "--ui-state-h",
        type=Path,
        default=ROOT / "src" / "apps" / "readyshellpoc" / "core" / "rs_ui_state.h",
    )
    parser.add_argument(
        "--markdown-out",
        type=Path,
        default=ROOT / "docs" / "readyshell_overlay_inventory.md",
    )
    parser.add_argument(
        "--html-out",
        type=Path,
        default=ROOT / "docs" / "readyshell_overlay_inventory.html",
    )
    args = parser.parse_args()
    args.root = args.root.resolve()
    if args.disk is None:
        args.disk = resolve_default_disk_path(args.root, args.profile)
    else:
        args.disk = (args.root / args.disk).resolve() if not args.disk.is_absolute() else args.disk.resolve()
    args.makefile = (args.root / args.makefile).resolve() if not args.makefile.is_absolute() else args.makefile.resolve()
    args.map = (args.root / args.map).resolve() if not args.map.is_absolute() else args.map.resolve()
    args.readyshell_prg = (
        (args.root / args.readyshell_prg).resolve() if not args.readyshell_prg.is_absolute() else args.readyshell_prg.resolve()
    )
    args.build_version = (
        (args.root / args.build_version).resolve() if not args.build_version.is_absolute() else args.build_version.resolve()
    )
    args.cmd_overlay_header = (
        (args.root / args.cmd_overlay_header).resolve()
        if not args.cmd_overlay_header.is_absolute()
        else args.cmd_overlay_header.resolve()
    )
    args.value_c = (args.root / args.value_c).resolve() if not args.value_c.is_absolute() else args.value_c.resolve()
    args.overlay_c = (args.root / args.overlay_c).resolve() if not args.overlay_c.is_absolute() else args.overlay_c.resolve()
    args.shell_c = (args.root / args.shell_c).resolve() if not args.shell_c.is_absolute() else args.shell_c.resolve()
    args.ui_state_h = (args.root / args.ui_state_h).resolve() if not args.ui_state_h.is_absolute() else args.ui_state_h.resolve()
    args.markdown_out = (
        (args.root / args.markdown_out).resolve() if not args.markdown_out.is_absolute() else args.markdown_out.resolve()
    )
    args.html_out = (args.root / args.html_out).resolve() if not args.html_out.is_absolute() else args.html_out.resolve()

    ctx = build_report_context(args)
    args.markdown_out.write_text(render_markdown(ctx) + "\n", encoding="utf-8")
    args.html_out.write_text(render_html(ctx), encoding="utf-8")
    print(f"wrote {args.markdown_out}")
    print(f"wrote {args.html_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
