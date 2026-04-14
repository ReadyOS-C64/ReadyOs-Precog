# ReadyShell Architecture

This document describes the current ReadyShell implementation in ReadyOS as it
exists in the local build, not an aspirational future design.

For command syntax and user-facing behavior, see
[../src/apps/readyshellpoc/README.md](../src/apps/readyshellpoc/README.md).
For the generated size and placement inventory, see
[./readyshell_overlay_inventory.md](./readyshell_overlay_inventory.md).

## 1. End-To-End Shape

ReadyShell is an overlayed C64 app with four main layers:

1. Resident shell/runtime code
2. Parse overlay
3. Execution-core overlay
4. External command overlays

At runtime, the resident app owns the shell loop, prompt state, REPL state,
resume hooks, and overlay orchestration. Parsing and most execution logic still
run through the shared overlay window, but the current implementation now
preloads all eight overlays into fixed REU cache slots during shell startup.

```text
user types line
    |
    v
resident shell loop
    |
    +--> restore parser overlay from REU if needed
    |       parse source -> AST / runtime structures
    |
    +--> restore exec overlay (rsvm) from REU
            execute stages
              |
              +--> direct exec-core commands
              |
              `--> external command overlay calls
                        |
                        +--> restore command overlay from REU
                        +--> run command
                        `--> restore rsvm from REU
```

Normal command execution no longer reloads overlays `3-8` from disk on each
call. Disk loading is now the boot preload path and the safety fallback if a
cache slot or metadata record is invalid.

## 2. Runtime Memory Model

Current release build layout:

| Region | Range | Purpose |
| --- | --- | --- |
| Resident app window | `$1000-$C5FF` | ReadyShell-owned app RAM |
| Overlay load bytes | `$8DFE-$8DFF` | PRG load-address bytes for overlay sidecars |
| Overlay execution window | `$8E00-$C5FF` | Shared live window for whichever overlay is active |
| Resident BSS | `$8769-$895F` | Resident writable state below overlays |
| Resident heap | `$8960-$8DFD` | cc65 heap below overlay load address |
| High runtime area | `$CA00-$CFFF` | ReadyShell runtime state outside app snapshot |

Important constraints:

- resident growth directly reduces heap headroom
- overlay growth does not reduce heap headroom, but every overlay must still
  fit the `0x3800` overlay window
- full-window REU snapshots are required because overlay-local writable data
  must survive phase switches

## 3. Overlay Set

Current overlays:

| Overlay | File | Role | Current loading model |
| --- | --- | --- | --- |
| 1 | `rsparser.prg` | lexer, parser, parse support | boot-preloaded, cached in bank `0x40` |
| 2 | `rsvm.prg` | values, vars, formatting, pipes, command lookup, shared execution paths | boot-preloaded, cached in bank `0x40` |
| 3 | `rsdrvilst.prg` | `DRVI`, `LST` | boot-preloaded, cached in bank `0x40` |
| 4 | `rsldv.prg` | `LDV` | boot-preloaded, cached in bank `0x41` |
| 5 | `rsstv.prg` | `STV` | boot-preloaded, cached in bank `0x40` |
| 6 | `rsfops.prg` | `DEL`, `REN`, `PUT`, `ADD` | boot-preloaded, cached in bank `0x41` |
| 7 | `rscat.prg` | `CAT` | boot-preloaded, cached in bank `0x41` |
| 8 | `rscopy.prg` | `COPY` | boot-preloaded, cached in bank `0x41` |

Fixed REU slot layout:

```text
bank 0x40
  overlay 1  rsparser   $400000-$4037FF
  overlay 2  rsvm       $403800-$406FFF
  overlay 3  rsdrvilst  $407000-$40A7FF
  overlay 5  rsstv      $40A800-$40DFFF
  free tail             $40E000-$40FFFF

bank 0x41
  overlay 4  rsldv      $410000-$4137FF
  overlay 6  rsfops     $413800-$416FFF
  overlay 7  rscat      $417000-$41A7FF
  overlay 8  rscopy     $41A800-$41DFFF
  free tail             $41E000-$41FFFF
```

Each slot stores the full `0x3800` overlay window, not just the PRG payload
bytes.

## 4. Command Families

ReadyShell commands split into two families.

### 4.1 Exec-core commands

These live in `OVERLAY2`:

- `PRT`
- `MORE`
- `TOP`
- `SEL`
- `GEN`
- `TAP`

These execute directly once `rsvm` is active in the overlay window.

