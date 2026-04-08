#!/usr/bin/env python3
"""
Build ReadyOS apps.cfg as a strict lowercase-PETASCII SEQ payload.

Source format is sectioned:

  [system]
  variant_name=precog
  variant_boot_name=

  [launcher]
  load_all_to_reu=0
  runappfirst=

  [apps]
  9:editor:editor:1
  text editor with clipboard
  ...

Rules:
- Alphabetic source text must be lowercase.
- `[system]` is emitted first, followed by `[launcher]`, then `[apps]`.
- App catalog entries preserve the existing alternating entry/description format.
- Build-time overrides can replace `load_all_to_reu` and `runappfirst`.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from typing import Dict, List, Optional, Tuple


PRG_RE = re.compile(r"[a-z0-9_.-]+")

SECTION_SYSTEM = "system"
SECTION_LAUNCHER = "launcher"
SECTION_APPS = "apps"
VALID_SECTIONS = {SECTION_SYSTEM, SECTION_LAUNCHER, SECTION_APPS}


def fail(path: str, line_no: int, msg: str) -> None:
    raise ValueError(f"{path}:{line_no}: {msg}")


def has_upper_ascii(text: str) -> bool:
    for ch in text:
        if "A" <= ch <= "Z":
            return True
    return False


def normalize_prg_token(raw: str, path: str, line_no: int) -> str:
    prg = raw.strip()
    if not prg:
        fail(path, line_no, "empty PRG token")
    if has_upper_ascii(prg):
        fail(path, line_no, f"prg token must be lowercase: {raw!r}")
    if "," in prg:
        fail(path, line_no, f"comma suffix not allowed in PRG token: {raw!r}")
    if prg.endswith(".prg"):
        fail(path, line_no, f".prg extension not allowed: {raw!r}")
    if len(prg) == 0 or len(prg) > 12:
        fail(path, line_no, f"prg token length invalid: {raw!r}")
    if not PRG_RE.fullmatch(prg):
        fail(path, line_no, f"invalid prg characters: {raw!r}")
    return prg


def normalize_hotkey_slot(raw: str, path: str, line_no: int) -> str:
    slot = raw.strip()
    if not slot:
        fail(path, line_no, "empty hotkey slot")
    if not slot.isdigit():
        fail(path, line_no, f"hotkey slot must be numeric: {raw!r}")
    if slot == "0" or int(slot, 10) > 9:
        fail(path, line_no, f"hotkey slot must be 1..9: {raw!r}")
    return slot


def parse_app_entry(line: str, path: str, line_no: int) -> Tuple[int, str, str, str]:
    parts = [p.strip() for p in line.split(":")]
    if len(parts) not in (3, 4):
        fail(path, line_no, f"malformed app entry: {line!r}")

    drive_raw, prg_raw, label = parts[:3]
    slot = ""
    if len(parts) == 4:
        slot = normalize_hotkey_slot(parts[3], path, line_no)

    if not drive_raw.isdigit():
        fail(path, line_no, f"drive token must be numeric: {drive_raw!r}")
    drive = int(drive_raw, 10)
    if drive < 8 or drive > 11:
        fail(path, line_no, f"drive must be 8..11: {drive}")

    prg = normalize_prg_token(prg_raw, path, line_no)

    if not label:
        fail(path, line_no, "display name is empty")
    if len(label) > 31:
        fail(path, line_no, f"display name too long ({len(label)} > 31)")

    return drive, prg, label, slot


def validate_lower_text(text: str, path: str, line_no: int, label: str) -> None:
    if has_upper_ascii(text):
        fail(path, line_no, f"{label} must be lowercase: {text!r}")


def parse_key_value(line: str, path: str, line_no: int) -> Tuple[str, str]:
    if "=" not in line:
        fail(path, line_no, f"expected key=value line: {line!r}")
    key, value = [part.strip() for part in line.split("=", 1)]
    if not key:
        fail(path, line_no, f"empty key in line: {line!r}")
    validate_lower_text(key, path, line_no, "key")
    return key, value


def parse_source(path: str) -> Tuple[Dict[str, str], Dict[str, str], List[Tuple[str, str]]]:
    with open(path, "r", encoding="utf-8", errors="strict") as f:
        raw_lines = f.read().splitlines()

    system_cfg: Dict[str, str] = {
        "variant_name": "readyos",
        "variant_boot_name": "",
    }
    launcher_cfg: Dict[str, str] = {
        "load_all_to_reu": "0",
        "runappfirst": "",
    }
    apps: List[Tuple[str, str]] = []

    section: Optional[str] = None
    pending_entry: Optional[str] = None
    pending_entry_line = 0

    for idx, raw in enumerate(raw_lines, start=1):
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith(";"):
            continue
        validate_lower_text(line, path, idx, "source text")

        if line.startswith("[") and line.endswith("]"):
            if pending_entry is not None:
                fail(path, pending_entry_line, "missing description line before next section")
            section = line[1:-1].strip()
            if section not in VALID_SECTIONS:
                fail(path, idx, f"unknown section: {line!r}")
            continue

        if section is None:
            fail(path, idx, "content before first section")

        if section == SECTION_SYSTEM:
            key, value = parse_key_value(line, path, idx)
            if key in system_cfg:
                system_cfg[key] = value
            continue

        if section == SECTION_LAUNCHER:
            key, value = parse_key_value(line, path, idx)
            if key == "load_all_to_reu":
                if value not in ("0", "1"):
                    fail(path, idx, "load_all_to_reu must be 0 or 1")
                launcher_cfg[key] = value
            elif key == "runappfirst":
                if value:
                    launcher_cfg[key] = normalize_prg_token(value, path, idx)
                else:
                    launcher_cfg[key] = ""
            continue

        if section == SECTION_APPS:
            if pending_entry is None:
                drive, prg, label, slot = parse_app_entry(line, path, idx)
                entry = f"{drive}:{prg}:{label}"
                if slot:
                    entry += f":{slot}"
                pending_entry = entry
                pending_entry_line = idx
            else:
                if len(line) > 38:
                    fail(path, idx, f"description too long ({len(line)} > 38)")
                apps.append((pending_entry, line))
                pending_entry = None
                pending_entry_line = 0

    if pending_entry is not None:
        fail(path, pending_entry_line, "missing description line")
    if not apps:
        raise ValueError(f"{path}: no app entries found")
    if len(apps) > 23:
        raise ValueError(f"{path}: too many entries ({len(apps)} > 23)")
    return system_cfg, launcher_cfg, apps


def render_lines(system_cfg: Dict[str, str],
                 launcher_cfg: Dict[str, str],
                 apps: List[Tuple[str, str]]) -> List[str]:
    lines = [
        "[system]",
        f"variant_name={system_cfg['variant_name']}",
        f"variant_boot_name={system_cfg['variant_boot_name']}",
        "[launcher]",
        f"load_all_to_reu={launcher_cfg['load_all_to_reu']}",
        f"runappfirst={launcher_cfg['runappfirst']}",
        "[apps]",
    ]
    for entry, desc in apps:
        lines.append(entry)
        lines.append(desc)
    return lines


def resolve_boot_variant(system_cfg: Dict[str, str]) -> str:
    variant = system_cfg.get("variant_boot_name", "")
    if variant:
        return variant
    return system_cfg.get("variant_name", "")


def asm_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def write_variant_asm(path: str, variant_text: str) -> None:
    out_dir = os.path.dirname(path)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(path, "w", encoding="utf-8", errors="strict") as f:
        f.write("; Auto-generated by build_apps_catalog_petscii.py. Do not edit by hand.\n\n")
        f.write("msg_variant:\n")
        f.write(f'    .byte "{asm_escape(variant_text)}"\n')
        f.write("msg_variant_end:\n")


def petscii_lower_byte(ch: str, path: str, line_no: int) -> int:
    code = ord(ch)
    if ch == "\r" or ch == "\n":
        return 13
    if "a" <= ch <= "z":
        return ord(ch.upper())
    if "A" <= ch <= "Z":
        fail(path, line_no, f"unexpected uppercase letter in encoding pass: {ch!r}")
    if 32 <= code <= 126:
        return code
    fail(path, line_no, f"unsupported character U+{code:04X} ({ch!r})")
    return 0


def encode_petscii_lower(lines: List[str], path: str) -> bytes:
    out = bytearray()
    for i, line in enumerate(lines, start=1):
        for ch in line:
            out.append(petscii_lower_byte(ch, path, i))
        out.append(13)
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description="Build ReadyOS apps.cfg PETASCII payload")
    ap.add_argument("--input", required=True, help="Sectioned config text source")
    ap.add_argument("--output", required=True, help="Output binary payload")
    ap.add_argument("--variant-asm-output",
                    help="Optional output asm include for boot variant text")
    ap.add_argument("--override-load-all", choices=("0", "1"),
                    help="Override launcher load_all_to_reu")
    ap.add_argument("--override-run-first",
                    help="Override launcher runappfirst prg token")
    args = ap.parse_args()

    try:
        system_cfg, launcher_cfg, apps = parse_source(args.input)
        if args.override_load_all is not None:
            launcher_cfg["load_all_to_reu"] = args.override_load_all
        if args.override_run_first is not None:
            launcher_cfg["runappfirst"] = normalize_prg_token(args.override_run_first,
                                                               "<override>", 0)
        lines = render_lines(system_cfg, launcher_cfg, apps)
        variant_text = resolve_boot_variant(system_cfg)
        payload = encode_petscii_lower(lines, args.input)
    except ValueError as ex:
        print(f"error: {ex}", file=sys.stderr)
        return 1

    out_dir = os.path.dirname(args.output)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(payload)

    if args.variant_asm_output:
        write_variant_asm(args.variant_asm_output, variant_text)

    print(f"wrote {args.output} ({len(payload)} bytes, {len(apps)} entries)")
    if args.variant_asm_output:
        print(f"wrote {args.variant_asm_output} ({len(variant_text)} chars)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
