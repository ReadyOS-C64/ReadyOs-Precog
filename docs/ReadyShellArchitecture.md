# ReadyShell Architecture

This document describes the current ReadyShell implementation in ReadyOS as it exists in the local build, not an aspirational future design.

For command syntax and user-facing behavior, see [../src/apps/readyshellpoc/README.md](../src/apps/readyshellpoc/README.md).
For the generated size and placement inventory, see [./readyshell_overlay_inventory.md](./readyshell_overlay_inventory.md).

## 1. End-To-End Shape

ReadyShell is an overlayed C64 app with four main layers:

1. Resident shell/runtime code
2. Parse overlay
3. Execution-core overlay
4. Command overlays

At runtime, the resident app owns the shell loop, user input, REPL state, and overlay orchestration. Parsing and most execution logic are swapped through the shared overlay window.

```text
user types line
    |
    v
resident shell loop
    |
    +--> load parser overlay if needed
    |       parse source -> AST / bytecode-ish runtime structures
    |
    +--> restore exec overlay (rsvm)
            execute stages
              |
              +--> direct exec-core commands
              |
              `--> external command overlay calls
                        |
                        +--> load command overlay from disk
                        +--> run command
                        `--> restore rsvm from REU cache
```

## 2. Runtime Memory Model

Current release build layout:

| Region | Range | Purpose |
| --- | --- | --- |
| Resident app window | `$1000-$C5FF` | ReadyShell-owned app RAM |
| Overlay load bytes | `$8DFE-$8DFF` | PRG load-address bytes for overlay sidecars |
| Overlay execution window | `$8E00-$C5FF` | Shared live window for whichever overlay is active |
| Resident BSS | `$8878-$8A6E` | Resident writable state below overlays |
| Resident heap | `$8A70-$8DFD` | cc65 heap below overlay load address |
| High runtime area | `$CA00-$CFFF` | ReadyShell runtime state outside app snapshot |

This produces one important constraint:

- resident growth directly reduces heap headroom
- overlay growth does not reduce heap headroom, but must fit the `0x3800` overlay window

## 3. Overlay Set

Current overlays:

| Overlay | File | Role | Current loading model |
| --- | --- | --- | --- |
| 1 | `rsparser.prg` | lexer, parser, parse support | boot-loaded and REU-cached |
| 2 | `rsvm.prg` | values, vars, formatting, pipes, command lookup, shared execution paths | boot-loaded and REU-cached |
| 3 | `rsdrvilst.prg` | `DRVI`, `LST` | disk-loaded on demand |
| 4 | `rsldv.prg` | `LDV` | disk-loaded on demand |
| 5 | `rsstv.prg` | `STV` | disk-loaded on demand |
| 6 | `rsfops.prg` | `DEL`, `REN`, `PUT`, `ADD` | disk-loaded on demand |
| 7 | `rscat.prg` | `CAT`, `COPY` | disk-loaded on demand |

Overlay 1 and overlay 2 are the primary system overlays. They are separate files on disk, but they share one REU cache bank.

Command overlays are not cached today. Each call loads the overlay from disk, runs it, then restores `rsvm` from REU.

## 4. Command Families

ReadyShell commands currently split into two families.

### 4.1 Exec-core commands

These live in `OVERLAY2`:

- `PRT`
- `MORE`
- `TOP`
- `SEL`
- `GEN`
- `TAP`

These do not require a command-overlay load. They execute directly once `rsvm` is resident in the overlay window.

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

These are now driven through the REU-backed external-command registry plus one generic resident execution path.

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

Execution is command-protocol driven, not just command-name driven. The overlay command ABI defines:

- `BEGIN`
- `ITEM`
- `RUN`
- `PROCESS`
- `END`

The current shipped commands use:

- direct `RUN` for most external commands
- `BEGIN` + repeated `ITEM` for `CAT`

The generic external runner in resident code uses command capability bits to choose the active path.

## 6. External Command Registry

The external command registry lives in REU metadata space, not in resident heap/BSS.

