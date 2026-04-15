#!/usr/bin/env python3
"""Parse xseqchk binary result block."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


SLOT_BASE = 0x20
SLOT_SIZE = 0x30
PREVIEW_LEN = 16


def petscii_byte_to_host(value: int) -> int:
    if 0xC1 <= value <= 0xDA:
        return value & 0x7F
    return value


def decode_text(raw: bytes) -> str:
    norm = bytes(petscii_byte_to_host(b) for b in raw if b != 0)
    return norm.decode("latin1", "replace")


def decode_preview(raw: bytes, length: int) -> bytes:
    return bytes(petscii_byte_to_host(b) for b in raw[:length])


def slot_fields(raw: bytes, index: int) -> dict[str, object]:
    base = SLOT_BASE + index * SLOT_SIZE
    preview_len = raw[base + 16]
    preview = decode_preview(raw[base + 18:base + 18 + PREVIEW_LEN], min(preview_len, PREVIEW_LEN))
    return {
        "index": index,
        "drive": raw[base + 0],
        "kind": chr(petscii_byte_to_host(raw[base + 1])) if raw[base + 1] else "-",
        "algo": chr(petscii_byte_to_host(raw[base + 2])) if raw[base + 2] else "-",
        "prefix_zero": raw[base + 3],
        "mode": chr(petscii_byte_to_host(raw[base + 4])) if raw[base + 4] else "-",
        "op_rc": raw[base + 5],
        "existed_before": raw[base + 6],
        "open_rc": raw[base + 7],
        "write_rc": raw[base + 8],
        "status_rc": raw[base + 9],
        "status_code": raw[base + 10],
        "read_rc": raw[base + 11],
        "entry_rc": raw[base + 12],
        "type": raw[base + 13],
        "size": raw[base + 14] | (raw[base + 15] << 8),
        "len": raw[base + 16],
        "expected_len": raw[base + 17],
        "preview": preview,
        "status_msg": decode_text(raw[base + 34:base + 48]),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dbg-bin", required=True)
    args = ap.parse_args()

    raw = Path(args.dbg_bin).read_bytes()
    if len(raw) < SLOT_BASE:
        print(f"error: short xseqchk status block ({len(raw)} bytes)", file=sys.stderr)
        return 2

    marker = raw[0]
    version = raw[1]
    ok = raw[2]
    case_id = raw[3]
    fail_step = raw[4]
    fail_detail = raw[5]
    winners = raw[9]
    slot_count = raw[8]

    print(
        f"marker=0x{marker:02X} version={version} case={case_id} "
        f"ok={ok} fail_step={fail_step} fail_detail={fail_detail} winners=0x{winners:02X}"
    )

    any_pass = False
    all_slots_ok = True
    algo_summary: dict[tuple[str, int, str], bool] = {}

    for index in range(slot_count):
        fields = slot_fields(raw, index)
        if fields["drive"] == 0:
            continue
        slot_ok = fields["op_rc"] == 0
        key = (fields["algo"], int(fields["prefix_zero"]), fields["mode"])
        algo_summary[key] = algo_summary.get(key, True) and slot_ok
        any_pass = any_pass or slot_ok
        all_slots_ok = all_slots_ok and slot_ok
        preview_text = fields["preview"].decode("latin1", "replace").replace("\r", "\\r")
        print(
            f"slot={fields['index']} d{fields['drive']} kind={fields['kind']} "
            f"algo={fields['algo']} p0={fields['prefix_zero']} mode={fields['mode']} "
            f"op_rc={fields['op_rc']} open={fields['open_rc']} write={fields['write_rc']} "
            f"st_rc={fields['status_rc']} st={fields['status_code']} read={fields['read_rc']} "
            f"entry={fields['entry_rc']} type=0x{fields['type']:02X} size={fields['size']} "
            f"len={fields['len']}/{fields['expected_len']} preview='{preview_text}' "
            f"msg='{fields['status_msg']}' pass={slot_ok}"
        )

    for key, passed in sorted(algo_summary.items()):
        algo, prefix_zero, mode = key
        label = f"algo={algo} prefix0={prefix_zero} mode={mode}"
        print(f"{label} pass={passed}")

    if algo_summary:
        any_algo_pass = any(algo_summary.values())
        if case_id == 0:
            print(f"RESULT: {'PASS' if any_algo_pass else 'FAIL'} (matrix)")
            return 0 if any_algo_pass else 1

    print(f"RESULT: {'PASS' if all_slots_ok else 'FAIL'}")
    return 0 if all_slots_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
