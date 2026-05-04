# ReadyOS

ReadyOS PRECOG is an experimental REU-first environment for a modern
Commodore 64 setup. Its long-term center of gravity is the new Commodore 64
Ultimate and related Ultimate-family hardware, but it is intended to support a
wide range of C64 setups that have a reasonably large REU. That includes VICE,
Ultimate-family hardware, and other practical REU-capable modern paths. PRECOG
`0.2.1` is the current public release line.

The current `0.2.1` release is still comparatively generic rather than being
explicitly tailored to the new C64 Ultimate. The next release is expected to
push further in that Ultimate-first direction while still trying to stay usable
on other REU-capable C64 setups.

## The Concept

What if a Commodore 64 could feel ready, not just nostalgic? ReadyOS treats
waiting as the enemy. It is a keyboard-first, full-screen terminal-style
environment built around instant app switching, suspend/resume, shared
clipboard and history, and deeper links between apps. The goal is a C64
workflow where READY means responsive, reliable, and repeatable.

At a glance:

- requires an REU-backed modern C64 path and is tested at `16MB` REU
- main product direction: new Commodore 64 Ultimate and Ultimate-family workflows
- practical secondary path today: VICE with REU enabled
- still intended to support other C64 setups with a decent-sized REU
- tuned to stay usable from `1MHz` up through `48MHz`
- ships multiple release SKUs so the runtime can fit `D64`, `D71`, `D81`, and EasyFlash cartridge workflows
- emphasizes "instant" app switching with apps suspended in the REU

Project links:

- GitHub: https://github.com/ReadyOS-C64/ReadyOs
- Homepage: https://readyos64.com
- Wiki: https://readyos.notion.site/

<img width="715" height="540" alt="image" src="https://github.com/user-attachments/assets/2053305a-46fe-4335-8394-9cb949982788" />
<img width="715" height="540" alt="image" src="https://github.com/user-attachments/assets/8bcc8e00-8b6c-4de6-a0cd-b03b740b6a11" />
<img width="715" height="540" alt="image" src="https://github.com/user-attachments/assets/d403df2f-0aa8-4dd3-aa71-495bbd41a638" />
<img width="715" height="540" alt="image" src="https://github.com/user-attachments/assets/943470cc-10ef-483e-8edd-770c00407cbb" />
<img width="715" height="540" alt="image (7)" src="https://github.com/user-attachments/assets/741b1a49-8d95-43c7-aaa3-e752fb5933a6" />

## Getting Started

The canonical release layout is:

- `releases/<version>/precog-easyflash/`
- `releases/<version>/precog-dual-d71/`
- `releases/<version>/precog-d81/`
- `releases/<version>/precog-dual-d64/`
- `releases/<version>/precog-solo-d64-a/`
- `releases/<version>/precog-solo-d64-b/`
- `releases/<version>/precog-solo-d64-c/`
- `releases/<version>/precog-solo-d64-d/`
- `releases/<version>/precog-solo-d64-e/`

Targets:

- VICE
- THEC64 Mini / Maxi
- Commodore 64 Ultimate-family hardware such as C64 Ultimate, Ultimate 64, or
  Ultimate Cart

So far tested in VICE and the Commodore 64 Ultimate.

Recommended baseline:

- enable the REU
- set REU size to `16MB`
- follow the `helpme.md` inside the selected `releases/<version>/<profile>/` directory

Boot note:

- disk SKUs boot with the documented `PREBOOT` chain for that profile
- `precog-easyflash` boots by mounting `readyos_data.d64` on drive `8`,
  attaching `readyos_easyflash.crt`, and resetting into the cartridge
- during that boot, the on-screen cartridge label reads `precog cartridge (beta)`

## Current Status

- Base release: `0.2.1`
- Local builds use the existing rolling suffix flow for artifact filenames only
- Builds release media per profile
- Currently includes `16` apps with `24` app slots reserved in the REU

## Release Variants

ReadyOS now ships the same runtime in `9` public media variants because the
target drive types, disk capacities, and cartridge support are different.