### 4.2 External commands

These live outside `OVERLAY2`:

- `DRVI`
- `LST`
- `LDV`
- `STV`
- `DEL`
- `REN`
- `PUT`
- `ADD`
- `CAT`
- `COPY`

These are driven through the REU-backed external-command registry plus one
generic resident execution path.

## 5. Parse And Pipeline Flow

Parsing happens in overlay 1, but pipeline execution returns to overlay 2.

```text
source line
   |
   v
OVERLAY1: lexer + parser
   |
   v
resident regains control
   |
   v
OVERLAY2: execute pipeline stage-by-stage
   |
   +--> expr stage
   +--> filter stage
   +--> foreach stage
   +--> exec-core command
   `--> external command
```

Execution is command-protocol driven, not just command-name driven. The overlay
command ABI defines:

- `BEGIN`
- `ITEM`
- `RUN`
- `PROCESS`
- `END`

The current shipped commands use:

- direct `RUN` for most external commands
- `BEGIN` + repeated `ITEM` for `CAT`

The generic external runner in resident code uses command capability bits to
choose the active path.

## 6. External Command Registry

The external command registry lives in REU metadata space, not in resident
heap/BSS.

Current registry regions:

| Region | Range | Purpose |
| --- | --- | --- |
| Command registry header | `$488010-$488017` | magic, version, counts |
| Command descriptor table | `$488020-$48807F` | fixed-capacity external command descriptors |
| Overlay state table | `$488080-$4880EB` | fixed-capacity state records for external overlays |

Each descriptor identifies:

- command id
- owning external overlay index
- protocol capability flags
- overlay-local handler id

Each overlay state record carries:

- overlay phase
- load-policy flags
- load/cache state
- cache bank and slot offset
- disk filename

This means new external commands no longer require one resident wrapper per
command.

## 7. External Command Call Flow

Current external call path:

```text
OVERLAY2 decides command is external
    |
    v
resident vm_cmd_external()
    |
    v
lookup descriptor in REU registry
    |
    v
prepare external overlay from overlay-state record
    |
    +--> normal case: restore full slot from REU
    `--> fallback: load from disk and refresh cache slot
    |
    v
call one overlay dispatcher with handler id
    |
    v
command overlay runs command body
    |
    v
resident restores OVERLAY2 from REU
```

For example, repeated `LST` calls now reuse the same cached overlay image:

```text
LST
  restore rsdrvilst from REU
  run LST
  restore rsvm from REU

LST again
  restore rsdrvilst from REU
  run LST
  restore rsvm from REU
```

So the registry now describes both dispatch metadata and the fixed cache-slot
placement for overlays `3-8`.

## 8. REU Usage

Current ReadyShell REU usage is split by purpose.

### 8.1 Overlay cache banks

```text
bank 0x40
  overlays 1, 2, 3, 5
  free tail: 8192 bytes

bank 0x41
  overlays 4, 6, 7, 8
  free tail: 8192 bytes
```

Important detail:

- these are full-window snapshots, not just PRG payload bytes
- writable overlay data survives phase switching because the whole window image
  is cached

### 8.2 Debug and shell-state banks

```text
bank 0x43
  debug/probe ring and verification bytes

bank 0x48
  command scratch     $480000-$487FFF
  command registry    $488010-$4880EB
  shared metadata     $4880F0-$4880FB
  pause flag          $4880FC
  REU heap metadata   $488000-$4880FF
  REU value arena     $488100-$48FEFF
```

`bank 0x48` is shared but not overlaid: scratch, registry, pause state, and
the REU-backed value arena all live there at fixed addresses.

## 9. Responsibilities

- Resident code owns the shell loop, overlay boot/load/restore logic, generic
  external command dispatch, screen/platform glue, and REU coordination.
- Overlay 1 owns lexing, parsing, parse support, and cleanup.
- Overlay 2 owns runtime values, variables, formatting, pipes, command lookup,
  capability classification, and shared execution helpers.
- External command overlays own command-specific disk logic, serialization
  logic, and overlay-local dispatchers.

## 10. Current Constraints

- Resident heap below the overlay load address is `1182` bytes.
- `OVERLAY2` is still the tightest overlay and remains the next likely growth
  bottleneck.
- The fixed cache layout leaves `8192` bytes free at the tail of bank `0x40`
  and another `8192` bytes free at the tail of bank `0x41`.
- External commands now pay a one-time startup preload cost instead of repeated
  disk loads during each command call.
