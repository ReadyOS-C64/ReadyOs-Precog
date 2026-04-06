# ReadyOS

ReadyOS is a Commodore 64 application environment built with `cc65`, dual `D71`
disk images, and a 16MB REU. Its main goal is fast switching between
full-screen apps by treating the app runtime window as a swappable memory image
while keeping a small resident shim and shared metadata alive outside that
window.

ReadyOS PRECOG is the current experimental path toward ReadyOS itself, with a
particular focus on the C64 Ultimate as a machine that could feel ready for
real use, not just nostalgic use. The core value proposition is an REU-first,
keyboard-first, terminal-style environment with instant app switching,
suspend/resume, shared clipboard and history, and deeper links between apps.

The broader vision is a fast C64 workflow where "READY" means responsive,
reliable, and repeatable, and where this environment can later pair with
Ultimate Buddy as a LAN companion for loading, mounting, proxying services, and
supporting a more modern development loop.

Project vision and public overview: https://readyos.notion.site/

The project includes end-user apps, reusable TUI and REU support libraries, a
bootloader with a fixed shim ABI, and repo-local build support scripts for disk
generation, validation, and generated assets.

## What Is In This Repo

- Boot chain and shim implementation for ReadyOS on C64
- A launcher plus a set of keyboard-first apps
- TUI micromodules in `src/lib` shared by multiple apps
- REU snapshot, warm-resume, and clipboard infrastructure
- Repo-local build support in `build_support/`
- Root build/run entry points: `Makefile` and `run.sh`

This repo now owns the core build toolchain. The standard build path no longer
depends on the external debug-tool repo for asset generation, disk preservation,
or verification. A few specialized probe/debug workflows may still point to
external tooling, but normal build and run flows are local.

## App Catalog

The normal app catalog is defined in `cfg/apps_catalog.txt` and is written to
disk as `apps.cfg`.

| Drive | Program | Display Name | Purpose |
| --- | --- | --- | --- |
| 9 | `editor` | editor | Text editor with clipboard support |
| 9 | `calcplus` | calc plus | Keyboard-first expression calculator |
| 9 | `hexview` | hex viewer | Browse memory in hex form |
| 9 | `clipmgr` | clipboard | Manage clipboard items in REU |
| 9 | `reuviewer` | reu viewer | Inspect REU allocation and usage |
| 9 | `tasklist` | task list | Outline and notes app with search |
| 9 | `game2048` | 2048 game | 2048 tile puzzle |
| 8 | `deminer` | deminer | Minesweeper-style puzzle |
| 8 | `cal26` | calendar 26 | 2026 calendar with REL-backed appointments |
| 8 | `dizzy` | dizzy kanban | Kanban-style task board |
| 9 | `readme` | read.me | In-system project and architecture guide |
| 8 | `readyshell` | ready shell | ReadyShell POC app with overlays |

There is also an internal diagnostics harness:

- `xrelchk`: REL-file test program used for CAL26 transport and regression
  work. It is a harness, not part of the normal launcher catalog.

## The Config File

ReadyOS uses `apps.cfg` as the launcher-visible app catalog on disk.

### Source And Generation

- Human-edited source: `cfg/apps_catalog.txt`
- Generated disk payload: `apps.cfg`
- Generator: `build_support/build_apps_catalog_petscii.py`

The source file is written as lowercase UTF-8 text and converted into the
PETASCII-oriented SEQ payload that the C64-side launcher expects.

### How It Loads

At build time, `apps.cfg` is generated and written to disk 8 as a managed SEQ
file. At runtime, the launcher reads `apps.cfg` from drive 8 to determine:

- which apps appear in the launcher
- which drive each app lives on
- the display label shown to the user
- the one-line description text

`apps.cfg` is treated as build-owned. It is regenerated on rebuild and is not
preserved as user data the way custom user-created files are.

If you need to inspect what the built config looks like on the target system,
ReadyOS includes `showcfg`, a BASIC inspector for the `apps.cfg` contents on
drive 8.

## Architecture Overview

### Runtime Model

ReadyOS splits memory into a large app window and a small persistent resident
area:

- App runtime snapshot window: `$1000-$C5FF`
- REU metadata/system table: `$C600-$C7FF`
- Resident shim: `$C800-$C9FF`
- Hardware I/O region: `$D000-$DFFF`

The active app owns `$1000-$C5FF`. The shim stashes and fetches exactly
`$B600` bytes between C64 RAM and REU so apps can be preloaded, resumed, or
switched without sharing in-process state the way a conventional multitasking
OS would.

### The Shim