| Profile | Media | Why It Exists | Boot Flow | App Set |
| --- | --- | --- | --- | --- |
| `precog-easyflash` | `CRT` cartridge plus companion `D64` on drive `8` | full cartridge cold-boot path for VICE and Ultimate-family setups that can keep a disk mounted | reset into cartridge boot | full current app catalog |
| `precog-dual-d71` | two `D71` images on drives `8` and `9` | default full-content profile for `1571` setups and the main local verification target | `PREBOOT -> SETD71 -> BOOT` | full current app catalog |
| `precog-d81` | one `D81` image on drive `8` | full-content single-disk profile for `1581`/`D81` setups | `PREBOOT -> BOOT` | full current app catalog |
| `precog-dual-d64` | two `D64` images on drives `8` and `9` | reduced profile for `1541`-compatible capacity limits | `PREBOOT -> BOOT` | curated subset of the current app catalog |
| `precog-solo-d64-a` | one `D64` image on drive `8` | standalone single-disk subset with editor, reference, and dizzy | `PREBOOT -> BOOT` | `editor`, `hexview`, `readme`, `dizzy` |
| `precog-solo-d64-b` | one `D64` image on drive `8` | standalone single-disk productivity subset with quicknotes, calculator, clipboard, and files | `PREBOOT -> BOOT` | `quicknotes`, `calcplus`, `clipmgr`, `simplefiles` |
| `precog-solo-d64-c` | one `D64` image on drive `8` | standalone single-disk planning subset with tasklist, calendar, and REU viewer | `PREBOOT -> BOOT` | `tasklist`, `cal26`, `reuviewer` |
| `precog-solo-d64-d` | one `D64` image on drive `8` | standalone single-disk experimental subset with simple cells, calculator, 2048, and deminer | `PREBOOT -> BOOT` | `simplecells`, `calcplus`, `game2048`, `deminer` |
| `precog-solo-d64-e` | one `D64` image on drive `8` | standalone single-disk ReadyShell-focused subset for one-disk-only environments | `PREBOOT -> BOOT` | `readyshell` and its shell-focused subset |

The cartridge SKU has one important nuance: `readyos_data.d64` is still part of
the expected runtime. The cartridge contains the EasyFlash boot code and the
preloaded payloads, while drive `8` remains the normal disk-backed place for
runtime files, help content, and app data.

The cartridge SKU also now performs an explicit early REU check. If REU is not
present, the boot loader shows a clear error, waits for a keypress, and returns
to BASIC cold start instead of trying to continue.

The dual-D64 profile is intentionally smaller. Right now it keeps the core
productivity path that fits on two `D64`s: `editor`, `quicknotes`,
`calcplus`, `clipmgr`, `tasklist`, `simplefiles`, `game2048`, `sidetris`,
`deminer`, and `cal26`.

The solo-D64 variants exist for environments that can mount only one `D64`
at a time, such as some web emulators and simplified media loaders. The split
is intentional.

### EasyFlash Boot Colors

The `precog-easyflash` cold boot now uses border colors so the long preload is
visibly doing work.

- light blue border: loader setup and general control flow
- green border: shim install and shared-state setup
- yellow border: cartridge-to-RAM copy
- orange border: RAM-to-REU stash or REU restore
- light green border: final launcher handoff
- red border: REU missing, waiting for keypress to return to BASIC

The blue background remains constant. Long yellow or orange phases are expected
and mean the machine is still preloading launcher, app, and overlay snapshots.

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
  human-readable description on the next line. Used a lot for automated app testing.
- In `9:editor:editor:1`, `9` is the source drive, `editor` is the PRG token,
  the second `editor` is the launcher label, and the trailing `1` is the
  default hotkey slot. The parser accepts slot values `1..9`; omitting the
  fourth field means no default slot binding.
- A catalog entry such as `8:readyshell:readyshell (beta):2` assigns ReadyShell
  to launcher hotkey slot `2` at boot, even though ReadyShell does not currently
  support runtime rebinding from inside the shell.
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

## Global App Hotkeys

ReadyOS supports up to nine direct app hotkey slots.

- `CTRL+1` through `CTRL+9` launch or switch to the app bound to that slot.
- `CTRL+SHIFT+1` through `CTRL+SHIFT+9` bind the current app to that slot at
  runtime in apps that use the shared hotkey handler.
- The same slot numbers can be seeded at boot from `cfg/profiles/*.ini` by
  adding the optional fourth `:slot` field in the `[apps]` section.
- Apps with their own raw keyboard input loops may not support runtime rebinding
  even though launcher-configured default slots still work.

Real C64 versus emulator key forms:

- On a real C64, launch uses `CTRL` plus the number key.
- On a real C64, bind uses `CTRL+SHIFT` plus the number key. Those are the same
  physical keys that print shifted digit symbols: `! " # $ % & ' ( )`.
- In emulators, host keymaps vary. Some map cleanly to `CTRL+SHIFT+<digit>`,
  while others deliver `CTRL+!`, `CTRL+"`, `CTRL+#`, and so on.
- ReadyOS accepts both forms in apps that support runtime rebinding, so use
  whichever chord your emulator keymap produces.

