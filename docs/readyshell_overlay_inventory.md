# ReadyShell Overlay Inventory Report (v0.2Y)

Artifact-backed report generated from the current local ReadyShell build, linker map, and D71 disk image.

## Executive Summary

- Profile / disk source: `precog-dual-d71` using `releases/0.2/precog-dual-d71/readyos-v0.2y-dual-d71_1.d71` (disk label `readyos`, `110` blocks free).
- Resident ReadyShell PRG: `readyshell.prg` on disk as `readyshell`, `31382` bytes and `124` D71 blocks.
- Overlay execution window: `$8E00-$C5FF` for `14336` bytes, with PRG load-address bytes at `$8DFE-$8DFF`.
- Resident BSS / heap below overlays: BSS `$8A94-$8C8A` (`503` bytes), heap `$8C8C-$8DFD` (`370` bytes).
- High RAM runtime region outside the app window: `$CA00-$CFFF`.
- REU policy split:
  - overlays 1-2 are boot-loaded from disk and cached into one shared REU bank using fixed full-window slots
  - overlays 3-8 are loaded from disk on demand for each command call
  - bank 0x48 is shared for the external-command registry, overlay metadata, pause state, command handoff scratch, and the REU-backed ReadyShell value arena

## Runtime Memory Map

| Region | Range | Size | Notes |
| --- | --- | ---: | --- |
| Resident app window | `$1000-$C5FF` | `46592` | ReadyOS app-owned RAM window for ReadyShell. |
| Overlay load address bytes | `$8DFE-$8DFF` | `2` | PRG load address emitted ahead of each overlay sidecar file. |
| Overlay execution window | `$8E00-$C5FF` | `14336` | Shared live area for whichever overlay is active. |
| Resident BSS | `$8A94-$8C8A` | `503` | Resident writable data below the overlay load address. |
| Resident heap | `$8C8C-$8DFD` | `370` | cc65 heap carved below the overlay load address. |
| High-RAM runtime | `$CA00-$CFFF` | `1536` | Fixed ReadyShell runtime state outside the app snapshot window. |

## REU Layout And Loading Model

| Use | REU range | Size | How it is used |
| --- | --- | ---: | --- |
| Shared cache bank | `$400000-$40FFFF` | `65536` | One bank reserved for both core overlays and their free tail. |
| Overlay 1 parse slot | `$400000-$4037FF` | `14336` | Full overlay-window snapshot for overlay 1. |
| Overlay 2 exec slot | `$403800-$406FFF` | `14336` | Full overlay-window snapshot for overlay 2. |
| Shared bank free tail | `$407000-$40FFFF` | `36864` | Currently unused tail after the two core overlay slots. |
| Debug trace ring | `$43F000-$43F20F` | `528` | Overlay debug markers and verification state. |
| Command scratch | `$480000-$487FFF` | `32768` | Inter-overlay handoff area for command frames and streaming state. |
| Command registry header | `$488010-$488017` | `8` | REU-backed external-command registry header. |
| Command descriptor table | `$488020-$48807F` | `96` | Fixed-capacity external-command descriptor table in REU metadata. |
| Overlay state table | `$488080-$4880EB` | `108` | Fixed-capacity overlay load/cache state table for external command overlays. |
| Shared ReadyShell metadata | `$4880F0-$4880FB` | `12` | Shared core-overlay cache metadata record. |
| Pause flag | `$4880FC` | `1` | Shared output-pause bit used by resident output and `MORE`. |
| REU heap metadata | `$488000-$4880FF` | `256` | ReadyShell REU heap header region, including shared metadata bytes. |
| REU heap arena | `$488100-$48FEFF` | `32256` | Persistent value payload arena for REU-backed strings/arrays/objects. |

## Shared Core Overlay Cache Visual

```text
REU bank 0x40

+----------------------------------------+ $400000
| overlay 1 parse slot                   |
| full overlay-window image: 0x3800      |
| active file: rsparser.prg              |
+----------------------------------------+ $403800
| overlay 2 exec slot                    |
| full overlay-window image: 0x3800      |
| active file: rsvm.prg                  |
+----------------------------------------+ $407000
| free tail                              |
| 0x9000 bytes                           |
+----------------------------------------+ $40FFFF
```

## Command Scratch And Value Arena Usage

