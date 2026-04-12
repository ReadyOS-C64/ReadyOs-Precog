#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any, Dict, Optional


KEY_STEPS = [
    "launch",
    "overlay_done",
    "wait_readyshell_prompt",
    "verify_1_echo",
    "check_prompt_post_input",
    "capture_post_input_screen",
    "dump_post_input_memory",
    "dump_post_input_reu",
    "debug_post_input_regs",
]


def _read_json(path: pathlib.Path) -> Dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def _step_map(manifest: Dict[str, Any]) -> Dict[str, Dict[str, Any]]:
    out: Dict[str, Dict[str, Any]] = {}
    for step in manifest.get("steps", []):
        sid = step.get("step_id")
        if isinstance(sid, str):
            out[sid] = step
    return out


def _read_text_if(path: pathlib.Path) -> Optional[str]:
    if not path.exists():
        return None
    return path.read_text(encoding="utf-8-sig", errors="replace")


def _parse_regs(text: str) -> Dict[int, int]:
    regs: Dict[int, int] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith("id=") or "value=0x" not in line:
            continue
        try:
            left, right = line.split(" value=0x", 1)
            rid = int(left.split("=", 1)[1], 10)
            val = int(right, 16)
            regs[rid] = val
        except Exception:
            continue
    return regs


def _is_printable(b: int) -> bool:
    return 32 <= b <= 126


def _decode_asciiish(data: bytes) -> str:
    chars = []
    for b in data:
        c = b & 0x7F
        chars.append(chr(c) if _is_printable(c) else ".")
    return "".join(chars)


def _inspect_overlay_sample(data: bytes, start: int) -> Optional[Dict[str, Any]]:
    end = start + 0x40
    if len(data) < end:
        return None
    sample = data[start:end]
    ascii_sample = _decode_asciiish(sample)
    return {
        "start": start,
        "sample_hex": sample[:32].hex(" "),
        "sample_ascii": ascii_sample[:64],
        "basic_signature": "FILE NOT FOUN" in ascii_sample,
    }


def _mem_inspect(mem_path: pathlib.Path) -> Dict[str, Any]:
    out: Dict[str, Any] = {}
    if not mem_path.exists():
        out["present"] = False
        return out

    data = mem_path.read_bytes()
    out["present"] = True

    if len(data) >= 0xC7F1:
        head = data[0xC7F0]
        ring = data[0xC7A0:0xC7E0]
        out["ring_head"] = head
        out["ring_ascii"] = _decode_asciiish(ring)
        if 0 <= head <= len(ring):
            seq = ring[:head]
            seq_ascii = _decode_asciiish(seq)
            out["ring_tail_to_head"] = seq_ascii
            out["ring_tail_to_head_hex"] = seq.hex(" ")
            out["ring_flags"] = {
                "reu_probe_ok": "Q" in seq_ascii,
                "reu_probe_fail": "q" in seq_ascii,
                "cache_phase_entered": "C" in seq_ascii,
                "cache_verify_failed": "!Y" in seq_ascii,
                "cache_committed": "c" in seq_ascii,
            }

    overlay_profiles: Dict[str, Dict[str, Any]] = {}
    release_sample = _inspect_overlay_sample(data, 0x8E00)
    debug_sample = _inspect_overlay_sample(data, 0x8B00)
    if release_sample:
        overlay_profiles["release"] = release_sample
    if debug_sample:
        overlay_profiles["debug"] = debug_sample
    if overlay_profiles:
        out["overlay_profiles"] = overlay_profiles
        rel_basic = overlay_profiles.get("release", {}).get("basic_signature")
        dbg_basic = overlay_profiles.get("debug", {}).get("basic_signature")
        if rel_basic is False and dbg_basic is True:
            out["overlay_profile_inferred"] = "release"
        elif dbg_basic is False and rel_basic is True:
            out["overlay_profile_inferred"] = "debug"
        elif rel_basic is True and dbg_basic is True:
            out["overlay_profile_inferred"] = "basic_or_unmapped"
        else:
            out["overlay_profile_inferred"] = "unknown"
        # Backward-compatible keys for callers that still read single-base output.
        out["overlay_sample_hex"] = overlay_profiles.get("release", {}).get("sample_hex")
        out["overlay_sample_ascii"] = overlay_profiles.get("release", {}).get("sample_ascii")
        out["basic_signature"] = overlay_profiles.get("release", {}).get("basic_signature")

    return out


def _screen_mode(decoded_text: Optional[str]) -> str:
    if not decoded_text:
        return "missing"
    up = decoded_text.upper()
    if "READY." in up:
        return "basic_ready"
    if "RS>" in up:
        return "readyshell_prompt"
    return "other"


def _collect(run_root: pathlib.Path) -> Dict[str, Any]:
    manifest_path = run_root / "manifest.json"
    if not manifest_path.exists():
        raise FileNotFoundError(f"manifest missing: {manifest_path}")

    manifest = _read_json(manifest_path)
    steps = _step_map(manifest)

    post_prompt = _read_text_if(run_root / "screen_decoded" / "post_prompt.txt")
    post_type1 = _read_text_if(run_root / "screen_decoded" / "post_type1.txt")
    post_input = _read_text_if(run_root / "screen_decoded" / "post_input.txt")

    regs_txt = _read_text_if(run_root / "stages" / "debug_post_input_regs" / "monitor_command.txt")
    regs = _parse_regs(regs_txt or "")

    mem = _mem_inspect(run_root / "dumps" / "mem_post_input.bin")

    return {
        "run_root": str(run_root),
        "status": manifest.get("status"),
        "failed_step": manifest.get("failed_step"),
        "degraded_steps": manifest.get("degraded_steps") or [],
        "steps": steps,
        "screens": {
            "post_prompt_mode": _screen_mode(post_prompt),
            "post_type1_mode": _screen_mode(post_type1),
            "post_input_mode": _screen_mode(post_input),
        },
        "regs": regs,
        "mem": mem,
    }


