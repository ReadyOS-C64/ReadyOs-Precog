#!/usr/bin/env python3
"""
ReadyOS profile loader and release packager.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List

import build_apps_catalog_petscii as apps_catalog


ROOT = Path(__file__).resolve().parents[1]
PROFILES_DIR = ROOT / "cfg" / "profiles"
RELEASES_DIR = ROOT / "releases"
LEGACY_RELEASE_DIR = ROOT / "release"
BIN_DIR = ROOT / "bin"
RELEASE_ROOT_README_TEMPLATE = ROOT / "docs" / "release_root_readme_template.md"
AUTHORITATIVE_PROFILE_ID = "precog-dual-d71"
AUTHORITATIVE_DATA_DIR = ROOT / "cfg" / "authoritative"
SYNCABLE_AUTHORITATIVE_INDEX = AUTHORITATIVE_DATA_DIR / "sync_inventory.json"
SYNCABLE_PURGE_INDEX = AUTHORITATIVE_DATA_DIR / "sync_purge_inventory.json"
GITHUB_URL = "https://github.com/ReadyOS-C64/ReadyOs"
MAIN_SITE_URL = "https://readyos64.com"
WIKI_URL = "https://readyos.notion.site"
PUBLIC_VARIANT_ORDER = [
    "precog-dual-d71",
    "precog-d81",
    "precog-dual-d64",
    "precog-solo-d64-a",
    "precog-solo-d64-b",
    "precog-solo-d64-c",
    "precog-solo-d64-d",
    "precog-solo-d64-e",
]
VARIANT_NOTES = {
    "dual-d71": "Default full-content profile for two 1571-class drives and the main local verification target.",
    "d81": "Full-content single-disk profile for 1581 and D81 setups where the whole current app catalog fits on one image.",
    "dual-d64": "Reduced dual-disk profile for 1541-class environments that can mount two D64 images but not higher-capacity media.",
    "solo-d64-a": "Single-D64 subset focused on editor, reference, and dizzy for one-disk-only environments.",
    "solo-d64-b": "Single-D64 productivity subset centered on quicknotes, clipboard, calculator, and files.",
    "solo-d64-c": "Single-D64 planning subset centered on tasklist, calendar, and REU viewer.",
    "solo-d64-d": "Single-D64 experimental subset for simple cells, calculator, 2048, and deminer.",
    "solo-d64-e": "Single-D64 shell-focused subset for readyshell and its overlay payloads in one-disk-only environments.",
}
VARIANT_BEST_FIT = {
    "dual-d71": "C64 Ultimate, Ultimate 64, or VICE setups that can keep two 1571-class drives mounted.",
    "d81": "C64 Ultimate, VICE, or other 1581-capable setups that prefer one full-content image.",
    "dual-d64": "Real or emulated 1541-only setups that can mount two disks but not D71 or D81 media.",
    "solo-d64-a": "THEC64, web emulators, or simple loaders that can mount only one D64 at a time.",
    "solo-d64-b": "THEC64, web emulators, or simple loaders that can mount only one D64 at a time.",
    "solo-d64-c": "THEC64, web emulators, or simple loaders that can mount only one D64 at a time.",
    "solo-d64-d": "THEC64, web emulators, or simple loaders that can mount only one D64 at a time.",
    "solo-d64-e": "THEC64, web emulators, or simple loaders that can mount only one D64 at a time.",
}
REL_SEED_D71_CANDIDATES = [
    ROOT / "readyos0-1-5.d71",
    ROOT.parent / "readyos0-1-5.d71",
    ROOT.parent.parent / "readyos0-1-5.d71",
]
BUILD_OWNED_SUPPORT_FILES = (
    {
        "app": "editor",
        "disk_name": "editor help",
        "repo_name": "editor_help.seq",
        "type": "seq",
        "bootstrap_drive": 8,
        "target_drive": 8,
        "generated_artifact": "obj/editor_help.seq",
    },
    {
        "app": "readyshell",
        "disk_name": "rshelp",
        "repo_name": "rshelp.seq",
        "type": "seq",
        "bootstrap_drive": 8,
        "target_drive": 8,
        "generated_artifact": "obj/rshelp.seq",
    },
    {
        "app": "tasklist",
        "disk_name": "example tasks",
        "repo_name": "example_tasks.seq",
        "type": "seq",
        "bootstrap_drive": 8,
        "target_drive": 8,
        "generated_artifact": "obj/tasklist_sample.seq",
    },
)
LEGACY_SYNCABLE_SUPPORT_FILES = (
    {
        "app": "quicknotes",
        "disk_name": "myquicknotes",
        "repo_name": "myquicknotes.seq",
        "type": "seq",
        "bootstrap_drive": 8,
        "target_drive": 8,
    },
    {
        "app": "clipmgr",
        "disk_name": "CLIPSET1",
        "repo_name": "clipset1.seq",
        "type": "seq",
        "bootstrap_drive": 8,
        "target_drive": 8,
    },
    {
        "app": "clipmgr",
        "disk_name": "CLIPSET3",
        "repo_name": "clipset3.seq",
        "type": "seq",
        "bootstrap_drive": 8,
        "target_drive": 8,
    },
    {
        "app": "simplecells",
        "disk_name": "sheet2",
        "repo_name": "sheet2.seq",
        "type": "seq",
        "bootstrap_drive": 8,
        "target_drive": 8,
    },
    {
        "app": "cal26",
        "disk_name": "cal26.rel",
        "repo_name": "cal26.rel",
        "type": "rel",
        "record_length": 64,
        "bootstrap_drive": 8,
        "target_drive": 8,
    },
    {
        "app": "cal26",
        "disk_name": "cal26cfg.rel",
        "repo_name": "cal26cfg.rel",
        "type": "rel",
        "record_length": 32,
        "bootstrap_drive": 8,
        "target_drive": 8,
    },
    {
        "app": "dizzy",
        "disk_name": "dizzy.rel",
        "repo_name": "dizzy.rel",
        "type": "rel",
        "record_length": 64,
        "bootstrap_drive": 8,
        "target_drive": 8,
    },
    {
        "app": "dizzy",
        "disk_name": "dizzycfg.rel",
        "repo_name": "dizzycfg.rel",
        "type": "rel",
        "record_length": 32,
        "bootstrap_drive": 8,
        "target_drive": 8,
    },
)
C1541_LIST_LINE_RE = re.compile(r'^\s*\d+\s+"([^"]+)"\s+([a-zA-Z]+)')
KNOWN_APP_NAMES = {
    "editor",
    "quicknotes",
    "calcplus",
    "hexview",
    "clipmgr",
    "reuviewer",
    "sysinfo",
    "tasklist",
    "simplefiles",
    "simplecells",
    "game2048",
    "sidetris",
    "deminer",
    "cal26",
    "dizzy",
    "readme",
    "readyshell",
}


def fail(message: str) -> None:
    raise ValueError(message)


def run(cmd: List[str], check: bool = True, capture_output: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=ROOT,
        text=True,
        capture_output=capture_output,
        check=check,
    )


def normalize_syncable_authoritative_entry(entry: Dict[str, object]) -> Dict[str, object]:
    normalized = {
        "disk_name": str(entry["disk_name"]),
        "repo_name": str(entry["repo_name"]),
        "type": str(entry["type"]).lower(),
        "target_drive": int(entry.get("target_drive", 8)),
    }
    app_name = entry.get("app")
    if app_name not in (None, ""):
        normalized["app"] = str(app_name)
    if normalized["type"] == "rel":
        normalized["record_length"] = int(entry["record_length"])
    elif normalized["type"] != "seq":
        fail(f"unsupported syncable authoritative file type: {normalized['type']}")
    return normalized


def default_syncable_authoritative_entries() -> List[Dict[str, object]]:
    return [normalize_syncable_authoritative_entry(entry) for entry in LEGACY_SYNCABLE_SUPPORT_FILES]


def load_syncable_authoritative_entries() -> List[Dict[str, object]]:
    if not SYNCABLE_AUTHORITATIVE_INDEX.exists():
        return default_syncable_authoritative_entries()

    payload = json.loads(SYNCABLE_AUTHORITATIVE_INDEX.read_text(encoding="utf-8"))
    if not isinstance(payload, list):
        fail(f"syncable authoritative inventory must be a JSON list: {SYNCABLE_AUTHORITATIVE_INDEX}")
    return [normalize_syncable_authoritative_entry(entry) for entry in payload]


def write_syncable_authoritative_inventory(entries: List[Dict[str, object]]) -> None:
    ensure_dir(AUTHORITATIVE_DATA_DIR)
    ordered = sorted(
        (normalize_syncable_authoritative_entry(entry) for entry in entries),
        key=lambda item: (str(item["type"]), str(item["repo_name"]).lower()),
    )
    tmp_path = SYNCABLE_AUTHORITATIVE_INDEX.with_suffix(".json.tmp")
    tmp_path.write_text(json.dumps(ordered, indent=2) + "\n", encoding="utf-8")
    tmp_path.replace(SYNCABLE_AUTHORITATIVE_INDEX)


def normalize_syncable_purge_entry(entry: Dict[str, object]) -> Dict[str, object]:
    normalized = {
        "disk_name": str(entry["disk_name"]),
        "type": str(entry["type"]).lower(),
    }
    if normalized["type"] not in {"seq", "rel", "usr"}:
        fail(f"unsupported syncable purge file type: {normalized['type']}")
    return normalized


def load_syncable_purge_entries() -> List[Dict[str, object]]:
    if not SYNCABLE_PURGE_INDEX.exists():
        return []

    payload = json.loads(SYNCABLE_PURGE_INDEX.read_text(encoding="utf-8"))
    if not isinstance(payload, list):
        fail(f"syncable purge inventory must be a JSON list: {SYNCABLE_PURGE_INDEX}")
    return [normalize_syncable_purge_entry(entry) for entry in payload]


def write_syncable_purge_inventory(entries: List[Dict[str, object]]) -> None:
    ensure_dir(AUTHORITATIVE_DATA_DIR)
    ordered = sorted(
        (normalize_syncable_purge_entry(entry) for entry in entries),
        key=lambda item: (str(item["type"]), str(item["disk_name"]).lower()),
    )
    tmp_path = SYNCABLE_PURGE_INDEX.with_suffix(".json.tmp")
    tmp_path.write_text(json.dumps(ordered, indent=2) + "\n", encoding="utf-8")
    tmp_path.replace(SYNCABLE_PURGE_INDEX)


def build_owned_support_entries(apps_set: set[str] | None = None) -> List[Dict[str, object]]:
    entries: List[Dict[str, object]] = []
    for entry in BUILD_OWNED_SUPPORT_FILES:
        if apps_set is not None and str(entry["app"]) not in apps_set:
            continue
        entries.append(dict(entry))
    return entries


def syncable_authoritative_entries(apps_set: set[str] | None = None) -> List[Dict[str, object]]:
    entries: List[Dict[str, object]] = []
    for entry in load_syncable_authoritative_entries():
        app_name = entry.get("app")
        if apps_set is not None and app_name not in (None, "") and str(app_name) not in apps_set:
            continue
        entries.append(dict(entry))
    return entries


def build_owned_excluded_disk_names() -> set[str]:
    names = {"apps.cfg"}
    for entry in BUILD_OWNED_SUPPORT_FILES:
        names.add(str(entry["disk_name"]).lower())
    return names


def parse_c1541_record_length(proc: subprocess.CompletedProcess[str]) -> int | None:
    marker = "record length "
    for text in (proc.stderr, proc.stdout):
        if marker not in text:
            continue
        value = text.split(marker, 1)[1].split()[0]
        try:
            return int(value)
        except ValueError:
            return None
    return None


def list_seq_rel_files(disk_path: Path) -> List[Dict[str, object]]:
    proc = run(["c1541", str(disk_path), "-list"], check=False, capture_output=True)
    if proc.returncode != 0:
        fail(f"c1541 list failed for {disk_path}: {proc.stderr.strip()}")

    entries: List[Dict[str, object]] = []
    for line in proc.stdout.splitlines():
        match = C1541_LIST_LINE_RE.match(line)
        if not match:
            continue
        file_type = match.group(2).strip().lower()
        if file_type not in {"seq", "rel"}:
            continue
        entries.append({
            "disk_name": match.group(1).strip(),
            "type": file_type,
        })
    return entries


def repo_name_from_disk_name(disk_name: str, file_type: str) -> str:
    base = re.sub(r"[^a-z0-9.]+", "_", disk_name.strip().lower()).strip("_")
    if not base:
        fail(f"cannot derive repo filename from disk name: {disk_name!r}")
    suffix = f".{file_type}"
    if not base.endswith(suffix):
        base += suffix
    return base


def list_profile_ids() -> List[str]:
    ids = []
    for path in sorted(PROFILES_DIR.glob("*.json")):
        ids.append(path.stem)
    return ids


def ordered_profile_ids(profile_ids: List[str]) -> List[str]:
    order = {profile_id: index for index, profile_id in enumerate(PUBLIC_VARIANT_ORDER)}
    return sorted(profile_ids, key=lambda item: (order.get(item, len(PUBLIC_VARIANT_ORDER)), item))


def load_profile(profile_id: str) -> Dict[str, object]:
    path = PROFILES_DIR / f"{profile_id}.json"
    if not path.exists():
        fail(f"unknown profile: {profile_id}")
    profile = json.loads(path.read_text(encoding="utf-8"))
    profile["_path"] = str(path)
    return profile


def readyshell_parse_trace_debug(profile: Dict[str, object]) -> int:
    value = int(profile.get("readyshell_parse_trace_debug", 0))
    if value not in (0, 1):
        fail(f"invalid readyshell_parse_trace_debug for profile {profile.get('id', '<unknown>')}: {value}")
    return value


def default_profile_id() -> str:
    for profile_id in list_profile_ids():
        profile = load_profile(profile_id)
        if profile.get("default_run"):
            return profile_id
    fail("no default profile defined")


def profile_catalog_source(profile: Dict[str, object]) -> Path:
    return ROOT / str(profile["catalog_source"])


def public_release_version(version_text: str) -> str:
    if version_text and version_text[-1].isalpha():
        return version_text[:-1]
    return version_text


def release_version_dir(version_text: str) -> Path:
    return RELEASES_DIR / public_release_version(version_text).lower()


def profile_output_dir(profile: Dict[str, object], version_text: str) -> Path:
    return release_version_dir(version_text) / str(profile["id"])


def profile_manifest_path(profile: Dict[str, object], version_text: str) -> Path:
    return profile_output_dir(profile, version_text) / "manifest.json"


def legacy_profile_output_dir(profile: Dict[str, object]) -> Path:
    return LEGACY_RELEASE_DIR / str(profile["id"])


def legacy_profile_manifest_path(profile: Dict[str, object]) -> Path:
    return legacy_profile_output_dir(profile) / "manifest.json"


def read_current_version_text() -> str:
    from update_build_version import read_current_version

    return read_current_version()


def latest_manifest_path(profile_id: str) -> Path:
    profile = load_profile(profile_id)
    current_manifest = profile_manifest_path(profile, read_current_version_text())
    if current_manifest.is_file():
        return current_manifest

    candidates: List[Path] = []
    for path in RELEASES_DIR.glob(f"*/{profile_id}/manifest.json"):
        if path.is_file():
            candidates.append(path)
    if candidates:
        candidates.sort(key=lambda path: path.stat().st_mtime_ns, reverse=True)
        return candidates[0]

    legacy_manifest = legacy_profile_manifest_path(profile)
    if legacy_manifest.is_file():
        return legacy_manifest

    fail(f"no current build manifest for profile: {profile_id}")


def latest_profile_disk_path(profile_id: str, drive: int = 8) -> Path:
    manifest = resolve_profile(profile_id, None, latest=True)
    disks = manifest.get("disks", [])
    if not disks:
        fail(f"no disks in latest manifest for profile: {profile_id}")

    selected = None
    for disk in disks:
        if int(disk.get("drive", 0)) == int(drive):
            selected = disk
            break
    if selected is None:
        selected = disks[0]

    path = Path(str(selected["path"]))
    if not path.is_absolute():
        path = ROOT / path
    return path.resolve()


def release_root_readme_path(version_text: str) -> Path:
    return release_version_dir(version_text) / "README.md"


def public_profile_ids() -> List[str]:
    ids: List[str] = []
    for profile_id in list_profile_ids():
        profile = load_profile(profile_id)
        if readyshell_parse_trace_debug(profile) == 0:
            ids.append(profile_id)
    return ordered_profile_ids(ids)


def debug_profile_ids() -> List[str]:
    ids: List[str] = []
    for profile_id in list_profile_ids():
        profile = load_profile(profile_id)
        if readyshell_parse_trace_debug(profile) == 1:
            ids.append(profile_id)
    return ordered_profile_ids(ids)


def boot_flow_text(preboot_mode: str) -> str:
    if preboot_mode == "setd71":
        return "PREBOOT -> SETD71 -> BOOT"
    return "PREBOOT -> BOOT"


def format_drive_list(drives: List[int]) -> str:
    text = [f"`{drive}`" for drive in drives]
    if len(text) == 1:
        return text[0]
    if len(text) == 2:
        return f"{text[0]} and {text[1]}"
    return ", ".join(text[:-1]) + f", and {text[-1]}"


def media_summary(resolved: Dict[str, object]) -> str:
    disks = list(resolved["disks"])
    drives = [int(disk["drive"]) for disk in disks]
    image_types = {str(disk["image_type"]).upper() for disk in disks}
    if len(disks) == 1:
        image_type = str(disks[0]["image_type"]).upper()
        return f"1x `{image_type}` on drive {format_drive_list(drives)}"
    if len(image_types) == 1:
        image_type = next(iter(image_types))
        return f"{len(disks)}x `{image_type}` on drives {format_drive_list(drives)}"
    return f"{len(disks)} disk images on drives {format_drive_list(drives)}"


def default_catalog_app_count() -> int:
    profile = load_profile(default_profile_id())
    return len(parse_catalog_entries(profile, None))


def build_public_variant_matrix(version_text: str) -> str:
    lines = [
        "| Folder | Media | Best Fit | Why It Exists | Boot |",
        "| --- | --- | --- | --- | --- |",
    ]
    for profile_id in public_profile_ids():
        profile = load_profile(profile_id)
        resolved = resolve_profile(profile_id, version_text, latest=False)
        kind = str(profile["kind"])
        lines.append(
            f"| `{profile_id}` | {media_summary(resolved)} | "
            f"{VARIANT_BEST_FIT.get(kind, 'Profile-specific C64 media target.')} | "
            f"{VARIANT_NOTES.get(kind, 'Profile-specific ReadyOS media layout.')} | "
            f"`{boot_flow_text(str(resolved['preboot_mode']))}` |"
        )
    return "\n".join(lines)


def build_release_folder_list(profile_ids: List[str]) -> str:
    return "\n".join(f"- `{profile_id}/`" for profile_id in profile_ids)


def build_debug_variant_note() -> str:
    ids = debug_profile_ids()
    if not ids:
        return "This release line currently has no separate debug-trace variant folders."
    names = ", ".join(f"`{profile_id}`" for profile_id in ids)
    return (
        "This release line also ships debug-trace variants for ReadyShell development: "
        f"{names}. They are intended for debugging and instrumentation, not as the default end-user choice."
    )


def render_release_root_readme(version_text: str) -> str:
    if not RELEASE_ROOT_README_TEMPLATE.exists():
        fail(f"missing release root README template: {RELEASE_ROOT_README_TEMPLATE}")

    template = RELEASE_ROOT_README_TEMPLATE.read_text(encoding="utf-8")
    public_version = public_release_version(version_text)
    public_ids = public_profile_ids()
    replacements = {
        "PUBLIC_VERSION": public_version,
        "VERSION_TEXT": version_text,
        "GITHUB_URL": GITHUB_URL,
        "MAIN_SITE_URL": MAIN_SITE_URL,
        "WIKI_URL": WIKI_URL,
        "CURRENT_APP_COUNT": str(default_catalog_app_count()),
        "PUBLIC_VARIANT_COUNT": str(len(public_ids)),
        "PUBLIC_VARIANT_FOLDERS": build_release_folder_list(public_ids),
        "PUBLIC_VARIANT_MATRIX": build_public_variant_matrix(version_text),
        "DEBUG_VARIANT_NOTE": build_debug_variant_note(),
    }
    content = template
    for key, value in replacements.items():
        content = content.replace(f"{{{{{key}}}}}", value)

    unresolved = sorted(set(re.findall(r"\{\{[A-Z0-9_]+\}\}", content)))
    if unresolved:
        fail("unresolved release root README template tokens: " + ", ".join(unresolved))
    return content


def write_release_root_readme(version_text: str) -> None:
    version_dir = release_version_dir(version_text)
    ensure_dir(version_dir)
    release_root_readme_path(version_text).write_text(
        render_release_root_readme(version_text).rstrip() + "\n",
        encoding="utf-8",
    )


def build_disk_filename(profile: Dict[str, object], disk: Dict[str, object], version_text: str) -> str:
    kind = str(profile["kind"])
    ext = str(disk["image_type"])
    disks = profile["disks"]
    if len(disks) == 1:
        return f"readyos-v{version_text.lower()}-{kind}.{ext}"
    return f"readyos-v{version_text.lower()}-{kind}_{int(disk['index'])}.{ext}"


def build_boot_prg_filename(profile: Dict[str, object], version_text: str, stem: str) -> str:
    return f"readyos-v{version_text.lower()}-{str(profile['kind'])}-{stem}.prg"


def repo_artifact_path(rel_path: str) -> Path:
    rel = Path(rel_path)
    if rel.suffix.lower() == ".prg" and len(rel.parts) == 1:
        return BIN_DIR / rel
    return ROOT / rel


def boot_prg_specs(profile: Dict[str, object], version_text: str, output_dir: Path) -> List[Dict[str, str]]:
    specs = [
        {
            "stem": "preboot",
            "disk_name": "preboot",
            "source": str(BIN_DIR / "preboot.prg"),
            "path": str(output_dir / build_boot_prg_filename(profile, version_text, "preboot")),
        },
        {
            "stem": "boot",
            "disk_name": "boot",
            "source": str(BIN_DIR / "boot.prg"),
            "path": str(output_dir / build_boot_prg_filename(profile, version_text, "boot")),
        },
    ]
    if str(profile["boot"]["preboot_mode"]) == "setd71":
        specs.append(
            {
                "stem": "setd71",
                "disk_name": "setd71",
                "source": str(BIN_DIR / "setd71.prg"),
                "path": str(output_dir / build_boot_prg_filename(profile, version_text, "setd71")),
            }
        )
    return specs


def resolve_profile(profile_id: str, version_text: str | None, latest: bool) -> Dict[str, object]:
    profile = load_profile(profile_id)

    if latest:
        manifest_path = latest_manifest_path(profile_id)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        return manifest

    if not version_text:
        version_text = read_current_version_text()

    output_dir = profile_output_dir(profile, version_text)
    manifest_path = profile_manifest_path(profile, version_text)

    system_cfg, _launcher_cfg, _apps = apps_catalog.parse_source(str(profile_catalog_source(profile)))
    disks = []
    for disk in profile["disks"]:
        disk_path = output_dir / build_disk_filename(profile, disk, version_text)
        disks.append(
            {
                "index": int(disk["index"]),
                "drive": int(disk["drive"]),
                "image_type": str(disk["image_type"]),
                "vice_drive_type": str(disk["vice_drive_type"]),
                "true_drive": bool(disk.get("true_drive", False)),
                "path": str(disk_path),
            }
        )
    boot_prgs = boot_prg_specs(profile, version_text, output_dir)
    autostart_disk_prg = str(profile["boot"]["autostart_prg"])
    autostart_path = next(item["path"] for item in boot_prgs if item["stem"] == Path(autostart_disk_prg).stem.lower())

    return {
        "id": profile["id"],
        "display_name": profile["display_name"],
        "kind": profile["kind"],
        "readyshell_parse_trace_debug": readyshell_parse_trace_debug(profile),
        "variant_boot_name": system_cfg.get("variant_boot_name", ""),
        "version_text": version_text,
        "catalog_source": str(profile_catalog_source(profile)),
        "output_dir": str(output_dir),
        "manifest_path": str(manifest_path),
        "autostart_prg": str(autostart_path),
        "autostart_disk_prg": autostart_disk_prg,
        "preboot_mode": str(profile["boot"]["preboot_mode"]),
        "disks": disks,
        "boot_prgs": boot_prgs,
    }


def parse_catalog_entries(profile: Dict[str, object], catalog_override: str | None) -> List[Dict[str, object]]:
    source = Path(catalog_override) if catalog_override else profile_catalog_source(profile)
    _system, _launcher, apps = apps_catalog.parse_source(str(source))
    entries = []
    for entry, desc in apps:
        drive, prg, label, _slot = apps_catalog.parse_app_entry(entry, str(source), 1)
        entries.append({
            "drive": drive,
            "prg": prg,
            "label": label,
            "desc": desc,
        })
    return entries


def enabled_apps(entries: List[Dict[str, object]]) -> set[str]:
    return {str(entry["prg"]) for entry in entries}


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def write_preboot(preboot_mode: str, out_path: Path) -> None:
    if preboot_mode == "setd71":
        lines = ['10 load "setd71",8', '20 run']
    elif preboot_mode == "boot":
        lines = ['10 load "boot",8', '20 run']
    else:
        fail(f"unsupported preboot mode: {preboot_mode}")

    with tempfile.NamedTemporaryFile("w", suffix=".bas", delete=False, encoding="ascii") as tf:
        tf.write("\n".join(lines) + "\n")
        temp_path = Path(tf.name)
    try:
        run(["petcat", "-w2", "-o", str(out_path), str(temp_path)])
    finally:
        temp_path.unlink(missing_ok=True)


def rebuild_profile_boot_chain(profile: Dict[str, object], version_text: str) -> None:
    run([sys.executable, str(ROOT / "build_support" / "update_build_version.py"), "--write", version_text])
    targets = ["bin/boot.prg", "bin/preboot.prg"]
    if str(profile["boot"]["preboot_mode"]) == "setd71":
        targets.append("bin/setd71.prg")
    run(["make", "-B", f"PROFILE={profile['id']}", f"READYOS_VERSION_TEXT={version_text}", *targets])


def ensure_generated_assets(profile: Dict[str, object],
                            catalog_source: Path,
                            override_load_all: str | None,
                            override_run_first: str | None) -> None:
    obj_dir = ROOT / "obj"
    ensure_dir(obj_dir)
    ensure_dir(BIN_DIR)

    run([
        sys.executable,
        str(ROOT / "build_support" / "build_apps_catalog_petscii.py"),
        "--input", str(catalog_source),
        "--output", str(obj_dir / "apps_cfg_petscii.seq"),
        "--variant-asm-output", str(ROOT / "src" / "generated" / "msg_variant.inc"),
        *([] if override_load_all is None else ["--override-load-all", override_load_all]),
        *([] if override_run_first is None else ["--override-run-first", override_run_first]),
    ])

    run([
        sys.executable,
        str(ROOT / "build_support" / "build_petscii_lower_seq.py"),
        "--input", str(ROOT / "cfg" / "editor_help.txt"),
        "--output", str(obj_dir / "editor_help.seq"),
    ])
    run([
        sys.executable,
        str(ROOT / "build_support" / "build_petscii_lower_seq.py"),
        "--input", str(ROOT / "cfg" / "rshelp.txt"),
        "--output", str(obj_dir / "rshelp.seq"),
    ])
    run([
        sys.executable,
        str(ROOT / "build_support" / "build_petscii_lower_seq.py"),
        "--input", str(ROOT / "cfg" / "tasklist_sample.txt"),
        "--output", str(obj_dir / "tasklist_sample.seq"),
    ])

    write_preboot(str(profile["boot"]["preboot_mode"]), BIN_DIR / "preboot.prg")
    if str(profile["boot"]["preboot_mode"]) == "setd71":
        run(["petcat", "-w2", "-o", str(BIN_DIR / "setd71.prg"), str(ROOT / "src" / "boot" / "setd71.bas")])


def managed_build_names(profile: Dict[str, object], apps_set: set[str]) -> set[str]:
    managed = {"apps.cfg"}
    for entry in build_owned_support_entries(None):
        managed.add(str(entry["disk_name"]))
    for entry in syncable_authoritative_entries(None):
        managed.add(str(entry["disk_name"]))
    for entry in load_syncable_purge_entries():
        managed.add(str(entry["disk_name"]))
    return {name.lower() for name in managed}


def backup_user_files(disk_path: Path, managed_names: set[str]) -> tuple[Path, Path] | None:
    if not disk_path.exists():
        return None

    stage_dir = Path(tempfile.mkdtemp(prefix="readyos_preserve_"))
    manifest_path = stage_dir / "manifest.tsv"
    listing_path = stage_dir / "listing.txt"
    listing = run(["c1541", str(disk_path), "-list"], check=False, capture_output=True)
    listing_path.write_text(listing.stdout, encoding="ascii")
    manifest_path.write_text("", encoding="ascii")

    idx = 0
    for line in listing.stdout.splitlines():
        if '"' not in line:
            continue
        name = line.split('"')[1]
        parts = line.strip().split()
        if not parts:
            continue
        ftype = parts[-1].lower()
        if ftype not in {"seq", "rel", "usr"}:
            continue
        if name.lower() in managed_names:
            continue

        idx += 1
        host_path = stage_dir / f"file_{idx}.bin"
        if ftype == "rel":
            rel = run(["c1541", str(disk_path), "-read", f"{name},l", str(host_path)],
                      check=False, capture_output=True)
            if not host_path.exists() or host_path.stat().st_size == 0:
                host_path.unlink(missing_ok=True)
                continue
            rec_len = parse_c1541_record_length(rel)
            if rec_len is None:
                host_path.unlink(missing_ok=True)
                continue
            with manifest_path.open("a", encoding="ascii") as fh:
                fh.write(f"{name}\t{ftype}\t{rec_len}\t{host_path}\n")
            continue

        spec = name if ftype == "usr" else f"{name},s"
        read_res = run(["c1541", str(disk_path), "-read", spec, str(host_path)], check=False)
        if read_res.returncode != 0:
            host_path.unlink(missing_ok=True)
            continue
        with manifest_path.open("a", encoding="ascii") as fh:
            fh.write(f"{name}\t{ftype}\t0\t{host_path}\n")
    return stage_dir, manifest_path


def restore_user_files(disk_path: Path, manifest_path: Path) -> None:
    if not manifest_path.exists():
        return
    for line in manifest_path.read_text(encoding="ascii").splitlines():
        if not line.strip():
            continue
        name, ftype, rec_len, host_path = line.split("\t")
        host = Path(host_path)
        if not host.exists():
            continue
        run(["c1541", str(disk_path), "-delete", name], check=False)
        if ftype == "rel":
            spec = f"{name},l,{rec_len}"
        elif ftype == "usr":
            spec = f"{name},u"
        else:
            spec = f"{name},s"
        run(["c1541", str(disk_path), "-write", str(host), spec], check=False)


def resolve_rel_seed_d71() -> Path | None:
    for candidate in REL_SEED_D71_CANDIDATES:
        if candidate.exists():
            return candidate
    return None


def authoritative_support_entries(apps_set: set[str]) -> List[Dict[str, object]]:
    return build_owned_support_entries(apps_set) + syncable_authoritative_entries(apps_set)


def authoritative_support_path(entry: Dict[str, object]) -> Path:
    return AUTHORITATIVE_DATA_DIR / str(entry["repo_name"])


def support_target_drive(profile: Dict[str, object], entry: Dict[str, object],
                         catalog_entries: List[Dict[str, object]]) -> int:
    preferred = int(entry.get("target_drive", 8))
    profile_drives = {int(disk["drive"]) for disk in profile["disks"]}
    if preferred in profile_drives:
        return preferred

    app_name = str(entry["app"])
    for catalog_entry in catalog_entries:
        if str(catalog_entry["prg"]) == app_name:
            return int(catalog_entry["drive"])

    if profile_drives:
        return min(profile_drives)
    fail(f"profile has no drives for support file placement: {profile['id']}")


def extract_disk_file(source_disk: Path, disk_name: str, file_type: str, target_path: Path) -> int | None:
    if file_type == "rel":
        spec = f"{disk_name},l"
    elif file_type == "seq":
        spec = f"{disk_name},s"
    else:
        fail(f"unsupported authoritative file type: {file_type}")
    proc = run(["c1541", str(source_disk), "-read", spec, str(target_path)], check=False,
               capture_output=(file_type == "rel"))
    if not target_path.exists() or target_path.stat().st_size == 0:
        target_path.unlink(missing_ok=True)
        fail(f"missing {file_type.upper()} payload in source disk {source_disk}: {disk_name}")
    if file_type == "rel":
        record_length = parse_c1541_record_length(proc)
        if record_length is None:
            target_path.unlink(missing_ok=True)
            fail(f"missing REL record length in source disk {source_disk}: {disk_name}")
        return record_length
    return None


def resolve_bootstrap_d71_disks() -> Dict[int, Path]:
    disks: Dict[int, Path] = {}

    try:
        manifest = resolve_profile(AUTHORITATIVE_PROFILE_ID, None, latest=True)
    except ValueError:
        manifest = None

    if manifest is not None:
        for disk in manifest.get("disks", []):
            path = Path(str(disk.get("path", "")))
            drive = int(disk.get("drive", 0))
            if path.exists():
                disks[int(disk.get("drive", 0))] = path

    donor = resolve_rel_seed_d71()
    if donor is not None and 8 not in disks:
        disks[8] = donor
    return disks


def bootstrap_authoritative_support_files(apps_set: set[str]) -> None:
    ensure_dir(AUTHORITATIVE_DATA_DIR)
    bootstrap_disks = resolve_bootstrap_d71_disks()
    for entry in authoritative_support_entries(apps_set):
        target_path = authoritative_support_path(entry)
        if target_path.exists():
            continue

        generated_artifact = entry.get("generated_artifact")
        if generated_artifact:
            generated_path = ROOT / str(generated_artifact)
            if generated_path.exists():
                shutil.copyfile(generated_path, target_path)
                continue

        source_disk = bootstrap_disks.get(int(entry["bootstrap_drive"]))
        if source_disk is not None:
            extract_disk_file(source_disk, str(entry["disk_name"]), str(entry["type"]), target_path)
            continue

        fail(f"unable to bootstrap authoritative support file: {target_path}")


def write_authoritative_support_file(entry: Dict[str, object], target_disk: Path) -> None:
    source_path = authoritative_support_path(entry)
    if not source_path.exists():
        fail(f"missing authoritative support file: {source_path}")

    disk_name = str(entry["disk_name"])
    run(["c1541", str(target_disk), "-delete", disk_name], check=False)
    if str(entry["type"]) == "rel":
        spec = f"{disk_name},l,{int(entry['record_length'])}"
    elif str(entry["type"]) == "seq":
        spec = f"{disk_name},s"
    else:
        fail(f"unsupported authoritative file type: {entry['type']}")
    run(["c1541", str(target_disk), "-write", str(source_path), spec], check=False)


def sync_authoritative_from_d71() -> None:
    manifest = resolve_profile(AUTHORITATIVE_PROFILE_ID, None, latest=True)
    sync_entries = load_syncable_authoritative_entries()
    purge_entries = load_syncable_purge_entries()
    existing_by_disk_name = {
        (str(entry["disk_name"]).lower(), str(entry["type"]).lower()): entry
        for entry in sync_entries
    }
    excluded_names = build_owned_excluded_disk_names()
    discovered: List[Dict[str, object]] = []
    seen_name_types: set[tuple[str, str]] = set()

    for disk in manifest.get("disks", []):
        drive = int(disk["drive"])
        disk_path = Path(str(disk["path"]))
        if not disk_path.exists():
            fail(f"missing authoritative source disk for sync: {disk_path}")
        for entry in list_seq_rel_files(disk_path):
            disk_name = str(entry["disk_name"])
            file_type = str(entry["type"]).lower()
            if disk_name.lower() in excluded_names:
                continue
            name_type_key = (disk_name.lower(), file_type)
            if name_type_key in seen_name_types:
                fail(f"duplicate sync candidate across dual-d71 disks: {disk_name} ({file_type})")
            seen_name_types.add(name_type_key)
            discovered.append({
                "disk_name": disk_name,
                "type": file_type,
                "source_drive": drive,
                "disk_path": disk_path,
            })

    discovered.sort(key=lambda item: (int(item["source_drive"]), str(item["disk_name"]).lower()))

    stage_dir = Path(tempfile.mkdtemp(prefix="readyos_authoritative_sync_"))
    new_entries: List[Dict[str, object]] = []
    try:
        for discovered_entry in discovered:
            disk_name = str(discovered_entry["disk_name"])
            file_type = str(discovered_entry["type"])
            existing = existing_by_disk_name.get((disk_name.lower(), file_type))
            repo_name = (
                str(existing["repo_name"])
                if existing is not None
                else repo_name_from_disk_name(disk_name, file_type)
            )
            staged_path = stage_dir / repo_name
            record_length = extract_disk_file(
                Path(str(discovered_entry["disk_path"])),
                disk_name,
                file_type,
                staged_path,
            )
            entry: Dict[str, object] = {
                "disk_name": disk_name,
                "repo_name": repo_name,
                "type": file_type,
                "target_drive": int(discovered_entry["source_drive"]),
            }
            if existing is not None and existing.get("app") not in (None, ""):
                entry["app"] = str(existing["app"])
            if file_type == "rel":
                entry["record_length"] = int(record_length if record_length is not None else 0)
            new_entries.append(entry)

        ensure_dir(AUTHORITATIVE_DATA_DIR)
        for entry in new_entries:
            source_path = stage_dir / str(entry["repo_name"])
            target_path = authoritative_support_path(entry)
            shutil.copyfile(source_path, target_path)

        previous_repo_names = {str(entry["repo_name"]) for entry in sync_entries}
        next_repo_names = {str(entry["repo_name"]) for entry in new_entries}
        removed_repo_names = sorted(previous_repo_names - next_repo_names)
        current_name_types = {
            (str(entry["disk_name"]).lower(), str(entry["type"]).lower())
            for entry in new_entries
        }
        next_purge_by_key = {
            (str(entry["disk_name"]).lower(), str(entry["type"]).lower()): normalize_syncable_purge_entry(entry)
            for entry in purge_entries
            if (str(entry["disk_name"]).lower(), str(entry["type"]).lower()) not in current_name_types
        }
        for entry in sync_entries:
            key = (str(entry["disk_name"]).lower(), str(entry["type"]).lower())
            if key in current_name_types:
                continue
            next_purge_by_key[key] = {
                "disk_name": str(entry["disk_name"]),
                "type": str(entry["type"]).lower(),
            }

        for repo_name in removed_repo_names:
            (AUTHORITATIVE_DATA_DIR / repo_name).unlink(missing_ok=True)

        write_syncable_authoritative_inventory(new_entries)
        write_syncable_purge_inventory(list(next_purge_by_key.values()))

        print(f"Synced authoritative support assets from {AUTHORITATIVE_PROFILE_ID}")
        print(f"  Source version: {manifest['version_text']}")
        print(f"  Updated files: {len(new_entries)}")
        if removed_repo_names:
            print(f"  Removed files: {len(removed_repo_names)}")
            for repo_name in removed_repo_names:
                print(f"    removed {repo_name}")
    finally:
        shutil.rmtree(stage_dir, ignore_errors=True)


def build_help_text(profile: Dict[str, object],
                    resolved: Dict[str, object],
                    entries: List[Dict[str, object]]) -> str:
    public_version = public_release_version(str(resolved["version_text"]))
    vice_parts: List[str] = ["x64sc", "-reu", "-reusize", "16384"]
    for disk in resolved["disks"]:
        drive = str(disk["drive"])
        vice_parts.extend([f"-drive{drive}type", str(disk["vice_drive_type"])])
        if disk.get("true_drive"):
            vice_parts.append(f"-drive{drive}truedrive")
        vice_parts.extend([
            f"-devicebackend{drive}", "0",
            f"+busdevice{drive}",
            f"-{drive}", Path(str(disk["path"])).name,
        ])
    autostart_name = Path(str(resolved["autostart_prg"])).name
    autostart_disk_name = Path(str(resolved["autostart_disk_prg"])).name
    boot_prgs = list(resolved.get("boot_prgs", []))
    vice_parts.extend(["-autostart", autostart_name])
    vice_command = " ".join(vice_parts)
    preboot_mode = str(resolved["preboot_mode"])
    disk_count = len(resolved["disks"])

    lines = [
        f"# {profile['display_name']}",
        "",
        f"- Release Line: `{public_version}`",
        f"- Artifact Build: `{resolved['version_text']}`",
        f"- Kind: `{profile['kind']}`",
        "",
        "## Why This Variant Exists",
        "",
        f"- {VARIANT_NOTES.get(str(profile['kind']), 'Profile-specific ReadyOS media layout.')}",
        "",
        "## Artifacts",
        "",
    ]
    for disk in resolved["disks"]:
        lines.append(f"- Drive {disk['drive']}: `{Path(disk['path']).name}`")
    for boot_prg in boot_prgs:
        lines.append(f"- Host-Side Boot PRG: `{Path(str(boot_prg['path'])).name}`")
    lines.extend([
        "",
        "## Included Apps",
        "",
    ])
    for entry in entries:
        lines.append(f"- Drive {entry['drive']}: `{entry['prg']}` - {entry['label']}")
    lines.extend([
        "",
        "## VICE Setup",
        "",
        "- Enable REU with `16MB`.",
        "- The host-side boot PRGs are convenience autostart files. The disk copy of `PREBOOT` is still the normal disk-side bootstrap.",
    ])
    for disk in resolved["disks"]:
        true_drive_suffix = " with true drive enabled" if disk.get("true_drive") else ""
        lines.append(
            f"- Configure drive {disk['drive']} as `{disk['vice_drive_type']}`{true_drive_suffix} and attach `{Path(disk['path']).name}`."
        )
    lines.extend([
        "",
        "### VICE Command Example",
        "",
        f"- Autostart target: `{autostart_name}`",
        "",
        "```sh",
        vice_command,
        "```",
        "",
        "## Boot",
        "",
    ])
    if preboot_mode == "setd71":
        lines.extend([
            "- This profile uses the dual-stage boot chain `PREBOOT -> SETD71 -> BOOT`.",
            "- Both disks must already be attached before boot, and both drives must be configured as `1571`.",
            "- `SETD71` is part of this variant and reasserts the dual-1571 setup before loading `BOOT`.",
            f"- In VICE, autostart `{autostart_name}`, or manually run `LOAD \"{Path(autostart_disk_name).stem.upper()}\",8` then `RUN`.",
        ])
    else:
        lines.extend([
            "- This profile uses the direct boot chain `PREBOOT -> BOOT`.",
            "- There is no `SETD71` stage for this variant.",
        ])
        if disk_count == 1:
            lines.append(f"- Attach the single disk on drive `8`, then autostart `{autostart_name}` or run `LOAD \"{Path(autostart_disk_name).stem.upper()}\",8` then `RUN`.")
        else:
            lines.append(f"- Attach all listed disks before boot, then autostart `{autostart_name}` or run `LOAD \"{Path(autostart_disk_name).stem.upper()}\",8` then `RUN`.")
    lines.extend([
        "",
        "## C64 Ultimate",
        "",
        "- Copy the listed disk image files to the target storage.",
        "- Enable the REU and set it to `16MB`.",
        "- The host-side boot PRGs are optional convenience files for emulator launching; the disk-side `PREBOOT` entry is the standard hardware boot path.",
    ])
    if preboot_mode == "setd71":
        lines.extend([
            "- Attach both disk images before boot and use `1571`-compatible drive assignments for the two-disk set.",
            "- Boot with `LOAD \"PREBOOT\",8` then `RUN`; this variant then chains through `SETD71` before loading `BOOT`.",
        ])
    else:
        if disk_count == 1:
            lines.append("- Attach the single disk image on drive `8`, then boot with `LOAD \"PREBOOT\",8` and `RUN`.")
        else:
            lines.append("- Attach all listed disk images to their matching drives before boot, then run `LOAD \"PREBOOT\",8` and `RUN`.")
        lines.append("- This variant boots directly from `PREBOOT` into `BOOT` and does not use `SETD71`.")
    lines.append("")
    return "\n".join(lines)


def build_release(profile_id: str,
                  version_text: str,
                  catalog_override: str | None,
                  override_load_all: str | None,
                  override_run_first: str | None) -> None:
    profile = load_profile(profile_id)
    resolved = resolve_profile(profile_id, version_text, latest=False)
    catalog_source = Path(catalog_override) if catalog_override else profile_catalog_source(profile)
    entries = parse_catalog_entries(profile, str(catalog_source))
    apps_set = enabled_apps(entries)
    managed_files = managed_build_names(profile, apps_set)
    output_dir = Path(resolved["output_dir"])
    manifest_path = Path(resolved["manifest_path"])

    ensure_dir(output_dir)
    ensure_generated_assets(profile, catalog_source, override_load_all, override_run_first)
    bootstrap_authoritative_support_files(apps_set)
    rebuild_profile_boot_chain(profile, version_text)

    previous_manifest = None
    if manifest_path.exists():
        previous_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    backups = {}
    if previous_manifest:
        for disk in previous_manifest.get("disks", []):
            old_path = Path(str(disk["path"]))
            backup = backup_user_files(old_path, managed_files)
            if backup:
                backups[int(disk["index"])] = backup

    for disk_meta in profile["disks"]:
        disk_index = int(disk_meta["index"])
        disk_drive = int(disk_meta["drive"])
        disk_path = output_dir / build_disk_filename(profile, disk_meta, version_text)
        if disk_path.exists():
            disk_path.unlink()
        run(["c1541", "-format", str(disk_meta["label"]), str(disk_meta["image_type"]), str(disk_path)])
        for entry in disk_meta["contents"]:
            if entry["type"] == "prg" and str(entry["name"]) in KNOWN_APP_NAMES and str(entry["name"]) not in apps_set:
                continue
            app_name = entry.get("app")
            if app_name and str(app_name) not in apps_set:
                continue
            host_path = repo_artifact_path(str(entry["artifact"]))
            if not host_path.exists():
                fail(f"missing artifact for {profile_id}: {host_path}")
            write_spec = str(entry["name"])
            if entry["type"] == "seq":
                write_spec = f"{write_spec},s"
            run(["c1541", str(disk_path), "-write", str(host_path), write_spec])

        for op in disk_meta.get("post_build", []):
            if op["type"] == "seed_cal26_rel" and "cal26" in apps_set:
                run([sys.executable, str(ROOT / "build_support" / "seed_cal26_rel.py"),
                     "--disk", str(disk_path)])

        for support_entry in authoritative_support_entries(apps_set):
            if support_target_drive(profile, support_entry, entries) != disk_drive:
                continue
            write_authoritative_support_file(support_entry, disk_path)

        if disk_index in backups:
            _stage_dir, manifest = backups[disk_index]
            restore_user_files(disk_path, manifest)

    for backup in backups.values():
        stage_dir, _manifest = backup
        shutil.rmtree(stage_dir, ignore_errors=True)

    for boot_prg in resolved["boot_prgs"]:
        source = Path(str(boot_prg["source"]))
        target = Path(str(boot_prg["path"]))
        if not source.exists():
            fail(f"missing boot-chain artifact for {profile_id}: {source}")
        shutil.copyfile(source, target)

    help_text = build_help_text(profile, resolved, entries)
    (output_dir / "helpme.md").write_text(help_text + "\n", encoding="utf-8")
    (output_dir / "help.md").write_text(help_text + "\n", encoding="utf-8")

    manifest = {
        "id": profile["id"],
        "display_name": profile["display_name"],
        "kind": profile["kind"],
        "variant_boot_name": resolved["variant_boot_name"],
        "version_text": version_text,
        "catalog_source": str(catalog_source),
        "autostart_prg": resolved["autostart_prg"],
        "autostart_disk_prg": resolved["autostart_disk_prg"],
        "preboot_mode": str(profile["boot"]["preboot_mode"]),
        "disks": resolved["disks"],
        "boot_prgs": resolved["boot_prgs"],
        "apps": entries,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    write_release_root_readme(version_text)

    if previous_manifest:
        for disk in previous_manifest.get("disks", []):
            old_path = Path(str(disk["path"]))
            if old_path.exists() and old_path.parent == output_dir and old_path.name not in {
                Path(str(item["path"])).name for item in resolved["disks"]
            }:
                old_path.unlink()
        for boot_prg in previous_manifest.get("boot_prgs", []):
            old_path = Path(str(boot_prg["path"]))
            if old_path.exists() and old_path.parent == output_dir and old_path.name not in {
                Path(str(item["path"])).name for item in resolved["boot_prgs"]
            }:
                old_path.unlink()


def print_shell_exports(profile_id: str, version_text: str) -> None:
    resolved = resolve_profile(profile_id, version_text, latest=False)
    disks = sorted(resolved["disks"], key=lambda item: int(item["index"]))

    def shell_quote(value: str) -> str:
        return "'" + value.replace("'", "'\"'\"'") + "'"

    print(f"PROFILE_ID={shell_quote(str(resolved['id']))}")
    print(f"PROFILE_DISPLAY_NAME={shell_quote(str(resolved['display_name']))}")
    print(f"PROFILE_VERSION_TEXT={shell_quote(str(resolved['version_text']))}")
    print(f"PROFILE_READYSHELL_PARSE_TRACE_DEBUG={shell_quote(str(resolved['readyshell_parse_trace_debug']))}")
    print(f"PROFILE_AUTOSTART_PRG={shell_quote(str(resolved['autostart_prg']))}")
    print(f"PROFILE_AUTOSTART_DISK_PRG={shell_quote(str(resolved['autostart_disk_prg']))}")
    print(f"PROFILE_PREBOOT_MODE={shell_quote(str(resolved['preboot_mode']))}")
    print("PROFILE_DISK_PATHS=(" + " ".join(shell_quote(str(disk["path"])) for disk in disks) + ")")
    print("PROFILE_DISK_DRIVES=(" + " ".join(shell_quote(str(disk["drive"])) for disk in disks) + ")")
    print("PROFILE_VICE_ATTACH_ARGS=(" +
          " ".join(
              shell_quote(arg)
              for disk in disks
              for arg in (
                  f"-drive{disk['drive']}type",
                  str(disk["vice_drive_type"]),
                  *(["-drive%dtruedrive" % disk["drive"]] if disk.get("true_drive") else []),
                  f"-devicebackend{disk['drive']}",
                  "0",
                  f"+busdevice{disk['drive']}",
                  f"-{disk['drive']}",
                  str(disk["path"]),
              )
          ) +
          ")")


def migrate_legacy_release_tree(version_text: str) -> None:
    version_dir = release_version_dir(version_text)
    ensure_dir(version_dir)

    for profile_id in list_profile_ids():
        profile = load_profile(profile_id)
        legacy_dir = legacy_profile_output_dir(profile)
        if not legacy_dir.is_dir():
            continue
        target_dir = profile_output_dir(profile, version_text)
        if target_dir.exists():
            shutil.rmtree(target_dir)
        shutil.copytree(legacy_dir, target_dir)

        manifest_path = target_dir / "manifest.json"
        if manifest_path.is_file():
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["version_text"] = version_text
            manifest["catalog_source"] = str(profile_catalog_source(profile))
            manifest["output_dir"] = str(target_dir)
            manifest["manifest_path"] = str(manifest_path)
            manifest["readyshell_parse_trace_debug"] = readyshell_parse_trace_debug(profile)
            if "autostart_prg" in manifest:
                manifest["autostart_prg"] = str(target_dir / Path(str(manifest["autostart_prg"])).name)
            for disk in manifest.get("disks", []):
                disk["path"] = str(target_dir / Path(str(disk["path"])).name)
            for boot_prg in manifest.get("boot_prgs", []):
                boot_prg["path"] = str(target_dir / Path(str(boot_prg["path"])).name)
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    write_release_root_readme(version_text)


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list-ids")
    sub.add_parser("default-id")

    catalog_parser = sub.add_parser("catalog-source")
    catalog_parser.add_argument("--profile", required=True)

    parse_trace_parser = sub.add_parser("readyshell-parse-trace-debug")
    parse_trace_parser.add_argument("--profile", required=True)

    resolve_parser = sub.add_parser("resolve")
    resolve_parser.add_argument("--profile", required=True)
    resolve_group = resolve_parser.add_mutually_exclusive_group()
    resolve_group.add_argument("--version")
    resolve_group.add_argument("--latest", action="store_true")

    export_shell = sub.add_parser("export-shell")
    export_shell.add_argument("--profile", required=True)
    export_shell.add_argument("--version", required=True)

    latest_disk = sub.add_parser("latest-disk")
    latest_disk.add_argument("--profile", default=AUTHORITATIVE_PROFILE_ID)
    latest_disk.add_argument("--drive", type=int, default=8)

    preboot_parser = sub.add_parser("write-preboot")
    preboot_parser.add_argument("--profile", required=True)
    preboot_parser.add_argument("--output", required=True)

    build_parser = sub.add_parser("build-release")
    build_parser.add_argument("--profile", required=True)
    build_parser.add_argument("--version", required=True)
    build_parser.add_argument("--catalog-source")
    build_parser.add_argument("--override-load-all")
    build_parser.add_argument("--override-run-first")

    sub.add_parser("sync-authoritative-from-d71")

    migrate_parser = sub.add_parser("migrate-legacy-release")
    migrate_parser.add_argument("--version", required=True)

    args = ap.parse_args()

    try:
        if args.cmd == "list-ids":
            for profile_id in list_profile_ids():
                print(profile_id)
            return 0
        if args.cmd == "default-id":
            print(default_profile_id())
            return 0
        if args.cmd == "catalog-source":
            profile = load_profile(args.profile)
            print(profile_catalog_source(profile))
            return 0
        if args.cmd == "readyshell-parse-trace-debug":
            profile = load_profile(args.profile)
            print(readyshell_parse_trace_debug(profile))
            return 0
        if args.cmd == "resolve":
            payload = resolve_profile(args.profile, getattr(args, "version", None), getattr(args, "latest", False))
            print(json.dumps(payload, indent=2))
            return 0
        if args.cmd == "latest-disk":
            print(latest_profile_disk_path(args.profile, args.drive))
            return 0
        if args.cmd == "export-shell":
            print_shell_exports(args.profile, args.version)
            return 0
        if args.cmd == "write-preboot":
            profile = load_profile(args.profile)
            write_preboot(str(profile["boot"]["preboot_mode"]), Path(args.output))
            return 0
        if args.cmd == "build-release":
            build_release(args.profile, args.version, args.catalog_source,
                          args.override_load_all, args.override_run_first)
            return 0
        if args.cmd == "sync-authoritative-from-d71":
            sync_authoritative_from_d71()
            return 0
        if args.cmd == "migrate-legacy-release":
            migrate_legacy_release_tree(args.version)
            return 0
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