Current registry regions:

| Region | Range | Purpose |
| --- | --- | --- |
| Command registry header | `$488010-$488017` | magic, version, counts |
| Command descriptor table | `$488020-$48807F` | fixed-capacity external command descriptors |
| Overlay state table | `$488080-$4880D9` | fixed-capacity state records for external overlays |

Each descriptor identifies:

- command id
- owning external overlay index
- protocol capability flags
- overlay-local handler id

Each overlay state record carries:

- overlay phase
- load-policy flags
- load/cache state
- reserved fields for future cache metadata
- disk filename

This means new external commands no longer require one resident wrapper per command.

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
    v
load overlay from disk into overlay window
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

For example, `LST` twice means:

```text
LST
  load rsdrvilst from disk
  run LST
  restore rsvm from REU

LST again
  load rsdrvilst from disk again
  run LST
  restore rsvm from REU again
```

So the registry stores metadata about command overlays, but it does not make them REU-cached automatically.

A file-command example follows the same pattern:

```text
COPY "8:notes","9:notes"
  load rscat from disk
  run COPY
  restore rsvm from REU
```

## 8. REU Usage

Current ReadyShell REU usage is split by purpose.

### 8.1 Shared core-overlay cache bank

```text
bank 0x40

+----------------------------------------+ $400000
| OVERLAY1 parse slot                    |
| full overlay-window image (0x3800)     |
+----------------------------------------+ $403800
| OVERLAY2 exec slot                     |
| full overlay-window image (0x3800)     |
+----------------------------------------+ $407000
| free tail                              |
| 0x9000 bytes                           |
+----------------------------------------+ $40FFFF
```

Important detail:

- these are full-window snapshots, not just PRG payload bytes
- writable overlay data survives phase switching because the whole window image is cached

### 8.2 Debug and shell-state banks

```text
bank 0x43
  debug/probe ring and verification bytes

bank 0x48
  command scratch                 $480000-$487FFF
  command registry               $488010-$4880C7
  shared metadata                $4880E0-$4880EB
  pause flag                     $4880F0
  REU heap metadata              $488000-$4880FF
  REU value arena                $488100-$48FEFF
```

The `0x48` bank is where ReadyShell keeps cross-layer shell state that would otherwise cost resident RAM.

## 9. Resident vs Overlay Responsibilities

### Resident code owns

- shell UI / REPL loop
- token and bytecode support
- overlay boot/load/restore logic
- generic external command execution path
- screen/platform glue
- REU coordination for overlay caching

### Overlay 1 owns

- lexing
- parsing
- parse support / cleanup

### Overlay 2 owns

- runtime values
- variables
- formatting
- pipes
- command name lookup
- command capability classification
- shared execution helpers

### Command overlays own

- command-specific disk logic
- command-specific serialization / hydration logic
- overlay-local dispatcher for each command group

## 10. Current Architectural Constraints

These are the real limits today.

### 10.1 Resident heap pressure

Resident heap is only `910` bytes below the overlay load address. Resident code growth reduces that directly.

### 10.2 Overlay 2 pressure

`OVERLAY2` is the tightest overlay. It currently has only a few hundred bytes free, so growth in:

- command name lookup
- command capability tables
- shared VM helpers

is the next likely limit.

### 10.3 External overlays are still disk-bound

The registry removes resident per-command glue, but external command overlays still disk-load every call unless a later change adds REU caching for them.

## 11. Why The Registry Matters

Before this refactor, adding a new external command tended to create resident growth in multiple places.

Now the intended shape is:

- add command implementation to a command overlay
- add one descriptor row
- add or reuse one overlay-state row
- dispatch through the generic external runner

That trades per-command resident glue for compact REU metadata and overlay-local dispatch.

## 12. Next Pressure Point

If ReadyShell grows significantly, the next architectural win is not another registry layer. It is shrinking the per-command footprint inside `OVERLAY2`, especially:

- command-name lookup
- external-command capability classification

Those still scale too directly with command count.