def _print_summary(summary: Dict[str, Any]) -> None:
    print(f"run_root: {summary['run_root']}")
    print(f"status: {summary['status']}")
    print(f"failed_step: {summary['failed_step']}")
    print("degraded_steps: " + ", ".join(summary["degraded_steps"]) if summary["degraded_steps"] else "degraded_steps: <none>")

    print("key_steps:")
    for sid in KEY_STEPS:
        st = summary["steps"].get(sid)
        if not st:
            print(f"  - {sid}: <missing>")
            continue
        err = st.get("error")
        if err:
            code = err.get("code") if isinstance(err, dict) else None
            msg = err.get("message") if isinstance(err, dict) else str(err)
            print(f"  - {sid}: {st.get('status')} ({code}: {msg})")
        else:
            print(f"  - {sid}: {st.get('status')}")

    scr = summary["screens"]
    print("screens:")
    print(f"  - post_prompt: {scr['post_prompt_mode']}")
    print(f"  - post_type1: {scr['post_type1_mode']}")
    print(f"  - post_input: {scr['post_input_mode']}")

    regs = summary["regs"]
    if regs:
        pc = regs.get(3)
        sp = regs.get(4)
        sr = regs.get(5)
        print("registers:")
        print(f"  - pc(id=3): 0x{pc:04X}" if isinstance(pc, int) else "  - pc(id=3): <missing>")
        print(f"  - sp(id=4): 0x{sp:04X}" if isinstance(sp, int) else "  - sp(id=4): <missing>")
        print(f"  - sr(id=5): 0x{sr:04X}" if isinstance(sr, int) else "  - sr(id=5): <missing>")

    mem = summary["mem"]
    if mem.get("present"):
        print("memory:")
        if "ring_head" in mem:
            print(f"  - ring_head($C7F0): {mem['ring_head']}")
        if "ring_tail_to_head" in mem:
            print(f"  - ring_seq($C7A0..head): {mem['ring_tail_to_head']}")
        if "ring_tail_to_head_hex" in mem:
            print(f"  - ring_seq_hex: {mem['ring_tail_to_head_hex']}")
        flags = mem.get("ring_flags") or {}
        if flags:
            print(
                "  - ring_flags: "
                f"reu_ok={flags.get('reu_probe_ok')} "
                f"reu_fail={flags.get('reu_probe_fail')} "
                f"cache_phase={flags.get('cache_phase_entered')} "
                f"cache_verify_fail={flags.get('cache_verify_failed')} "
                f"cache_commit={flags.get('cache_committed')}"
            )
        if "overlay_profile_inferred" in mem:
            print(f"  - overlay_profile_inferred: {mem['overlay_profile_inferred']}")
        ovl = mem.get("overlay_profiles") or {}
        for profile in ("release", "debug"):
            row = ovl.get(profile)
            if not row:
                continue
            print(
                f"  - overlay_{profile}_signature(${row['start']:04X}): {row['basic_signature']}"
            )
            print(
                f"  - overlay_{profile}_sample_ascii: {row['sample_ascii']}"
            )


def _print_compare(new: Dict[str, Any], old: Dict[str, Any]) -> None:
    print("compare:")
    print(f"  - baseline: {old['run_root']}")
    print(f"  - status: {old['status']} -> {new['status']}")
    print(f"  - post_input_mode: {old['screens']['post_input_mode']} -> {new['screens']['post_input_mode']}")

    for sid in KEY_STEPS:
        n = new["steps"].get(sid, {}).get("status")
        o = old["steps"].get(sid, {}).get("status")
        if n != o:
            print(f"  - step:{sid}: {o} -> {n}")

    npc = new["regs"].get(3)
    opc = old["regs"].get(3)
    if npc != opc:
        npcs = f"0x{npc:04X}" if isinstance(npc, int) else "<missing>"
        opcs = f"0x{opc:04X}" if isinstance(opc, int) else "<missing>"
        print(f"  - pc(id=3): {opcs} -> {npcs}")

    nring = new["mem"].get("ring_tail_to_head")
    oring = old["mem"].get("ring_tail_to_head")
    if nring != oring:
        print(f"  - ring_seq: {oring} -> {nring}")


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Summarize .NET ViceTasks artifacts from a vice_auto run")
    ap.add_argument("--run-root", required=True, help="Path to logs/vice_auto_* run folder")
    ap.add_argument("--compare-root", help="Optional baseline logs/vice_auto_* run folder")
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    run_root = pathlib.Path(args.run_root).resolve()

    try:
        summary = _collect(run_root)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    _print_summary(summary)

    if args.compare_root:
        try:
            baseline = _collect(pathlib.Path(args.compare_root).resolve())
            _print_compare(summary, baseline)
        except Exception as exc:
            print(f"compare_error: {exc}", file=sys.stderr)
            return 3

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
