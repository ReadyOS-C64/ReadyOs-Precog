#!/usr/bin/env python3
"""
tasklist_host_smoke.py

Focused host-side checks for ReadyOS Tasklist:
- Tasklist source contracts for shared resume, note search, and help popup
- Runtime headroom gate for obj/tasklist.map
- File-format, clipboard, and search-spec smoke tests
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TASKLIST_SRC = ROOT / "src" / "apps" / "tasklist" / "tasklist.c"
TASKLIST_MAP = ROOT / "obj" / "tasklist.map"
APP_SNAPSHOT_END = 0xC5FF
MIN_HEADROOM = 1024


def check(name: str, cond: bool, detail: str = "") -> bool:
    status = "OK" if cond else "FAIL"
    msg = f"[{status}] {name}"
    if detail:
        msg += f" - {detail}"
    print(msg)
    return cond


def parse_map_segments(path: Path) -> dict[str, tuple[int, int, int]]:
    txt = path.read_text(encoding="utf-8", errors="replace")
    seg_re = re.compile(
        r"^\s*([A-Z0-9_]+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+[0-9A-F]{5}\s*$",
        flags=re.MULTILINE,
    )
    segments: dict[str, tuple[int, int, int]] = {}
    in_list = False
    for line in txt.splitlines():
        if line.strip() == "Segment list:":
            in_list = True
            continue
        if not in_list:
            continue
        m = seg_re.match(line)
        if m:
            segments[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16))
            continue
        if segments and not line.strip():
            break
    if not segments:
        raise ValueError(f"segment list missing: {path}")
    return segments


def parse_tasklist_text(text: str) -> list[dict[str, object]]:
    tasks: list[dict[str, object]] = []
    for raw in text.splitlines():
        if not raw:
            continue
        if raw.startswith(">"):
            if not tasks:
                raise ValueError("note line without preceding task")
            notes = tasks[-1]["notes"]
            assert isinstance(notes, list)
            notes.append(raw[1:])
            continue
        if len(raw) < 3 or raw[2] != " ":
            raise ValueError(f"malformed task line: {raw!r}")
        tasks.append(
            {
                "indent": int(raw[0]),
                "done": int(raw[1]),
                "text": raw[3:],
                "notes": [],
            }
        )
    return tasks


def serialize_tasklist(tasks: list[dict[str, object]]) -> str:
    lines: list[str] = []
    for task in tasks:
        lines.append(f"{task['indent']}{task['done']} {task['text']}")
        for note in task["notes"]:
            lines.append(f">{note}")
    return "\r".join(lines) + "\r"


def split_clipboard_payload(payload: str) -> tuple[str, str | None]:
    sep = "\n---\n"
    if sep not in payload:
        return payload, None
    task_text, note_text = payload.split(sep, 1)
    return task_text, note_text


def text_contains(haystack: str, needle: str) -> bool:
    return needle.lower() in haystack.lower()


def matches_query(task_text: str, note_text: str, query: str) -> bool:
    for raw_word in query.split():
        if raw_word.startswith("#"):
            needle = raw_word
        else:
            needle = raw_word
        if not text_contains(task_text, needle) and not text_contains(note_text, needle):
            return False
    return True


def main() -> int:
    ok = True

    print("=== Tasklist Source Contract ===")
    src = TASKLIST_SRC.read_text(encoding="utf-8", errors="replace")
    ok &= check("tasklist includes resume_state", '#include "../../lib/resume_state.h"' in src)
    ok &= check("tasklist initializes shared resume", "resume_init_for_app(" in src)
    ok &= check("tasklist saves resume segments", "resume_save_segments(" in src)
    ok &= check("tasklist restores resume segments", "resume_load_segments(" in src)
    ok &= check("tasklist has note search helper", "note_contains(" in src)
    ok &= check("tasklist has help popup", "show_help_popup(" in src)

    print("\n=== Tasklist Headroom Gate ===")
    try:
        segments = parse_map_segments(TASKLIST_MAP)
        runtime_end = max(
            segments[name][1]
            for name in ("STARTUP", "CODE", "RODATA", "DATA", "INIT", "ONCE", "BSS")
            if name in segments
        )
        headroom = APP_SNAPSHOT_END - runtime_end
        ok &= check("tasklist runtime headroom", headroom >= MIN_HEADROOM, f"{headroom} bytes")
    except Exception as ex:  # pragma: no cover - failure path only
        ok &= check("tasklist map parse", False, str(ex))

    print("\n=== Tasklist Format Smoke ===")
    sample_tasks = [
        {"indent": 0, "done": 0, "text": "Inbox", "notes": ["call mom", "tag #home"]},
        {"indent": 1, "done": 1, "text": "Ship release #work", "notes": []},
    ]
    encoded = serialize_tasklist(sample_tasks)
    decoded = parse_tasklist_text(encoded.replace("\r", "\n"))
    ok &= check("tasklist format roundtrip", decoded == sample_tasks)

    print("\n=== Clipboard Smoke ===")
    task_text, note_text = split_clipboard_payload("Review PR\n---\nLine 1\n#tag")
    ok &= check("clipboard splits task text", task_text == "Review PR", repr(task_text))
    ok &= check("clipboard splits note text", note_text == "Line 1\n#tag", repr(note_text))
    plain_text, plain_note = split_clipboard_payload("Plain task")
    ok &= check("clipboard plain payload preserved", plain_text == "Plain task", repr(plain_text))
    ok &= check("clipboard plain payload has no note", plain_note is None, repr(plain_note))

    print("\n=== Search Smoke ===")
    ok &= check("plain term matches note text", matches_query("alpha", "beta gamma", "gamma"))
    ok &= check("hashtag term matches note text", matches_query("alpha", "beta #home", "#home"))
    ok &= check("mixed query requires all terms", matches_query("ship release", "note #work", "ship #work"))
    ok &= check("missing term rejects match", not matches_query("ship release", "note #work", "ship #home"))

    print("\nALL CHECKS PASSED" if ok else "\nSOME CHECKS FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
