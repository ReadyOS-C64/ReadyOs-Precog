#!/usr/bin/env python3
"""
simplefiles_host_smoke.py

Static integration checks for the simple files app wiring.
"""

from pathlib import Path
import json
import sys


ROOT = Path(__file__).resolve().parents[1]


def fail(msg: str) -> int:
    print(f"[FAIL] {msg}")
    return 1


def main() -> int:
    catalog = (ROOT / "cfg" / "apps_catalog.txt").read_text(encoding="utf-8")
    if "9:simplefiles:simple files" not in catalog:
        return fail("apps_catalog.txt missing simplefiles entry")

    spec = json.loads((ROOT / "build_support" / "memory_map_spec.json").read_text(encoding="utf-8"))
    if "obj/simplefiles.map" not in spec.get("map_files", []):
        return fail("memory_map_spec.json missing obj/simplefiles.map")

    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    required = [
        "SIMPLEFILES = simplefiles.prg",
        "$(SIMPLEFILES): $(APPS_DIR)/simplefiles/simplefiles.c $(LIB_SIMPLEFILES)",
        "-write $(SIMPLEFILES) simplefiles",
    ]
    for marker in required:
        if marker not in makefile:
            return fail(f"Makefile missing marker: {marker}")

    print("[OK] simplefiles wiring smoke")
    return 0


if __name__ == "__main__":
    sys.exit(main())
