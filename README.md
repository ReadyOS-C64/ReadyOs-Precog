# ReadyOS

ReadyOS PRECOG is an experimental REU-first environment for a modern
Commodore 64 setup. It is aimed first at Commodore 64 Ultimate-family
hardware, but it is also designed to run well in VICE with REU enabled, which
makes it a practical fit for THEC64 Mini / Maxi style workflows too. PRECOG
`0.2` is the current public release line.

What if a Commodore 64 could feel ready, not just nostalgic? ReadyOS treats
waiting as the enemy. It is a keyboard-first, full-screen terminal-style
environment built around instant app switching, suspend/resume, shared
clipboard and history, and deeper links between apps. The goal is a C64
workflow where READY means responsive, reliable, and repeatable.

At a glance:

- requires an REU-backed modern C64 path; tested at `16MB` REU
- primary focus: Commodore 64 Ultimate workflows
- practical secondary path: VICE with REU enabled
- tuned to stay usable from `1MHz` up through `48MHz`
- ships as profile-based media builds with REU-backed app switching
- local artifact filenames may include an extra trailing letter such as
  `0.2c`; that suffix is an internal build/debug stamp, not a separate public release

Project overview: https://readyos.notion.site/

## Getting Started

The canonical release layout is:

- `releases/<version>/precog-dual-d71/`
- `releases/<version>/precog-d81/`
- `releases/<version>/precog-dual-d64/`
- `releases/<version>/precog-solo-d64-a/`
- `releases/<version>/precog-solo-d64-b/`
- `releases/<version>/precog-solo-d64-c/`
- `releases/<version>/precog-solo-d64-d/`

Older checked-in snapshot folders such as `release/` or `Releases/` should be
treated as historical artifacts, not the current build target.

When a helper script needs a dual-`D71` source image for authoritative `SEQ` /
`REL` seeding or recovery, it should resolve that from the latest built
`releases/<version>/precog-dual-d71/manifest.json` and its listed disks, not
from root-level `readyos.d71` / `readyos_2.d71` files.

Tested targets:

- VICE
- THEC64 Mini / Maxi
- Commodore 64 Ultimate-family hardware such as C64 Ultimate, Ultimate 64, or
  Ultimate Cart

Recommended setup:

- enable the REU
- set REU size to `16MB`
- follow the `helpme.md` inside the selected `releases/<version>/<profile>/` directory

Boot sequence:

- `LOAD "PREBOOT",8,1`
- `RUN`

## Current Status

- Base release: `0.2`
- Local builds use the existing rolling suffix flow for artifact filenames only
- Builds release media per profile
- Launcher catalog currently contains `16` apps
- Runtime reserves `24` app slots in REU
- ReadyOS runs on its own; UltimateBuddy remains an optional companion concept,
  not a runtime dependency

## Release Variants

ReadyOS now ships the same runtime in seven media variants because the target
drive types and disk capacities are different.

| Profile | Media | Why It Exists | Boot Flow | App Set |
| --- | --- | --- | --- | --- |
| `precog-dual-d71` | two `D71` images on drives `8` and `9` | default full-content profile for `1571` setups and the main local verification target | `PREBOOT -> SETD71 -> BOOT` | full current app catalog |
| `precog-d81` | one `D81` image on drive `8` | full-content single-disk profile for `1581`/`D81` setups | `PREBOOT -> BOOT` | full current app catalog |
| `precog-dual-d64` | two `D64` images on drives `8` and `9` | reduced profile for `1541`-compatible capacity limits | `PREBOOT -> BOOT` | curated subset of the current app catalog |
| `precog-solo-d64-a` | one `D64` image on drive `8` | standalone single-disk subset with editor, reference, and dizzy | `PREBOOT -> BOOT` | `editor`, `hexview`, `readme`, `dizzy` |
| `precog-solo-d64-b` | one `D64` image on drive `8` | standalone single-disk productivity subset with quicknotes, calculator, clipboard, and files | `PREBOOT -> BOOT` | `quicknotes`, `calcplus`, `clipmgr`, `simplefiles` |
| `precog-solo-d64-c` | one `D64` image on drive `8` | standalone single-disk planning subset with tasklist, calendar, and REU viewer | `PREBOOT -> BOOT` | `tasklist`, `cal26`, `reuviewer` |
| `precog-solo-d64-d` | one `D64` image on drive `8` | standalone single-disk experimental subset with readyshell, simple cells, game2048, and deminer | `PREBOOT -> BOOT` | `readyshell`, `simplecells`, `game2048`, `deminer` |

