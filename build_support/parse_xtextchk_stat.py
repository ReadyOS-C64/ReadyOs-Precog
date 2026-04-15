#!/usr/bin/env python3
"""Parse xtextchk binary result block."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


STEP_NAMES = {
    0: "none",
    1: "case",
    2: "load",
    3: "render",
    4: "dump",
}

SLOT_BASE = 0x20
SLOT_SIZE = 0x20


def decode_name(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("latin1", "replace")


def fmt_byte(value: int) -> str:
    if 32 <= value <= 126:
        return f"0x{value:02X}({chr(value)!r})"
    return f"0x{value:02X}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dbg-bin", required=True)
    args = ap.parse_args()

    raw = Path(args.dbg_bin).read_bytes()
    if len(raw) < 0x80:
        print(f"error: short xtextchk status block ({len(raw)} bytes)", file=sys.stderr)
        return 2

    marker = raw[0]
    version = raw[1]
    ok = raw[2]
    case_id = raw[3]
    fail_step = raw[4]
    fail_detail = raw[5]
    slot_count = raw[8]

    print(
        f"marker=0x{marker:02X} version={version} case={case_id} ok={ok} "
        f"fail_step={fail_step}({STEP_NAMES.get(fail_step, 'unknown')}) fail_detail={fail_detail}"
    )

    reproduced = False
    for index in range(slot_count):
        base = SLOT_BASE + index * SLOT_SIZE
        slot = raw[base:base + SLOT_SIZE]
        name = decode_name(slot[:8])
        load_rc = slot[8]
        line_len = slot[9]
        delim_index = slot[10]
        expected = slot[11]
        loaded = slot[12]
        mapped = slot[13]
        screen = slot[14]
        left = slot[15]
        right = slot[16]
        preview = " ".join(f"{b:02X}" for b in slot[18:26] if b)
        print(
            f"{name}: load_rc={load_rc} len={line_len} delim_index={delim_index} "
            f"expected={fmt_byte(expected)} loaded={fmt_byte(loaded)} "
            f"mapped={fmt_byte(mapped)} screen={fmt_byte(screen)} "
            f"left={fmt_byte(left)} right={fmt_byte(right)} preview=[{preview}]"
        )
        if name in ("apipe", "vline") and screen == 0x20:
            reproduced = True

    if ok == 0:
        print("RESULT: FAIL (harness reported failure)")
        return 1

    if reproduced:
        print("RESULT: REPRODUCED (pipe-style byte rendered as blank)")
        return 1

    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
