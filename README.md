# ReadyOS

ReadyOS PRECOG is an experimental REU-first environment for a modern
Commodore 64 setup. It is aimed first at the Commodore 64 Ultimate, but it is
also designed to run well in VICE with REU enabled, which makes it a practical
fit for THEC64 Mini / Maxi style workflows too. PRECOG `0.1.8` is the current
experimental release line.

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

Project overview: https://readyos.notion.site/

## Getting Started

Use the profile-specific disk images from the selected release folder:

- `releases/<version>/precog-dual-d71/`
- `releases/<version>/precog-d81/`
- `releases/<version>/precog-dual-d64/`

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

- Base release: `0.1.8`
- Local builds use the existing rolling suffix flow
- Builds release media per profile
- Launcher catalog currently contains `15` apps
- Runtime reserves `24` app slots in REU
- ReadyOS runs on its own; UltimateBuddy remains an optional companion concept,
  not a runtime dependency

## App Catalog

The launcher-visible catalog lives in `cfg/profiles/*.ini` and is generated
to `apps.cfg` on drive `8` for the selected profile.

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
| 8 | `cal26` | calendar 26 | 2026 calendar with month, week, day, upcoming, and REL-backed appointments; task reading is currently broken |
| 8 | `dizzy` | dizzy kanban | Kanban board with REL-backed persistence, search, and reorder |
| 9 | `readme` | read.me | In-system ReadyOS guide viewer |
| 8 | `readyshell` | ready shell (demo) | Overlay-based shell POC/demo |
| 8 | `deminer` | deminer | Minesweeper-style puzzle with suspend/resume |

Notes:

- `readyshell` uses overlay files on disk 1 and should still be treated as a
  demo path rather than a stable daily-use app.
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
- bank `1`: shared clipboard
- banks `2-25`: app slots (`24` total)
- higher banks: dynamic allocation pool
- banks `$40-$43`: fixed ReadyShell overlay/debug banks

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
- `bash ./run.sh --skipbuild`
  launch using the latest built artifacts for the selected profile
- `make`
  build the default profile release package
- `make release-all`
  build all release profiles with one version stamp
- `make verify`
  run the repo verification and host smoke checks

Notes:

- `run.sh` is the preferred local workflow because it rebuilds disks, updates
  generated assets, and preserves managed disk state correctly.
- Manual launch should match the setup in the Getting Started section.
- Windows support is still less exercised than the Unix shell path.

## Generated Assets And Public Docs

Generated build-owned assets include:

- `apps.cfg` from the selected `cfg/profiles/*.ini` source
- `src/generated/readme_pages.c` and `src/generated/readme_pages.h` from
  `src/apps/readme/readme_lite.md`
- `src/generated/build_version.h` and `src/generated/msg_version.inc` from the
  local run/build flow

Normal profile rebuilds preserve non-managed user files from the prior
profile build while replacing build-owned artifacts in `releases/<version>/<profile>/`.

Public supporting docs in `docs/` currently include:

- `docs/clipboard_bundle_seq_format.md`
- `docs/quicknotes_seq_format.md`
- `docs/simplecells_seq_format.md`

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