The dual-D64 profile is intentionally smaller. Right now it keeps the core
productivity path that fits on two `D64`s: `editor`, `quicknotes`,
`calcplus`, `clipmgr`, `tasklist`, `simplefiles`, `game2048`, `sidetris`,
`deminer`, and `cal26`.

The solo-D64 variants exist for environments that can mount only one `D64`
at a time, such as some web emulators and simplified media loaders. The split
is intentional:

- `editor` stays away from `quicknotes` so the text-editor and note-editor
  workflows do not compete for the same image.
- `cal26` stays away from `dizzy`, and `tasklist` stays away from `dizzy`,
  because the calendar and kanban data files are both REL-backed and should
  not share a pack with the task list.
- `readyshell` stays with `simplecells` in the experimental pack so the more
  demo-oriented tooling is grouped together.
- `deminer` is included in the solo-D64 set, and the matching support payloads
  are carried alongside each variant: `SEQ` files for `editor`, `quicknotes`,
  `tasklist`, and `simplecells`, plus the `REL` files for `cal26` and `dizzy`.
- Each image keeps some free blocks, so a standalone disk still has room for
  catalog and user-file growth instead of being packed to the edge.

## App Catalog

The launcher-visible catalog lives in `cfg/profiles/*.ini` and is generated
to `apps.cfg` on drive `8` for the selected profile.

## App Config Format

Each profile file in `cfg/profiles/` becomes the generated `apps.cfg` on drive
`8`. The real file format has three sections in this order: `[system]`,
`[launcher]`, and `[apps]`. The dual-`d71` profile begins like this:

```ini
[system]
variant_name=precog dual d71
variant_boot_name=precog dual d71

[launcher]
load_all_to_reu=0
runappfirst=

[apps]
9:editor:editor:1
text editor with clipboard

8:quicknotes:quicknotes
reu-backed note editor

8:cal26:calendar 26
calendar for 2026 with appointments
```

How it works:

- `[system]` carries launcher-visible variant text. `variant_name` is the
  general profile name, and `variant_boot_name` is the boot/display variant
  string when that needs to differ.
- `load_all_to_reu` controls whether the launcher tries to preload all app
  payloads into the REU at startup.
- `runappfirst` optionally names an app token to auto-launch after boot.
- Each app record uses `drive:program:label[:slot]` on one line, followed by a
  human-readable description on the next line.
- In `9:editor:editor:1`, `9` is the source drive, `editor` is the PRG token,
  the second `editor` is the launcher label, and the trailing `1` is the
  default hotkey slot. The parser accepts slot values `1..9`; omitting the
  fourth field means no default slot binding.
- `drive` must be numeric and in the range `8..11`.
- `program` must be lowercase, must not include `.prg`, must not include a
  Commodore file-type suffix such as `,p`, and must be `12` characters or
  fewer.
- `label` is the launcher-visible app name and is limited to `31` characters.
- The description line is limited to `38` characters.
- Source text is expected to be lowercase. The build step writes the final
  `apps.cfg` as a lowercase-PETASCII `SEQ` payload, and the launcher reads that
  generated file from drive `8`.
- Blank lines and comment lines are allowed in the source profile. App records
  still follow the same alternating entry-line / description-line structure.

| Drive | Program | Display Name | Current Role |
| --- | --- | --- | --- |
| 9 | `editor` | editor | Text editor with clipboard, find, and disk save/open |
| 8 | `quicknotes` | quicknotes | Split-pane REU-backed notes with save/open and search |
| 9 | `calcplus` | calc plus | Expression calculator with history, modes, variables, and clipboard |
| 9 | `hexview` | hex viewer | Memory browser with PETSCII and screen-code views |
| 9 | `clipmgr` | clipboard | Multi-item clipboard manager with preview and file import/export |
| 9 | `reuviewer` | reu viewer | Visual 256-bank REU map |
| 9 | `tasklist` | task list | Hierarchical outliner with notes, search, and file persistence |
| 9 | `simplefiles` | simple files | Dual-pane file manager with copy, rename, delete, and SEQ viewing |
| 9 | `simplecells` | simple cells (alpha) | Single-sheet spreadsheet with formulas, formatting, and save/load |
| 9 | `game2048` | 2048 game | 2048 puzzle game with resume/app switching |
| 9 | `sidetris` | sidetris | Sideways PETSCII block-drop game with suspend/resume |
| 8 | `cal26` | calendar 26 | 2026 calendar with month, week, day, upcoming, and REL-backed appointments; task reading is currently broken |
| 8 | `dizzy` | dizzy kanban | Kanban board with REL-backed persistence, search, and reorder |
| 9 | `readme` | read.me | In-system ReadyOS guide viewer |
| 8 | `readyshell` | ready shell | Overlay-based shell with expressions, pipelines, value save/load, directory queries, and text/file commands including `cat`, `put`, `add`, `del`, `ren`, and `copy` |
| 8 | `deminer` | deminer | Minesweeper-style puzzle with suspend/resume |

