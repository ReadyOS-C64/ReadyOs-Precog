#!/usr/bin/env python3
"""
file_dialog_memory_report.py

Capture and compare ReadyOS map-file memory metrics for the paged file-dialog
refactor using the project's current runtime contracts.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OBJ_DIR = ROOT / "obj"
DEFAULT_APPS = [
    "editor",
    "quicknotes",
    "tasklist",
    "clipmgr",
    "simplecells",
    "simplefiles",
    "readyshell",
]
APP_SNAPSHOT_END = 0xC5FF
APP_HIMEM = 0xC600
MAIN_SEGMENTS = ("STARTUP", "LOWCODE", "CODE", "RODATA", "DATA", "INIT", "ONCE", "BSS")
SEGMENT_RE = re.compile(
    r"^\s*([A-Z0-9_]+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+[0-9A-F]{5}\s*$",
    flags=re.MULTILINE,
)
SYMBOL_RE_TEMPLATE = r"^\s*{name}\s+([0-9A-F]{{6}})\s"


def parse_map(path: Path) -> tuple[str, dict[str, tuple[int, int, int]]]:
    txt = path.read_text(encoding="utf-8", errors="replace")
    segments: dict[str, tuple[int, int, int]] = {}
    in_list = False
    for line in txt.splitlines():
        if line.strip() == "Segment list:":
            in_list = True
            continue
        if not in_list:
            continue
        match = SEGMENT_RE.match(line)
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
        raise ValueError(f"segment list missing: {path}")
    return txt, segments


def parse_symbol(txt: str, name: str) -> int | None:
    match = re.search(SYMBOL_RE_TEMPLATE.format(name=re.escape(name)), txt, re.MULTILINE)
    if not match:
        return None
    return int(match.group(1), 16)


def fmt_hex(value: int | None) -> str:
    if value is None:
        return "-"
    return f"${value:04X}"


def fmt_dec(value: int | None) -> str:
    if value is None:
        return "-"
    return str(value)


def collect_metrics(app: str) -> dict[str, object]:
    map_path = OBJ_DIR / f"{app}.map"
    txt, segments = parse_map(map_path)
    runtime_end = max(segments[name][1] for name in MAIN_SEGMENTS if name in segments)
    bss_bytes = segments.get("BSS", (0, 0, 0))[2]
    used_bytes = sum(segments.get(name, (0, 0, 0))[2] for name in MAIN_SEGMENTS if name in segments)
    stack_size = parse_symbol(txt, "__STACKSIZE__")
    himem = parse_symbol(txt, "__HIMEM__")
    overlay_start = parse_symbol(txt, "__OVERLAYSTART__")

    if overlay_start is not None:
        app_class = "overlay"
        effective_limit = overlay_start - 1
    elif stack_size is not None and himem is not None:
        app_class = "stack_reserved"
        effective_limit = himem - stack_size - 1
    else:
        app_class = "normal"
        effective_limit = APP_SNAPSHOT_END

    return {
        "app": app,
        "class": app_class,
        "runtime_end": runtime_end,
        "bss_bytes": bss_bytes,
        "used_bytes": used_bytes,
        "headroom_c5ff": APP_SNAPSHOT_END - runtime_end,
        "headroom_c600": APP_HIMEM - runtime_end,
        "effective_limit": effective_limit,
        "effective_headroom": effective_limit - runtime_end,
        "stack_size": stack_size,
        "himem": himem,
        "overlay_start": overlay_start,
    }


def print_report(rows: list[dict[str, object]], baseline: dict[str, dict[str, object]] | None) -> None:
    if baseline:
        print(
            "app         class          end    used    bss   headC5FF  effLimit  effHead   dUsed  dBSS  dHead"
        )
        for row in rows:
            old = baseline.get(row["app"])
            d_used = row["used_bytes"] - old["used_bytes"] if old else None
            d_bss = row["bss_bytes"] - old["bss_bytes"] if old else None
            d_head = row["headroom_c5ff"] - old["headroom_c5ff"] if old else None
            print(
                f"{row['app']:<11}"
                f"{row['class']:<15}"
                f"{fmt_hex(row['runtime_end']):>7} "
                f"{fmt_dec(row['used_bytes']):>6} "
                f"{fmt_dec(row['bss_bytes']):>6} "
                f"{fmt_dec(row['headroom_c5ff']):>9} "
                f"{fmt_hex(row['effective_limit']):>9} "
                f"{fmt_dec(row['effective_headroom']):>8} "
                f"{fmt_dec(d_used):>6} "
                f"{fmt_dec(d_bss):>5} "
                f"{fmt_dec(d_head):>6}"
            )
        return

    print("app         class          end    used    bss   headC5FF  effLimit  effHead")
    for row in rows:
        print(
            f"{row['app']:<11}"
            f"{row['class']:<15}"
            f"{fmt_hex(row['runtime_end']):>7} "
            f"{fmt_dec(row['used_bytes']):>6} "
            f"{fmt_dec(row['bss_bytes']):>6} "
            f"{fmt_dec(row['headroom_c5ff']):>9} "
            f"{fmt_hex(row['effective_limit']):>9} "
            f"{fmt_dec(row['effective_headroom']):>8}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--apps", nargs="*", default=DEFAULT_APPS)
    parser.add_argument("--save", type=Path)
    parser.add_argument("--compare", type=Path)
    args = parser.parse_args()

    rows = [collect_metrics(app) for app in args.apps]

    if args.save:
        args.save.write_text(json.dumps({row["app"]: row for row in rows}, indent=2), encoding="utf-8")

    baseline = None
    if args.compare:
        baseline = json.loads(args.compare.read_text(encoding="utf-8"))

    print_report(rows, baseline)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
