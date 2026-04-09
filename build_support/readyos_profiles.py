#!/usr/bin/env python3
"""
ReadyOS profile loader and release packager.
"""

from __future__ import annotations

import argparse
import json
import os
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
AUTHORITATIVE_PROFILE_ID = "precog-dual-d71"
AUTHORITATIVE_DATA_DIR = ROOT / "cfg" / "authoritative"
BOOTSTRAP_D71_BY_DRIVE = {
    8: ROOT / "readyos.d71",
    9: ROOT / "readyos_2.d71",
}
REL_SEED_D71_CANDIDATES = [
    ROOT / "readyos0-1-5.d71",
    ROOT.parent / "readyos0-1-5.d71",
    ROOT.parent.parent / "readyos0-1-5.d71",
]
AUTHORITATIVE_SUPPORT_FILES = (
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
        "app": "tasklist",
        "disk_name": "example tasks",
        "repo_name": "example_tasks.seq",
        "type": "seq",
        "bootstrap_drive": 8,
        "target_drive": 8,
        "generated_artifact": "obj/tasklist_sample.seq",
    },
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
KNOWN_APP_NAMES = {
    "editor",
    "quicknotes",
    "calcplus",
    "hexview",
    "clipmgr",
    "reuviewer",
    "tasklist",
    "simplefiles",
    "simplecells",
    "game2048",
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


def list_profile_ids() -> List[str]:
    ids = []
    for path in sorted(PROFILES_DIR.glob("*.json")):
        ids.append(path.stem)
    return ids


def load_profile(profile_id: str) -> Dict[str, object]:
    path = PROFILES_DIR / f"{profile_id}.json"
    if not path.exists():
        fail(f"unknown profile: {profile_id}")
    profile = json.loads(path.read_text(encoding="utf-8"))
    profile["_path"] = str(path)
    return profile


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


def build_disk_filename(profile: Dict[str, object], disk: Dict[str, object], version_text: str) -> str:
    kind = str(profile["kind"])
    ext = str(disk["image_type"])
    disks = profile["disks"]
    if len(disks) == 1:
        return f"readyos-v{version_text.lower()}-{kind}.{ext}"
    return f"readyos-v{version_text.lower()}-{kind}_{int(disk['index'])}.{ext}"


def build_boot_prg_filename(profile: Dict[str, object], version_text: str, stem: str) -> str:
    return f"readyos-v{version_text.lower()}-{str(profile['kind'])}-{stem}.prg"


def boot_prg_specs(profile: Dict[str, object], version_text: str, output_dir: Path) -> List[Dict[str, str]]:
    specs = [
        {
            "stem": "preboot",
            "disk_name": "preboot",
            "source": str(ROOT / "preboot.prg"),
            "path": str(output_dir / build_boot_prg_filename(profile, version_text, "preboot")),
        },
        {
            "stem": "boot",
            "disk_name": "boot",
            "source": str(ROOT / "boot.prg"),
            "path": str(output_dir / build_boot_prg_filename(profile, version_text, "boot")),
        },
    ]
    if str(profile["boot"]["preboot_mode"]) == "setd71":
        specs.append(
            {
                "stem": "setd71",
                "disk_name": "setd71",
                "source": str(ROOT / "setd71.prg"),
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
    targets = ["boot.prg", "preboot.prg"]
    if str(profile["boot"]["preboot_mode"]) == "setd71":
        targets.append("setd71.prg")
    run(["make", "-B", f"PROFILE={profile['id']}", f"READYOS_VERSION_TEXT={version_text}", *targets])


def ensure_generated_assets(profile: Dict[str, object],
                            catalog_source: Path,
                            override_load_all: str | None,
                            override_run_first: str | None) -> None:
    obj_dir = ROOT / "obj"
    ensure_dir(obj_dir)

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
        "--input", str(ROOT / "cfg" / "tasklist_sample.txt"),
        "--output", str(obj_dir / "tasklist_sample.seq"),
    ])

    write_preboot(str(profile["boot"]["preboot_mode"]), ROOT / "preboot.prg")
    if str(profile["boot"]["preboot_mode"]) == "setd71":
        run(["petcat", "-w2", "-o", str(ROOT / "setd71.prg"), str(ROOT / "src" / "boot" / "setd71.bas")])


def managed_build_names(profile: Dict[str, object], apps_set: set[str]) -> set[str]:
    managed = {"apps.cfg"}
    for entry in authoritative_support_entries(apps_set):
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
            marker = "record length "
            rec_len = ""
            if marker in rel.stderr:
                rec_len = rel.stderr.split(marker, 1)[1].split()[0]
            elif marker in rel.stdout:
                rec_len = rel.stdout.split(marker, 1)[1].split()[0]
            if not rec_len:
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
    return [entry for entry in AUTHORITATIVE_SUPPORT_FILES if str(entry["app"]) in apps_set]


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


def extract_disk_file(source_disk: Path, disk_name: str, file_type: str, target_path: Path) -> None:
    if file_type == "rel":
        spec = f"{disk_name},l"
    elif file_type == "seq":
        spec = f"{disk_name},s"
    else:
        fail(f"unsupported authoritative file type: {file_type}")
    run(["c1541", str(source_disk), "-read", spec, str(target_path)], check=False)
    if not target_path.exists() or target_path.stat().st_size == 0:
        target_path.unlink(missing_ok=True)
        fail(f"missing {file_type.upper()} payload in source disk {source_disk}: {disk_name}")


def resolve_bootstrap_d71_disks() -> Dict[int, Path]:
    disks: Dict[int, Path] = {}
    for drive, path in BOOTSTRAP_D71_BY_DRIVE.items():
        if path.exists():
            disks[int(drive)] = path

    try:
        manifest = resolve_profile(AUTHORITATIVE_PROFILE_ID, None, latest=True)
    except ValueError:
        manifest = None

    if manifest is not None:
        for disk in manifest.get("disks", []):
            path = Path(str(disk.get("path", "")))
            drive = int(disk.get("drive", 0))
            if path.exists() and drive not in disks:
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

        source_disk = bootstrap_disks.get(int(entry["bootstrap_drive"]))
        if source_disk is not None:
            extract_disk_file(source_disk, str(entry["disk_name"]), str(entry["type"]), target_path)
            continue

        generated_artifact = entry.get("generated_artifact")
        if generated_artifact:
            generated_path = ROOT / str(generated_artifact)
            if generated_path.exists():
                shutil.copyfile(generated_path, target_path)
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


def build_help_text(profile: Dict[str, object],
                    resolved: Dict[str, object],
                    entries: List[Dict[str, object]]) -> str:
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
        f"- Version: `{resolved['version_text']}`",
        f"- Kind: `{profile['kind']}`",
        "",
        "## Artifacts",
        "",
    ]
    for disk in resolved["disks"]:
        lines.append(f"- Drive {disk['drive']}: `{Path(disk['path']).name}`")
    for boot_prg in boot_prgs:
        lines.append(f"- Host PRG: `{Path(str(boot_prg['path'])).name}`")
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
            host_path = ROOT / str(entry["artifact"])
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
            if "autostart_prg" in manifest:
                manifest["autostart_prg"] = str(target_dir / Path(str(manifest["autostart_prg"])).name)
            for disk in manifest.get("disks", []):
                disk["path"] = str(target_dir / Path(str(disk["path"])).name)
            for boot_prg in manifest.get("boot_prgs", []):
                boot_prg["path"] = str(target_dir / Path(str(boot_prg["path"])).name)
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list-ids")
    sub.add_parser("default-id")

    catalog_parser = sub.add_parser("catalog-source")
    catalog_parser.add_argument("--profile", required=True)

    resolve_parser = sub.add_parser("resolve")
    resolve_parser.add_argument("--profile", required=True)
    resolve_group = resolve_parser.add_mutually_exclusive_group()
    resolve_group.add_argument("--version")
    resolve_group.add_argument("--latest", action="store_true")

    export_shell = sub.add_parser("export-shell")
    export_shell.add_argument("--profile", required=True)
    export_shell.add_argument("--version", required=True)

    preboot_parser = sub.add_parser("write-preboot")
    preboot_parser.add_argument("--profile", required=True)
    preboot_parser.add_argument("--output", required=True)

    build_parser = sub.add_parser("build-release")
    build_parser.add_argument("--profile", required=True)
    build_parser.add_argument("--version", required=True)
    build_parser.add_argument("--catalog-source")
    build_parser.add_argument("--override-load-all")
    build_parser.add_argument("--override-run-first")

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
        if args.cmd == "resolve":
            payload = resolve_profile(args.profile, getattr(args, "version", None), getattr(args, "latest", False))
            print(json.dumps(payload, indent=2))
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
        if args.cmd == "migrate-legacy-release":
            migrate_legacy_release_tree(args.version)
            return 0
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
