#!/usr/bin/env python3
"""Parse xfilechk binary result block."""

from __future__ import annotations

import argparse
import sys


STEP_NAMES = {
    0: "none",
    1: "mode",
    2: "probe",
    3: "scratch",
    4: "copy_cmd",
    5: "copy_io",
    6: "verify",
    7: "dump",
    8: "stage",
}


def bit_drives(mask: int) -> str:
    parts = []
    for bit, drive in enumerate((8, 9, 10, 11)):
        if mask & (1 << bit):
            parts.append(str(drive))
    return ",".join(parts) if parts else "-"


def u16(lo: int, hi: int) -> int:
    return lo | (hi << 8)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dbg-bin", required=True)
    args = ap.parse_args()

    with open(args.dbg_bin, "rb") as f:
        raw = f.read()

    if len(raw) < 0x80:
        print(f"error: short xfilechk status block ({len(raw)} bytes)", file=sys.stderr)
        return 2

    marker = raw[0]
    version = raw[1]
    ok = raw[2]
    case_id = raw[3]
    fail_step = raw[4]
    fail_detail = raw[5]
    mode8 = raw[6]
    mode9 = raw[7]
    op_a = raw[0x0C]
    op_b = raw[0x0D]
    src_type = raw[0x10]
    src_size = u16(raw[0x11], raw[0x12])
    dst_type = raw[0x13]
    dst_size = u16(raw[0x14], raw[0x15])
    src_len = raw[0x16]
    dst_len = raw[0x17]
    scratch_rc = raw[0x18]
    scratch_status = raw[0x19]
    copy_rc = raw[0x1A]
    copy_status = raw[0x1B]
    verify_code = raw[0x1C]
    dump_open = raw[0x1D]
    dump_write = raw[0x1E]
    probe_cmd = raw[0x20]
    probe_open = raw[0x21]
    probe_first = raw[0x22]
    probe_recovery = raw[0x23]
    stages = bytes(b for b in raw[0x30:0x30 + raw[0x3F]] if b).decode("latin1", "replace")
    status_msg = bytes(b for b in raw[0x40:0x54] if b).decode("latin1", "replace")

    print(
        f"marker=0x{marker:02X} version={version} case={case_id} ok={ok} "
        f"fail_step={fail_step}({STEP_NAMES.get(fail_step, 'unknown')}) fail_detail={fail_detail}"
    )
    print(f"mode_rc: d8={mode8} d9={mode9}")
    if case_id in (11, 12, 13):
        print(
            f"probe case: base8={probe_cmd} base9={probe_open} "
            f"touch={probe_first} rec8={probe_recovery} rec9={raw[0x24]}"
        )
    else:
        print(
            f"probe cmd={probe_cmd:02X}[{bit_drives(probe_cmd)}] "
            f"open={probe_open:02X}[{bit_drives(probe_open)}] "
            f"first={probe_first:02X}[{bit_drives(probe_first)}] "
            f"recovery={probe_recovery:02X}[{bit_drives(probe_recovery)}]"
        )
    print(
        f"op_fields: a={op_a} b={op_b} scratch_rc={scratch_rc} scratch_st={scratch_status} "
        f"copy_rc={copy_rc} copy_st={copy_status} verify={verify_code}"
    )
    print(
        f"src: type={src_type} size={src_size} len={src_len} "
        f"dst: type={dst_type} size={dst_size} len={dst_len}"
    )
    print(f"stages={stages or '-'} status_msg={status_msg or '-'} dump_open={dump_open} dump_write={dump_write}")

    if marker != 0x58 or version != 0x01:
        print("RESULT: FAIL (bad marker/version)")
        return 1
    if dump_open not in (0, 255):
        print("RESULT: FAIL (status dump open failed)")
        return 1
    if ok != 1:
        print("RESULT: FAIL")
        return 1

    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