The shim is installed by the bootloader and lives permanently at
`$C800-$C9FF`. It provides a stable jump table used by the launcher and apps
for operations such as:

- load from disk and run
- fetch from REU and run
- preload to REU
- return to launcher
- switch directly to another app
- stash/fetch helpers

The shim exists outside the swappable app window so it can safely orchestrate
REU transfers even when those transfers overwrite the currently running app
image. This is why preload and switch behavior must be reasoned about through
the launcher+shim flow, not as standalone app behavior.

### REU Layout

ReadyOS uses REU as the backing store for app images, shared state, and fixed
ReadyShell overlay banks.

- Bank `0`: launcher/system image
- Bank `1`: shared clipboard
- Banks `2-17`: app slots
- Higher banks: dynamic pool
- Banks `$40-$43`: fixed ReadyShell overlay/debug banks

Each app bank stores:

- `$0000-$B5FF`: app snapshot image for `$1000-$C5FF`
- `$B600-$FFFF`: warm-resume payload tail

That warm-resume tail is managed by `resume_state` and must end exactly at the
64KB bank boundary.

### TUI Micromodules

The TUI library is intentionally split into small focused modules rather than a
single monolith. Common pieces in `src/lib` include:

- `tui_core`: screen/color primitives and basic drawing
- `tui_window`: framed windows and titles
- `tui_menu`: menu rendering and selection
- `tui_input`: input field editing
- `tui_nav`: global app navigation helpers
- `tui_misc`: progress bars and small utility widgets

The broader library set follows the same small-module pattern:

- `reu_mgr_*`: REU initialization, allocation, DMA, and stats helpers
- `clipboard_*`: copy/paste/count/admin helpers
- `resume_state`: warm-resume storage contract
- `ready_os`: syscall wrappers into the shim ABI

Some older aggregate files still exist in `src/lib`, but current builds are
composed primarily from these smaller modules.

### Keyboard Repeat Policy

ReadyOS defaults to `KBREPEAT_NONE` during normal TUI/app operation.

- This keeps warp/turbo execution from turning a held key into repeated spaces,
  repeated letters, or multi-step menu jumps.
- `tui_init()` restores that default for standard apps that link `tui_core`.
- Direct `cgetc()` paths that bypass `tui_core` must set the same policy
  manually.

Apps that genuinely benefit from held keys can opt in temporarily:

- Use `tui_keyrepeat_set(TUI_KEYREPEAT_CURSOR)` when only held cursor movement
  should repeat.
- Use `tui_keyrepeat_set(TUI_KEYREPEAT_ALL)` only for continuous-control loops
  that really want full typematic behavior.
- Call `tui_keyrepeat_default()` before entering menus, dialogs, prompts, or
  text-entry flows.

Current example:

- `game2048` enables cursor-only repeat during active play, then restores the
  ReadyOS default for pause and non-gameplay states.

### ReadyShell Overlays

ReadyShell is a POC shell app that uses overlays loaded below `__HIMEM__`.
The current overlay window is profile-driven:

- release/default: `READYSHELL_PARSE_TRACE_DEBUG=0`
- debug trace: `READYSHELL_PARSE_TRACE_DEBUG=1`

The overlay size, start address, and fixed REU overlay banks are part of the
memory contract and are validated by the repo’s verification scripts.

## Build And Run

### Requirements

- `cc65` toolchain: `cl65`, `ca65`, `ld65`
- VICE tools, especially `x64sc`, `c1541`, and `petcat`
- `python3`
- On macOS/Homebrew, VICE may need GLib schema environment setup. `run.sh`
  handles the common Homebrew schema path automatically on this machine class.

### Main Entry Points

- `bash ./run.sh`
  - Rebuild both `D71` images and launch ReadyOS in VICE
- `pwsh -File ./run.ps1`
  - Rebuild both `D71` images and launch ReadyOS in VICE via PowerShell
- `bash ./run.sh --skipbuild`
  - Launch using existing binaries and disks
- `bash ./run.sh readyshell-mon`
  - Run with remote monitor sockets/logging for ReadyShell diagnosis
- `make`
  - Build programs and both disk images
- `make verify`
  - Run the repo’s verification gates after build

### Windows Status

Windows support is not yet complete or fully tested.

Current state:

- `run.ps1` exists and is intended to be the future Windows-friendly entry
  point.
- The PowerShell-driven disk rebuild path is partly implemented.
- The Unix shell path (`run.sh`, `build.sh`, and parts of `Makefile`) still
  assumes POSIX tools and shell behavior.
- Some helper scripts and verification flows still assume tool names and
  behaviors common on macOS/Linux, such as `python3`, `make`, and direct VICE
  CLI usage.
