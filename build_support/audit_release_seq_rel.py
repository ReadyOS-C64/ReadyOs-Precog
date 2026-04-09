#!/usr/bin/env python3
"""
Audit packaged ReadyOS release images for expected SEQ/REL support assets.

This reads each built disk image with c1541, extracts every expected SEQ/REL
payload, and compares it byte-for-byte against the source of truth:

- generated apps.cfg from the selected profile catalog
- authoritative support files under cfg/authoritative/
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
sys.path.insert(0, str(ROOT / "build_support"))

import build_apps_catalog_petscii as apps_catalog
import readyos_profiles


LINE_RE = re.compile(r'^\s*\d+\s+"([^"]+)"\s+([a-zA-Z]+)')


def fail(message: str) -> None:
    raise ValueError(message)


def read_c1541_listing(disk_path: Path) -> dict[str, dict[str, str]]:
    proc = subprocess.run(
        ["c1541", str(disk_path), "-list"],
        text=True,
        capture_output=True,
        check=False,
    )
    if proc.returncode != 0:
        fail(f"c1541 list failed for {disk_path}: {proc.stderr.strip()}")

    entries: dict[str, dict[str, str]] = {}
    for line in proc.stdout.splitlines():
        match = LINE_RE.match(line)
        if not match:
            continue
        raw_name = match.group(1).strip()
        entries[raw_name.lower()] = {
            "type": match.group(2).strip().lower(),
            "raw_name": raw_name,
        }
    return entries


def read_c1541_file_bytes(disk_path: Path, spec: str) -> bytes:
    with tempfile.NamedTemporaryFile(prefix="readyos_audit_", suffix=".bin", delete=False) as tf:
        out_path = Path(tf.name)
    try:
        proc = subprocess.run(
            ["c1541", str(disk_path), "-read", spec, str(out_path)],
            text=True,
            capture_output=True,
            check=False,
        )
        if proc.returncode != 0:
            fail(f"c1541 read failed for {disk_path}:{spec}: {proc.stderr.strip()}")
        return out_path.read_bytes()
    finally:
        out_path.unlink(missing_ok=True)


def expected_apps_cfg_bytes(catalog_source: Path) -> bytes:
    system_cfg, launcher_cfg, apps = apps_catalog.parse_source(str(catalog_source))
    return apps_catalog.encode_petscii_lower(
        apps_catalog.render_lines(system_cfg, launcher_cfg, apps),
        str(catalog_source),
    )


def expected_disk_assets(profile: dict[str, object], manifest: dict[str, object]) -> dict[int, dict[str, dict[str, object]]]:
    catalog_source = Path(str(manifest["catalog_source"]))
    catalog_entries = readyos_profiles.parse_catalog_entries(profile, str(catalog_source))
    apps_set = readyos_profiles.enabled_apps(catalog_entries)
    disks_by_drive = {int(disk["drive"]): disk for disk in manifest["disks"]}
    expected: dict[int, dict[str, dict[str, object]]] = {
        int(disk["index"]): {} for disk in manifest["disks"]
    }

    if 8 not in disks_by_drive:
        fail(f"{profile['id']}: no drive 8 disk present for apps.cfg")

    drive8_index = int(disks_by_drive[8]["index"])
    expected[drive8_index]["apps.cfg"] = {
        "type": "seq",
        "bytes": expected_apps_cfg_bytes(catalog_source),
        "source": "generated apps.cfg",
        "disk_name": "apps.cfg",
    }

    for support_entry in readyos_profiles.authoritative_support_entries(apps_set):
        target_drive = readyos_profiles.support_target_drive(profile, support_entry, catalog_entries)
        disk_index = int(disks_by_drive[target_drive]["index"])
        source_path = readyos_profiles.authoritative_support_path(support_entry)
        expected[disk_index][str(support_entry["disk_name"]).lower()] = {
            "type": str(support_entry["type"]).lower(),
            "bytes": source_path.read_bytes(),
            "source": source_path.name,
            "disk_name": str(support_entry["disk_name"]),
        }

    return expected


def audit_profile(profile_id: str) -> bool:
    profile = readyos_profiles.load_profile(profile_id)
    manifest = readyos_profiles.resolve_profile(profile_id, None, latest=True)
    expected = expected_disk_assets(profile, manifest)
    all_ok = True

    print(f"PROFILE {profile_id} ({manifest['version_text']})")
    for disk in manifest["disks"]:
        disk_index = int(disk["index"])
        disk_path = Path(str(disk["path"]))
        actual_listing = read_c1541_listing(disk_path)
        actual_seqrel = {
            name: meta for name, meta in actual_listing.items()
            if meta["type"] in {"seq", "rel"}
        }
        expected_seqrel = expected[disk_index]

        actual_names = set(actual_seqrel)
        expected_names = set(expected_seqrel)
        missing = sorted(expected_names - actual_names)
        unexpected = sorted(actual_names - expected_names)
        if missing or unexpected:
            all_ok = False

        print(f"  disk {disk_index} drive {disk['drive']}: {disk_path.name}")
        print(f"    expected: {sorted(expected_names)}")
        print(f"    actual:   {sorted(actual_names)}")
        if missing:
            print(f"    MISSING: {missing}")
        if unexpected:
            print(f"    EXTRA:   {unexpected}")

        for name in sorted(expected_names):
            exp = expected_seqrel[name]
            actual = actual_seqrel.get(name)
            if actual is None:
                continue

            actual_type = str(actual["type"])
            expected_type = str(exp["type"])
            if actual_type != expected_type:
                all_ok = False
                print(f"    TYPE MISMATCH {name}: actual={actual_type} expected={expected_type}")
                continue

            raw_name = str(actual["raw_name"])
            spec = f"{raw_name},l" if expected_type == "rel" else f"{raw_name},s"
            actual_bytes = read_c1541_file_bytes(disk_path, spec)
            match = actual_bytes == exp["bytes"]
            print(f"    {name}: {'OK' if match else 'MISMATCH'} ({len(actual_bytes)} bytes vs {exp['source']})")
            if not match:
                all_ok = False

    print("")
    return all_ok


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--profile",
        action="append",
        dest="profiles",
        help="release profile to audit; repeat to audit multiple profiles",
    )
    args = ap.parse_args()

    profiles = args.profiles or readyos_profiles.list_profile_ids()
    all_ok = True
    for profile_id in profiles:
        ok = audit_profile(profile_id)
        all_ok &= ok

    if not all_ok:
        print("SEQ/REL release audit failed.")
        return 1

    print("ALL_SEQ_REL_AUDITS_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
