#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


SAMPLES = {
    "bang": bytes((65, 33, 66, 13)),
    "pipe": bytes((65, 124, 66, 13)),
    "vline": bytes((65, 221, 66, 13)),
}


def write_sample(path: str, payload: bytes) -> None:
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(payload)
    print(f"wrote {out} ({len(payload)} bytes)")


def main() -> int:
    ap = argparse.ArgumentParser(description="Build xtextchk sample SEQ payloads")
    ap.add_argument("--bang-output", required=True)
    ap.add_argument("--pipe-output", required=True)
    ap.add_argument("--vline-output", required=True)
    args = ap.parse_args()

    write_sample(args.bang_output, SAMPLES["bang"])
    write_sample(args.pipe_output, SAMPLES["pipe"])
    write_sample(args.vline_output, SAMPLES["vline"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