Notes:

- ReadyShell guide: [src/apps/readyshellpoc/README.md](src/apps/readyshellpoc/README.md)
- ReadyShell tutorial: [src/apps/readyshellpoc/ReadyShelltutorial.md](src/apps/readyshellpoc/ReadyShelltutorial.md)
- ReadyShell architecture: [docs/ReadyShellArchitecture.md](docs/ReadyShellArchitecture.md)
- ReadyShell overlay inventory: [docs/readyshell_overlay_inventory.md](docs/readyshell_overlay_inventory.md)
- ReadyShell now ships eight overlays: `rsparser`, `rsvm`, `rsdrvilst`,
  `rsldv`, `rsstv`, `rsfops`, `rscat`, and `rscopy`.
- ReadyShell preloads and REU-caches all eight overlays at startup. Bank `$40`
  holds overlays `1`, `2`, `3`, and `5`; bank `$41` holds overlays `4`, `6`,
  `7`, and `8`.
- Current ReadyShell command set: `PRT`, `MORE`, `TOP`, `SEL`, `GEN`, `TAP`,
  `DRVI`, `LST`, `LDV`, `STV`, `CAT`, `PUT`, `ADD`, `DEL`, `REN`, and `COPY`.
- `PUT` and `ADD` use direct `COMMAND <expr>, <filename>` syntax. `PUT`
  creates or replaces PETASCII text files; `ADD` appends to `SEQ` files and
  creates them when missing.
- `cal26` currently has a known regression: task reading is broken.
- `showcfg.prg` is a BASIC inspector for the generated `apps.cfg` payload on
  drive `8`.

## Architecture Snapshot

Runtime memory layout:

- app runtime window: `$1000-$C5FF`
- REU metadata/system table: `$C600-$C7FF`
- resident shim: `$C800-$C9FF`
- hardware I/O region: `$D000-$DFFF`

Runtime model:

- the active app owns `$1000-$C5FF`
- the shim stays resident outside that window
- app switching works by stashing and fetching the app window through the REU
- apps can return to the launcher or switch directly to another app without
  treating each transition as a fresh process launch

REU layout:

- bank `0`: launcher/system state
- clipboard payload banks: dynamic allocation pool, with legacy bank `1`
  wording retained only in parts of the older contract surface
- banks `2-25`: app slots (`24` total)
- higher banks: dynamic allocation pool
- bank `$40`: ReadyShell overlay cache bank for overlays `1`, `2`, `3`, and `5`
- bank `$41`: ReadyShell overlay cache bank for overlays `4`, `6`, `7`, and `8`
- bank `$43`: ReadyShell debug/probe ring
- bank `$48`: ReadyShell scratch, metadata, command registry, and REU value arena

Disk layout:

- media shape depends on the selected release profile
- dual-d71 is the default local run/test target
- d81 and dual-d64 profiles reuse the same runtime with different media maps

## Build And Run

Requirements:

- `cc65` toolchain: `cl65`, `ca65`, `ld65`
- VICE tools, especially `x64sc`, `c1541`, and `petcat`
- `python3`

Main entry points:

- `bash ./run.sh`
  rebuild the default release profile and launch ReadyOS in VICE
- `pwsh -File ./run.ps1`
  PowerShell entry point for the same workflow
- `bash ./run.sh --profile precog-d81`
  build and launch a non-default profile
- `bash ./run.sh --list-profiles`
  print the known release profile ids
- `bash ./run.sh --build-all`
  build every release profile and exit
- `bash ./run.sh --force-artifacts-from-d71 --build-all`
  promote non-excluded `SEQ` files and current `REL` files from the latest
  built `precog-dual-d71` images into `cfg/authoritative/`, then rebuild all
  profiles from that updated authoritative set
- `make seed-cal26`
  seed CAL26 REL data into the latest built `precog-dual-d71` drive `8` image
