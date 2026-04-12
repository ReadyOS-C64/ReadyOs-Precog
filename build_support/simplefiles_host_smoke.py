#!/usr/bin/env python3
"""
simplefiles_host_smoke.py

Static integration checks for the simple files app wiring.
"""

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def fail(msg: str) -> int:
    print(f"[FAIL] {msg}")
    return 1


def profile_contains_simplefiles(profile_path: Path) -> bool:
    payload = json.loads(profile_path.read_text(encoding="utf-8"))
    for disk in payload.get("disks", []):
        for entry in disk.get("contents", []):
            if entry.get("artifact") == "simplefiles.prg" and entry.get("name") == "simplefiles":
                return True
    return False


def main() -> int:
    catalog = (ROOT / "cfg" / "readyos_config.ini").read_text(encoding="utf-8")
    if "9:simplefiles:simple files" not in catalog:
        return fail("readyos_config.ini missing simplefiles entry")

    spec = json.loads((ROOT / "build_support" / "memory_map_spec.json").read_text(encoding="utf-8"))
    if "obj/simplefiles.map" not in spec.get("map_files", []):
        return fail("memory_map_spec.json missing obj/simplefiles.map")

    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    required = [
        "SIMPLEFILES = simplefiles.prg",
        "$(SIMPLEFILES): $(APPS_DIR)/simplefiles/simplefiles.c $(LIB_SIMPLEFILES)",
    ]
    for marker in required:
        if marker not in makefile:
            return fail(f"Makefile missing marker: {marker}")

    profile_tool = (ROOT / "build_support" / "readyos_profiles.py").read_text(encoding="utf-8")
    if '"simplefiles"' not in profile_tool:
        return fail("readyos_profiles.py missing simplefiles app registration")

    authoritative_profile = ROOT / "cfg" / "profiles" / "precog-dual-d71.json"
    if not profile_contains_simplefiles(authoritative_profile):
        return fail("precog-dual-d71 profile missing simplefiles packaging entry")

    print("[OK] simplefiles wiring smoke")
    return 0


if __name__ == "__main__":
    sys.exit(main())
