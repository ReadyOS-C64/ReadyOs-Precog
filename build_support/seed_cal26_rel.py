#!/usr/bin/env python3
"""
Seed ReadyOS disk image with valid CAL26 REL files.
Creates:
  - CAL26.REL (record length 64)
  - CAL26CFG.REL (record length 32)
with two events in the current month (year fixed to 2026).
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import subprocess
import sys
import tempfile

from readyos_profiles import AUTHORITATIVE_PROFILE_ID, latest_profile_disk_path

CAL_YEAR = 2026
CAL_DAYS = 365

REC_EVENTS_LEN = 64
REC_CFG_LEN = 32
REC_SUPERBLOCK = 1
REC_DAY_BASE = 2
REC_EVENT_1 = 367
REC_EVENT_2 = 368
NEXT_RECORD = 369

MONTH_DAYS = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]


def doy_from_month_day(month: int, day: int) -> int:
    return day + sum(MONTH_DAYS[: month - 1])


def write_dec(buf: bytearray, off: int, digits: int, value: int) -> None:
    for i in range(digits):
        buf[off + digits - 1 - i] = ord("0") + (value % 10)
        value //= 10


def pick_event_days(month: int) -> tuple[int, int]:
    mdays = MONTH_DAYS[month - 1]
    d1 = 5 if mdays >= 5 else 1
    d2 = 15 if mdays >= 15 else mdays
    if d1 == d2:
        d2 = d1 - 1 if d1 > 1 else min(2, mdays)
    return d1, d2


def build_events_blob(month: int, day1: int, day2: int) -> bytes:
    total_records = REC_EVENT_2
    blob = bytearray(b" " * (total_records * REC_EVENTS_LEN))

    def rec_off(recno: int) -> int:
        return (recno - 1) * REC_EVENTS_LEN

    doy1 = doy_from_month_day(month, day1)
    doy2 = doy_from_month_day(month, day2)

    # Superblock (record 1)
    off = rec_off(REC_SUPERBLOCK)
    sb = blob[off : off + REC_EVENTS_LEN]
    sb[0:4] = b"c26e"
    sb[4] = ord("1")
    write_dec(sb, 5, 5, 0)  # free_head
    write_dec(sb, 10, 5, NEXT_RECORD)
    csum = sum(sb[:15]) & 0xFFFF
    write_dec(sb, 15, 5, csum)
    blob[off : off + REC_EVENTS_LEN] = sb

    # Day index records (2..366)
    for doy in range(1, CAL_DAYS + 1):
        off = rec_off(REC_DAY_BASE + (doy - 1))
        rec = blob[off : off + REC_EVENTS_LEN]
        rec[0] = ord("i")
        write_dec(rec, 1, 3, doy)

        if doy == doy1:
            write_dec(rec, 6, 5, REC_EVENT_1)  # head
            write_dec(rec, 11, 5, REC_EVENT_1)  # tail
            write_dec(rec, 16, 5, 1)            # count
        elif doy == doy2:
            write_dec(rec, 6, 5, REC_EVENT_2)
            write_dec(rec, 11, 5, REC_EVENT_2)
            write_dec(rec, 16, 5, 1)
        else:
            write_dec(rec, 6, 5, 0)
            write_dec(rec, 11, 5, 0)
            write_dec(rec, 16, 5, 0)

        blob[off : off + REC_EVENTS_LEN] = rec

    # Event 1
    off = rec_off(REC_EVENT_1)
    ev1 = blob[off : off + REC_EVENTS_LEN]
    ev1[0] = ord("e")
    ev1[1] = ord("0")  # not done
    ev1[2] = ord("0")  # not deleted
    write_dec(ev1, 3, 3, doy1)
    write_dec(ev1, 6, 5, 0)  # prev
    write_dec(ev1, 11, 5, 0)  # next
    txt1 = f"SEEDED EVENT A {month:02d}/{day1:02d}".encode("ascii")
    text_len = min(len(txt1), REC_EVENTS_LEN - 18)
    write_dec(ev1, 16, 2, text_len)
    ev1[18 : 18 + text_len] = txt1[:text_len]
    blob[off : off + REC_EVENTS_LEN] = ev1

    # Event 2
    off = rec_off(REC_EVENT_2)
    ev2 = blob[off : off + REC_EVENTS_LEN]
    ev2[0] = ord("e")
    ev2[1] = ord("1")  # done
    ev2[2] = ord("0")  # not deleted
    write_dec(ev2, 3, 3, doy2)
    write_dec(ev2, 6, 5, 0)
    write_dec(ev2, 11, 5, 0)
    txt2 = f"SEEDED EVENT B {month:02d}/{day2:02d}".encode("ascii")
    text_len = min(len(txt2), REC_EVENTS_LEN - 18)
    write_dec(ev2, 16, 2, text_len)
    ev2[18 : 18 + text_len] = txt2[:text_len]
    blob[off : off + REC_EVENTS_LEN] = ev2

    return bytes(blob)


def build_cfg_blob(month: int, today_day: int) -> bytes:
    cfg = bytearray(b" " * REC_CFG_LEN)
    cfg[0:4] = b"c26c"
    cfg[4] = ord("1")
    today_doy = doy_from_month_day(month, today_day)
    write_dec(cfg, 5, 3, today_doy)
    cfg[8] = ord("0")  # week start Sunday
    return bytes(cfg)


def run_c1541(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, capture_output=True, check=check)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--disk", help="Path to disk image")
    parser.add_argument("--month", type=int, default=0, help="Month in 2026 (1-12); default current month")
    parser.add_argument("--today-day", type=int, default=0, help="Day-of-month to store as TODAY in config")
    args = parser.parse_args()

    if args.disk:
        disk = os.path.abspath(args.disk)
    else:
        disk = str(latest_profile_disk_path(AUTHORITATIVE_PROFILE_ID, 8))
    if not os.path.exists(disk):
        print(f"error: disk image not found: {disk}", file=sys.stderr)
        return 1

    today = dt.date.today()
    month = args.month if 1 <= args.month <= 12 else today.month
    day1, day2 = pick_event_days(month)

    td = args.today_day if args.today_day > 0 else min(today.day, MONTH_DAYS[month - 1])
    td = max(1, min(td, MONTH_DAYS[month - 1]))

    events_blob = build_events_blob(month, day1, day2)
    cfg_blob = build_cfg_blob(month, td)

    with tempfile.TemporaryDirectory(prefix="cal26_seed_") as tmp:
        events_path = os.path.join(tmp, "cal26_events.bin")
        cfg_path = os.path.join(tmp, "cal26_cfg.bin")

        with open(events_path, "wb") as f:
            f.write(events_blob)
        with open(cfg_path, "wb") as f:
            f.write(cfg_blob)

        # Best-effort delete prior REL variants only.
        # Do not delete "cal26" (the PRG app), or launcher cannot load CAL26.
        run_c1541(["c1541", disk,
                   "-delete", "cal26.rel",
                   "-delete", "cal26cfg.rel"], check=False)

        # Write fresh REL files
        w = run_c1541([
            "c1541", disk,
            "-write", events_path, "cal26.rel,l,64",
            "-write", cfg_path, "cal26cfg.rel,l,32",
            "-list",
        ], check=False)

        print(w.stdout)
        if w.returncode != 0:
            print(w.stderr, file=sys.stderr)
            return w.returncode

    print(f"Seeded CAL26 REL files on {disk}")
    print(f"  Month: {month:02d}/{CAL_YEAR}")
    print(f"  Events: {month:02d}/{day1:02d} and {month:02d}/{day2:02d}")
    print(f"  TODAY in config: {month:02d}/{td:02d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