- `bash ./run.sh --vice-fast`
  launch with VICE drive traps enabled, true drive emulation disabled, and the
  emulator starting in warp mode
- `bash ./run.sh --skipbuild`
  launch using the latest built artifacts for the selected profile
- `make`
  build the default profile release package
- `make release-all`
  build all release profiles with one version stamp
- `make audit-release-assets`
  extract packaged `SEQ`/`REL` files from every built image and compare them
  against the source of truth
- `make verify`
  run the repo verification and host smoke checks

Notes:

- `run.sh` is the preferred local workflow because it rebuilds disks, updates
  generated assets, and preserves managed disk state correctly.
- `--force-artifacts-from-d71` is opt-in. Without it, ordinary builds keep the
  existing repo-authoritative behavior.
- Disk-seeding and authoritative sync helpers should resolve their donor or
  destination `D71` from the latest built `precog-dual-d71` release manifest.
  Root-level `readyos.d71` / `readyos_2.d71` files are not the source of truth.
- `--vice-fast` only changes the VICE launch configuration. It does not affect
  build outputs or the packaged release images.
- Manual launch should match the setup in the Getting Started section.
- Windows support is still less exercised than the Unix shell path.

## Generated Assets And Public Docs

Generated build-owned assets include:

- `apps.cfg` from the selected `cfg/profiles/*.ini` source
- `src/generated/readme_pages.c` and `src/generated/readme_pages.h` from
  `src/apps/readme/readme_lite.md`
- `src/generated/build_version.h` and `src/generated/msg_version.inc` from the
  local run/build flow

Authoritative editable support payloads now live in `cfg/authoritative/`.
That includes the shipped SEQ and REL support files such as `editor help`,
`example tasks`, `myquicknotes`, `clipset1`, `clipset3`, `sheet2`,
`cal26.rel`, `cal26cfg.rel`, `dizzy.rel`, and `dizzycfg.rel`.

`cfg/authoritative/sync_inventory.json` tracks the sync-managed support set
that may be promoted from the latest built `precog-dual-d71` images when
`bash ./run.sh --force-artifacts-from-d71 ...` is used. Build-owned exclusions
such as generated `apps.cfg` and generated support payloads still come from the
normal code/build pipeline rather than from disk extraction.

Forced sync is exact for non-excluded `SEQ` files and discovered `REL` files:
new files are extracted into `cfg/authoritative/`, changed files replace the
repo copies, and previously authoritative sync-managed files that are no longer
present on the dual-D71 source images are removed so later rebuilds stop
reinserting them.

Normal profile rebuilds preserve non-managed user files from the prior
profile build while replacing build-owned artifacts in `releases/<version>/<profile>/`.

The packaged release audit extracts those internal `SEQ` and `REL` files from
the built images and checks them byte-for-byte against `cfg/authoritative/`
plus the generated `apps.cfg`.

Public supporting docs in `docs/` currently include:

- `docs/clipboard_bundle_seq_format.md`
- `docs/quicknotes_seq_format.md`
- `docs/simplecells_seq_format.md`
- [docs/readyshell_overlay_inventory.md](docs/readyshell_overlay_inventory.md)
- [docs/ReadyShellArchitecture.md](docs/ReadyShellArchitecture.md)

Rendered documentation exports in `docs/` currently include:

- [ReadyOS SHIM Architecture Report (HTML)](docs/ReadyOS%20SHIM%20Architecture%20Report%20%280.2%29.html)
- [ReadyShell Overlay Inventory (HTML)](docs/readyshell_overlay_inventory.html)
- [ReadyShell Architecture (HTML)](docs/ReadyShellArchitecture.html)

## Repository Layout

- `src/boot`: bootloader and BASIC boot helpers
- `src/apps`: launcher and user-facing apps
- `src/lib`: shared TUI, REU, clipboard, storage, and resume libraries
- `src/shim`: resident shim and switch/runtime support
- `cfg`: linker configs and catalog inputs
- `build_support`: local build-chain support, verification, and disk helpers
- `docs`: public format notes and related documentation
- `logs`: VICE logs and automation output

## Notes For Contributors

- Treat the launcher + shim path as the real execution model for ReadyOS.
- Respect the fixed app window and resident shim/system regions.
- Use `run.sh`, `run.ps1`, `make`, and `make verify` for normal public
  build-and-check workflows.

## License

ReadyOS is licensed under the MIT License. See `LICENSE` for the full text.
Copyright (c) 2026 Karl Prosser.