- The full Windows build, verification, and interactive VICE launch flow has
  not been validated end-to-end yet.

For now, treat Windows support as in progress rather than supported.

### Running In VICE

The normal supported path is:

- `bash ./run.sh`
- `pwsh -File ./run.ps1`

Either entry point rebuilds the disk images, attaches:

- `readyos.d71` as drive 8
- `readyos_2.d71` as drive 9

and launches VICE with REU enabled at 16MB.

If launching manually in VICE, make sure all of the following are true:

- drive 8 is mounted to `readyos.d71`
- drive 9 is mounted to `readyos_2.d71`
- REU is enabled
- REU size is set to 16MB
- boot starts from `preboot.prg`

`run.sh` is the preferred entry point because it also handles generated
artifacts, disk preservation, and the environment setup needed by the local
VICE install.

### Running On A Commodore 64 Ultimate

For real hardware on a C64 Ultimate-family setup, mount both disk images and
match the tested REU configuration:

- mount `readyos.d71` as drive 8
- mount `readyos_2.d71` as drive 9
- enable the REU
- set REU size to 16MB if possible

The project has only been exercised with the REU enabled and with 16MB as the
tested size so far. Smaller or different REU configurations should be treated
as unverified until explicitly tested.

The dual-disk and REU setup matters because ReadyOS expects:

- the launcher catalog and boot-side apps on drive 8
- the remaining apps on drive 9
- REU-backed stash/fetch behavior for app switching and resume

### Build Support

Repo-local helper scripts live in `build_support/`. They handle:

- `apps.cfg` generation from `cfg/apps_catalog.txt`
- README app asset generation from markdown-lite source
- disk user-data preservation during rebuild
- CAL26 REL seeding and recovery helpers
- memory-map and resume-contract verification

`run.sh` resolves `build_support/` first and passes that path into `make`.

### Generated README App Assets

The in-system README app is generated from:

- Source markdown-lite: `src/apps/readme/readme_lite.md`
- Generator: `build_support/build_readme_app_assets.py`
- Helper parser: `build_support/readme_lite_common.py`
- Generated outputs: `src/generated/readme_pages.h` and
  `src/generated/readme_pages.c`

Updating the README app content means editing `readme_lite.md` and rebuilding.

### Disk Rebuild Nuances

`run.sh` and the disk-image rules are designed to preserve user data already
present on existing `D71` images.

- Build-managed files are regenerated and replaced
- Non-managed `PRG`, `SEQ`, `REL`, and `USR` files are preserved
- `apps.cfg` is treated as build-owned and regenerated
- CAL26 REL files are seeded and may then be restored from a donor disk if the
  configured donor image exists

This means saved user files such as task data are intended to survive normal
rebuilds, while OS-owned catalog/build artifacts are rebuilt.

### Versioning

`run.sh` writes generated version metadata into:

- `src/generated/build_version.h`
- `src/generated/msg_version.inc`

It uses a rolling suffix written to `/tmp/readyos_run_version_suffix.txt`.
`--skipbuild` preserves the currently embedded version strings because it avoids
regenerating binaries.

## Verification And Source Of Truth Docs

For deeper implementation detail, start with:

- `MEMORY_MAP.md`: canonical memory contract
- `SHIM_PLAN.md`: shim control-flow model and lessons learned
- `IMPLEMENTATION_PLAN.md`: broader system design notes

Current verification gates include:

- `verify.py`
- `build_support/verify_resume_contract.py`
- `build_support/verify_memory_map.py`

These checks are intended to catch memory drift, REU/resume contract breakage,
and undocumented fixed-address usage.

## Repository Layout

- `src/boot`: bootloader and embedded shim
- `src/apps`: launcher and user-facing apps
- `src/lib`: TUI, REU, clipboard, resume, and shim wrapper modules
- `cfg`: linker configs and app catalog input
- `build_support`: local build-chain support scripts and validation helpers
- `docs`: supporting reports and research artifacts
- `logs`: VICE logs and captured automation output

## Notes For Contributors

- Treat the launcher+shim flow as the real execution model for app switching.
- Respect the `$1000-$C5FF` app window and the fixed shim/metadata regions.
- For CAL26 REL work, use `xrelchk` and documented harness evidence rather than
  copying behavior from unrelated apps.
- Keep changes to fixed addresses, REU contracts, and overlay sizes in sync with
  the memory-map documentation and verification scripts.

## License

ReadyOS is licensed under the MIT License. See `LICENSE` for the full text.
Copyright (c) 2026 Karl Prosser.