| Drive | Program | Display Name | Current Role |
| --- | --- | --- | --- |
| 9 | `editor` | editor | Text editor with selection abilities, clipboard, find, and disk save/open |
| 8 | `quicknotes` | quicknotes | Split-pane REU-backed notes with save/open and search |
| 9 | `calcplus` | calc plus | Expression calculator with history, modes, variables, and clipboard |
| 9 | `hexview` | hex viewer | Memory browser with PETSCII and screen-code views |
| 9 | `clipmgr` | clipboard | Multi-item clipboard manager with preview and file import/export |
| 9 | `reuviewer` | reu viewer | Visual 256-bank REU map |
| 9 | `tasklist` | task list | Hierarchical outliner with notes, search, and file persistence |
| 9 | `simplefiles` | simple files | Dual-pane file manager with copy, rename, delete, and SEQ previewing |
| 9 | `simplecells` | simple cells (alpha) | Single-sheet spreadsheet with formulas, formatting, and save/load |
| 9 | `game2048` | 2048 game | 2048 puzzle game with resume/app switching |
| 9 | `sidetris` | sidetris | Sideways block-drop game with suspend/resume |
| 8 | `cal26` | calendar 26 | 2026 calendar with month, week, day, upcoming, and REL-backed appointments |
| 8 | `dizzy` | dizzy kanban | Kanban board with REL-backed persistence, search, and reorder |
| 9 | `readme` | read.me | In-system ReadyOS guide viewer |
| 8 | `readyshell` | ready shell | A command line language for the c64, with many file commands, but also a robust object pipeline programing language shell with wildcard directory queries, and text/file commands including `cat`, `put`, `add`, `del`, `ren`, and `copy` |
| 8 | `deminer` | deminer | Minesweeper-style puzzle with suspend/resume |

Notes:

- ReadyShell guide: [src/apps/readyshell/README.md](src/apps/readyshell/README.md)
- ReadyShell tutorial: [src/apps/readyshell/ReadyShelltutorial.md](src/apps/readyshell/ReadyShelltutorial.md)
- ReadyShell architecture: [docs/ReadyShellArchitecture.md](docs/ReadyShellArchitecture.md)
- ReadyShell overlay inventory: [docs/readyshell_overlay_inventory.md](docs/readyshell_overlay_inventory.md)
- ReadyShell now ships eight overlays allowing language and command functionality that otherwise couldn't fit into the memory of a c64: `rsparser`, `rsvm`, `rsdrvilst`,
  `rsldv`, `rsstv`, `rsfops`, `rscat`, and `rscopy`.
- ReadyShell preloads and REU-caches all eight overlays at startup. Bank `$40`
  holds overlays `1`, `2`, `3`, and `5`; bank `$41` holds overlays `4`, `6`,
  `7`, and `8`.
- Current ReadyShell command set: `PRT`, `MORE`, `TOP`, `SEL`, `GEN`, `TAP`,
  `DRVI`, `LST`, `LDV`, `STV`, `CAT`, `PUT`, `ADD`, `DEL`, `REN`, and `COPY`.
- `LST` accepts wildcard patterns, optional drive selection, and optional
  comma-separated file-type filters such as `PRG`, `SEQ`, `USR`, and `REL`.
- `LDV` and `STV` accept either embedded drive syntax like `"9:snap"` or a
  trailing drive argument like `"snap", 9`.
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
- `python3` for building and bash
- only tested "on my machine" on macOS. The PowerShell build script is likely obsolete.

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
  run the repo verification, app host smoke checks, and the full
  `readyshell-host-tests` suite
- `make readyshell-host-tests`
  run the full host-side ReadyShell parser, VM, overlay-command, and REU tests
- `make readyshell-parse-smoke-host`
  run the fast host-side ReadyShell parser smoke checks
- `make readyshell-vm-smoke-host`
  run the host-side ReadyShell VM smoke checks, including the overlay-aware
  C64-flavored harness build and mocked file-command coverage
- `make readyshell-reu-tests-host`
  run the host-side ReadyShell REU heap/value and RSV1 serialization tests

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
- `make verify` now runs the full `readyshell-host-tests` aggregate target,
  which includes the parser, VM/overlay, and REU ReadyShell host checks.
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

## Notes For Contributors

- Treat the launcher + shim path as the real execution model for ReadyOS.
- Respect the fixed app window and resident shim/system regions.
- Use `run.sh`, `run.ps1`, `make`, and `make verify` for normal public
  build-and-check workflows.
- look at existing apps to see the "micromodule layout"
- Apps should always be compiled with the full OS for now (so we don't have to support backwards compatibility with the ABI, and REU patterns change)

## License

ReadyOS is licensed under the MIT License. See `LICENSE` for the full text.
Copyright (c) 2026 Karl Prosser.