| Commands | Overlay | Command scratch | Value arena | How the REU region is used |
| --- | --- | --- | --- | --- |
| PRT, MORE, TOP, SEL, GEN, TAP | `2 / rsvm` | No direct use | Indirect only | Run inside the shared execution core. They do not stage command-local data in `$480000-$487FFF`; any REU-backed values are handled through the normal overlay-2 value/runtime paths. |
| DRVI | `3 / rsdrvilst` | No | No | Reads drive header/status data and builds its output object in transient overlay-local RAM. |
| LST | `3 / rsdrvilst` | Yes | No | Spools 28-byte directory records through `$480000-$487FFF` so `BEGIN`/`ITEM` can walk the listing without keeping the directory channel open. |
| LDV | `4 / rsldv` | Yes | Yes, writes persistent values | Reads the RSV1 file into `$480000-$487FFF`, validates its header, then materializes strings, arrays, and objects into the REU heap arena `$488100-$48FEFF`. |
| STV | `5 / rsstv` | Yes | Yes, reads existing pointer values | Uses `$480000-$4800FF` for session metadata and `$480100-$487FFF` for the outgoing RSV1 payload. When serializing pointer-backed values, it dereferences them from the persistent REU heap arena before flattening them into scratch. |
| DEL, REN | `6 / rsfops` | No | No | Issue DOS scratch/rename commands directly through command-channel I/O with no REU staging. |
| PUT, ADD | `6 / rsfops` | Yes | No | Use `$480000-$48001F` for session metadata and `$480020-$487FFF` as a text spool for new/appended SEQ content before writing it back to disk. |
| CAT | `7 / rscat` | Yes | No | Uses `$480000-$4807FF` as a line-record table and `$480800-$487FFF` as the line-data spool so `ITEM` can replay file lines after the initial read pass. |
| COPY | `8 / rscopy` | No | No | Uses its overlay-local 128-byte transfer buffer plus direct DOS copy/streamed file I/O. It does not use the shared command scratch or the persistent value arena. |

- The command scratch window is shared, not partitioned per overlay. Only one command overlay owns it at a time because command overlays are disk-loaded serially.
- The value arena is persistent session state in bank `0x48`. `LDV` populates it explicitly, while `STV` can serialize values already living there.

## Static Audit Checks

- Registry capacity check: `rs_cmd_registry.c` seeds `10` external command descriptors into `16` reserved descriptor slots and `6` overlay-state rows into `6` reserved state slots.
- Metadata-page packing check: the full ReadyShell metadata block fits inside `$488000-$4880FF`. Header uses `$488010-$488017`, descriptor rows reserve `$488020-$48807F` with live rows ending at `$48805B`, state rows reserve `$488080-$4880EB` with live rows ending at `$4880EB`, shared metadata uses `$4880F0-$4880FB`, and the pause flag sits at `$4880FC`.
- Non-overlap check: command scratch ends at `$487FFF` and REU heap metadata begins at `$488000`; the state table ends at `$4880EB` and shared metadata begins at `$4880F0`; shared metadata ends at `$4880FB` and the pause flag is `$4880FC`; the value arena begins at `$488100`.
- Command-source audit: `DRVI` builds output only in overlay-local RAM; `LST` writes 28-byte directory records into shared scratch; `LDV` streams RSV1 payloads into scratch and materializes persistent values into the REU heap arena; `STV` serializes into scratch and dereferences pointer-backed values from the arena; `DEL` and `REN` issue direct DOS commands without REU staging; `PUT` and `ADD` use scratch metadata plus a text spool; `CAT` uses a scratch record table plus line-data spool; `COPY` stays overlay-local with `g_copy_buf[128]`.

## Overlay Inventory

| Ovl | Role | Build PRG | Disk name | PRG bytes | Disk blocks | Live bytes | Window use | REU cache | Commands |
| ---: | --- | --- | --- | ---: | ---: | ---: | ---: | --- | --- |
| 1 | Parser / Lexer | `rsparser.prg` | `rsparser` | `13007` | `52` | `13005` | `90.7%` | bank `0x40` slot `$400000-$4037FF` | None directly; parse phase support. |
| 2 | Execution Core | `rsvm.prg` | `rsvm` | `14035` | `56` | `14033` | `97.9%` | bank `0x40` slot `$403800-$406FFF` | PRT, MORE, TOP, SEL, GEN, TAP and the shared execution paths that command overlays return to. |
| 3 | Drive Info + Directory Listing | `rsdrvilst.prg` | `rsdrvilst` | `7745` | `31` | `7743` | `54.0%` | disk-only | DRVI, LST |
| 4 | Load Value | `rsldv.prg` | `rsldv` | `11156` | `44` | `11154` | `77.8%` | disk-only | LDV |
| 5 | Store Value | `rsstv.prg` | `rsstv` | `8972` | `36` | `8970` | `62.6%` | disk-only | STV |
| 6 | File Delete / Rename / Write | `rsfops.prg` | `rsfops` | `13235` | `53` | `13233` | `92.3%` | disk-only | DEL, REN, PUT, ADD |
| 7 | File Read | `rscat.prg` | `rscat` | `6457` | `26` | `6455` | `45.0%` | disk-only | CAT |
| 8 | File Copy | `rscopy.prg` | `rscopy` | `6601` | `26` | `6599` | `46.0%` | disk-only | COPY |

## Command Topology

```text
Resident ReadyShell dispatcher
  |
  +-- Overlay 2  rsvm       [shared execution core]
  |      commands: PRT | MORE | TOP | SEL | GEN | TAP
  |      note: shared execution paths that command overlays return to
  |
  +-- Overlay 3  rsdrvilst  [shared command overlay]
  |      commands: DRVI | LST
  |      note: multiple commands share one disk sidecar and one RAM image
  |
  +-- Overlay 4  rsldv      [single-command overlay]
  |      commands: LDV
  |
  +-- Overlay 5  rsstv      [single-command overlay]
  |      commands: STV
  |
  +-- Overlay 6  rsfops     [shared command overlay]
  |      commands: DEL | REN | PUT | ADD
  |      note: multiple commands share one disk sidecar and one RAM image
  |
  +-- Overlay 7  rscat      [single-command overlay]
  |      commands: CAT
  |
  `-- Overlay 8  rscopy     [single-command overlay]
         commands: COPY
```

- `DRVI` and `LST` now co-reside in `rsdrvilst`, so both commands load the same disk sidecar and the same overlay image.
- Overlays 3-8 remain disk-loaded command overlays; only overlays 1-2 are REU-cached.

## Resident Program

- Build PRG: `readyshell.prg`
- Disk filename: `readyshell`
- Disk staging comes from the main ReadyShell build artifact, not an overlay copy.
- Resident sources: `readyshellpoc.c, rs_token.c, rs_bc.c, rs_errors.c, rs_cmd_registry.c, rs_vm_c64.c, rs_overlay_c64.c, rs_platform_c64.c, rs_screen_c64.c, tui_nav.c, reu_mgr_dma.c, resume_state_ctx.c, resume_state_core.c`
- Resident asm/runtime support: `rs_runtime_c64.s`
- Command role: Resident app shell loop plus vm/overlay runtime. Command tokens resolved here, then dispatched to overlay 2 or command overlays.
- Current linker-visible resident footprint:
  - `CODE` `0x755F`
  - `RODATA` `0x0467`
  - `DATA` `0x0047`
  - `INIT` `0x001C`
  - `ONCE` `0x0038`
  - `BSS` `0x01F7`

## Per-Overlay Details

### Overlay 1: Parser / Lexer

- Purpose: Lexer, parser, AST construction, and parse cleanup.
- Build PRG: `rsparser.prg`
- Disk staging PRG: `obj/rsparser.prg`
- Disk filename: `rsparser`
- Source files: `rs_lexer.c, rs_parse.c, rs_parse_support.c, rs_parse_free.c`
- Commands: None directly; parse phase support.
- Runtime bytes in overlay window: `13005` at `$8E00-$C0CC`
- Window share: `90.7%` used, `1331` bytes free
- Disk footprint: `13007` bytes, `52` D71 blocks
- REU policy: Cached in shared bank `0x40`, slot `$400000-$4037FF`, as a full `0x3800`-byte overlay-window snapshot.
- RAM notes: Lives entirely inside the shared overlay window while active.

### Overlay 2: Execution Core

- Purpose: Values, variables, formatting, pipes, and shared execution helpers.
- Build PRG: `rsvm.prg`
- Disk staging PRG: `obj/rsvm.prg`
- Disk filename: `rsvm`
- Source files: `rs_vars.c, rs_value.c, rs_format.c, rs_cmd.c, rs_pipe.c`
- Commands: PRT, MORE, TOP, SEL, GEN, TAP and the shared execution paths that command overlays return to.
- Runtime bytes in overlay window: `14033` at `$8E00-$C4D0`
- Window share: `97.9%` used, `303` bytes free
- Disk footprint: `14035` bytes, `56` D71 blocks
- REU policy: Cached in shared bank `0x40`, slot `$403800-$406FFF`, as a full `0x3800`-byte overlay-window snapshot.
- RAM notes: Includes rs_vm_fmt_buf[128] and rs_vm_line_buf[384] inside the overlay image.

### Overlay 3: Drive Info + Directory Listing

- Purpose: Shared command overlay for DRVI and LST.
- Build PRG: `rsdrvilst.prg`
- Disk staging PRG: `obj/rsdrvilst.prg`
- Disk filename: `rsdrvilst`
- Source files: `rs_cmd_lst_c64.c, rs_cmd_drvi_c64.c`
- Commands: DRVI, LST
- Runtime bytes in overlay window: `7743` at `$8E00-$AC3E`
- Window share: `54.0%` used, `6593` bytes free
- Disk footprint: `7745` bytes, `31` D71 blocks
- REU policy: Not cached in a dedicated REU overlay bank; loaded from disk on demand.
- RAM notes: Shares the inter-command REU handoff area at 0x480000-0x487FFF.

### Overlay 4: Load Value

- Purpose: Single-command overlay for LDV.
- Build PRG: `rsldv.prg`
- Disk staging PRG: `obj/rsldv.prg`
- Disk filename: `rsldv`
- Source files: `rs_cmd_ldv_c64.c`
- Commands: LDV
- Runtime bytes in overlay window: `11154` at `$8E00-$B991`
- Window share: `77.8%` used, `3182` bytes free
- Disk footprint: `11156` bytes, `44` D71 blocks
- REU policy: Not cached in a dedicated REU overlay bank; loaded from disk on demand.
- RAM notes: Uses the shared handoff region plus the REU-backed value arena in bank 0x48 when hydrating pointer-backed values.

### Overlay 5: Store Value

- Purpose: Single-command overlay for STV.
- Build PRG: `rsstv.prg`
- Disk staging PRG: `obj/rsstv.prg`
- Disk filename: `rsstv`
- Source files: `rs_cmd_stv_c64.c`
- Commands: STV
- Runtime bytes in overlay window: `8970` at `$8E00-$B109`
- Window share: `62.6%` used, `5366` bytes free
- Disk footprint: `8972` bytes, `36` D71 blocks
- REU policy: Not cached in a dedicated REU overlay bank; loaded from disk on demand.
- RAM notes: Uses the shared handoff region plus the REU-backed value arena in bank 0x48 when serializing pointer-backed values.

### Overlay 6: File Delete / Rename / Write

- Purpose: Shared command overlay for DEL, REN, PUT, and ADD.
- Build PRG: `rsfops.prg`
- Disk staging PRG: `obj/rsfops.prg`
- Disk filename: `rsfops`
- Source files: `rs_cmd_delren_c64.c, rs_cmd_putadd_c64.c`
- Commands: DEL, REN, PUT, ADD
- Runtime bytes in overlay window: `13233` at `$8E00-$C1B0`
- Window share: `92.3%` used, `1103` bytes free
- Disk footprint: `13235` bytes, `53` D71 blocks
- REU policy: Not cached in a dedicated REU overlay bank; loaded from disk on demand.
- RAM notes: Keeps file-operation staging and transient command state in overlay-local code plus the shared REU scratch region.

### Overlay 7: File Read

- Purpose: Single-command overlay for CAT.
- Build PRG: `rscat.prg`
- Disk staging PRG: `obj/rscat.prg`
- Disk filename: `rscat`
- Source files: `rs_cmd_cat_c64.c`
- Commands: CAT
- Runtime bytes in overlay window: `6455` at `$8E00-$A736`
- Window share: `45.0%` used, `7881` bytes free
- Disk footprint: `6457` bytes, `26` D71 blocks
- REU policy: Not cached in a dedicated REU overlay bank; loaded from disk on demand.
- RAM notes: Uses overlay-local file I/O logic plus shared REU scratch when line staging is needed.

### Overlay 8: File Copy

- Purpose: Single-command overlay for COPY.
- Build PRG: `rscopy.prg`
- Disk staging PRG: `obj/rscopy.prg`
- Disk filename: `rscopy`
- Source files: `rs_cmd_copy_c64.c`
- Commands: COPY
- Runtime bytes in overlay window: `6599` at `$8E00-$A7C6`
- Window share: `46.0%` used, `7737` bytes free
- Disk footprint: `6601` bytes, `26` D71 blocks
- REU policy: Not cached in a dedicated REU overlay bank; loaded from disk on demand.
- RAM notes: Uses an overlay-local 128-byte transfer buffer plus direct DOS copy or streamed file I/O. It does not use the shared REU scratch or value arena.

## Observations

- Overlay 2 is effectively full: `14033` of `14336` bytes (`97.9%`).
- Overlay 1 is also large at `13005` bytes (`90.7%`).
- The resident heap below the overlay load address is only `370` bytes, so large transient work must lean on overlays and REU-backed storage.
- Overlays 1-2 no longer consume separate REU banks; they share bank `0x40` and leave `36864` bytes free at the tail of that bank.
- Command overlays 3-8 stay smaller on disk and in RAM, but they pay the disk-load cost per command because they are not REU-cached today.
- Overlay 2 carries the shared formatting buffers, so its footprint reflects both command support code and the text-rendering scratch it owns.
