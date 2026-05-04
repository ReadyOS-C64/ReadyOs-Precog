# ReadyOS EasyFlash Boot Flow

This document describes how the current ReadyOS EasyFlash cartridge flavor boots, what executes where, when cartridge banking happens, when it does not, how REU preload works, how the early REU-required guard behaves, and why the machine appears to sit on a blue screen for a long time before the launcher appears.

The description is based on the current source and generated layout in this repository, not on a hypothetical design.

On screen, this SKU now identifies itself during boot as `precog cartridge (beta)`.
The release folder, artifact names, and flavor name still remain `precog-easyflash`
for packaging and build purposes.

## Scope

This is the boot path for the EasyFlash flavor that builds:

- `bin/boot_easyflash_roml.bin`
- `bin/boot_easyflash_romh.bin`
- `bin/easyflash_shim.bin`
- `bin/launcher_easyflash.prg`
- `obj/easyflash_layout.json`
- `src/generated/easyflash_layout.inc`

Primary sources:

- `src/boot/easyflash_stub.s`
- `src/boot/boot_easyflash_asm.s`
- `src/boot/readyos_shim.inc`
- `src/apps/launcher/launcher_easyflash.c`
- `src/apps/launcher/launcher.c`
- `src/generated/easyflash_layout.inc`

## High-Level Model

The EasyFlash flavor is not a "run directly from cartridge forever" design.

It is a two-stage system:

1. Small cartridge boot code starts first.
2. That boot code preloads launcher, apps, and overlays into REU snapshots.
3. Normal user-facing execution after boot is mostly REU-driven, not cartridge-driven.

In practice, the cartridge is mainly a fast cold-boot source for the initial payloads. Once the preload pass is complete, the launcher and apps are restored from REU into RAM and run from RAM.

## Current Cartridge Layout

The generated layout currently defines:

- bank `0`: boot bank
- banks `1-2`: launcher payload
- banks `3-34`: application payloads
- banks `35-42`: ReadyShell overlay payloads
- banks `43-63`: unused

Current generated constants:

- `EASYFLASH_LAUNCHER_BANK = 1`
- `EASYFLASH_LAUNCHER_BANK_SPAN = 2`
- `EASYFLASH_APP_COUNT = 16`
- `EASYFLASH_OVERLAY_COUNT = 8`

Each EasyFlash bank is treated as one 16 KiB payload slot, split across:

- `$8000-$9FFF` (ROML half, 8 KiB)
- `$A000-$BFFF` (ROMH half, 8 KiB)

## Memory Regions Used During Boot

### Cartridge-visible areas

- `$8000-$9FFF`: cart ROML half for the selected EasyFlash bank
- `$A000-$BFFF`: cart ROMH half for the selected EasyFlash bank
- `$DE00`: cart bank register
- `$DE01`, `$DE02`: cart control registers

### RAM execution / staging areas

- `$0800-$27FF`: EasyFlash loader copied from cartridge and executed from RAM
- `$1000-$C5FF`: app working window and launcher restore window
- `$1000-$47FF`: upper part of loader tail refresh
- `$1000-$47FF` / full window: temporary staging during preload
- `$C600-$C7FF`: REU bookkeeping and loader state
- `$C7F0-$C7FB`: overlay metadata staging block
- `$C800-$C9FF`: historical shim copied into RAM
- `$CA00+`: diagnostic/debug scratch
- `$CC00-$CC8F`: copied app layout table
- `$CC90-...`: copied overlay layout table

### Screen / VIC visible areas

- `$0400`: expected text screen RAM
- `$D020`: border color
- `$D021`: background color

### REU registers

- `$DF01-$DF08`: REU DMA control registers

## Boot Color Guide

The current EasyFlash loader now uses a stable background plus stage-specific
border colors so the machine is visibly doing work during the long cold-boot
preload.

| Visible color | Register | Meaning | Typical time |
| --- | --- | --- | --- |
| blue background | `$D021` | baseline boot background | entire boot |
| light blue border | `$D020` | loader setup and general control flow | early setup, table copy, verify label |
| green border | `$D020` | shim install and shim-related setup | shim copy and shared-state setup |
| yellow border | `$D020` | copying payload from cartridge into RAM | launcher, app, and overlay payload reads |
| orange border | `$D020` | moving staged RAM into REU or restoring from REU | snapshot stash and final launcher restore |
| light green border | `$D020` | final handoff into the launcher | just before `jmp $1000` |
| red border | `$D020` | REU-missing error path | REU check failed, waiting for keypress |

Important nuance:

- The background intentionally stays blue.
- The border is the progress indicator.
- Long yellow or orange periods are normal and mean the machine is actively
  preloading, not frozen.
- During the app and overlay loops, the border repeatedly alternates between
  yellow and orange because each item is copied from cartridge and then stashed
  to REU.

## Stage 0: Power-On Cartridge Stub

The first code that runs is the ROMH stub in `src/boot/easyflash_stub.s`.

What it does:

1. Disables interrupts.
2. Selects cartridge bank `0` by writing `$DE00 = 0`.
3. Copies `32` pages (`$2000` bytes, 8 KiB) from cartridge `$8000` to RAM `$0800`.
4. Jumps to `$0800`.

Important nuance:

- This stub is tiny.
- It does not preload the system.
- Its only job is to get the real loader into normal RAM and transfer control there.

Execution at this stage:

- executing: cartridge ROM
- copying from: cartridge ROM bank `0`, `$8000-$9FFF`
- copying to: RAM `$0800-$27FF`

## Stage 1: Real Loader Starts From RAM

The main loader lives in `src/boot/boot_easyflash_asm.s` and is linked to run at `$0800` through `cfg/easyflash_loader.cfg`.

This is the real cold-boot engine.

Early setup includes:

- `SEI`, `CLD`, stack init
- CPU port normalization via `$00/$01`
- EasyFlash control register setup
- cart disable for REU-safe operations

Important nuance:

- From here on, the boot logic is executing from RAM at `$0800`, not directly from cartridge ROM.

## Stage 2: Loader Tail Refresh

Very early in the loader, `copy_loader_tail_from_cart` copies more bytes from cartridge bank `0` into RAM.

What it copies:

- source: cartridge bank `0`, starting at `$8800`
- destination: RAM starting at `$1000`
- length: `$1800` bytes

Why this exists:

- The loader itself is larger than the first 8 KiB copied by the ROMH stub.
- The stub only gets the first chunk into RAM.
- The loader then pulls in its own remaining code/data from cart bank `0`.

This is still pure asm.

## Stage 3: Boot Screen Prep

The loader:

- clears screen RAM at `$0400`
- writes a boot banner
- writes stage text
- changes border/background colors

Important nuance:

- At this point the code is writing to `$0400`.
- But the VIC may not yet be looking at `$0400`.
- So those messages can exist in RAM without being visible on the real screen.

This explains why the user can see a long blue pause with no readable text even though status text is already being written.

At this stage the user-visible progress signal is mainly the border-color
changes, not the screen text.

## Stage 3A: Early REU Requirement Probe

Before shim install or any preload work, the loader now does a minimal REU
presence test.

How it works:

1. It writes a known byte to RAM scratch at `$CA11`.
2. It clears RAM scratch at `$CA12`.
3. It performs a one-byte REU stash to REU bank `$FE`.
4. It immediately fetches that byte back into `$CA12`.
5. It compares `$CA12` to the original source byte.

If the compare succeeds:

- boot continues normally into shim install and preload.

If the compare fails:

- the border switches to the error color
- the loader reinitializes the machine into a stable text-screen state
- it shows:
  - `REU NOT DETECTED`
  - `EASYFLASH REQUIRES REU`
  - `PRESS ANY KEY TO RETURN TO BASIC`
- it waits in a KERNAL `GETIN` loop for any key
- after keypress, it jumps to BASIC cold start at `$FCE2`

Important nuance:

- this is no longer a silent failure or a lock-up path
- the cartridge SKU now makes the REU requirement explicit before any expensive
  preload work begins
- the failure path still runs entirely in early boot asm

## Stage 4: Historical Shim Install

The loader copies the historical shim byte image into `$C800-$C9FF`.

Source:

- `shim_data` in `src/boot/boot_easyflash_asm.s`
- produced from `bin/easyflash_shim.bin`
- canonical byte layout defined by `src/boot/readyos_shim.inc`

Destination:

- RAM `$C800-$C9FF`

What the shim is:

- a resident 512-byte helper layer
- common contract between launcher/apps and the boot/runtime handoff model

Key shim jump table entries:

- `$C800`: load from disk and run
- `$C803`: fetch from REU and run
- `$C806`: just run `$1000`
- `$C809`: preload to REU and return
- `$C80C`: return to launcher
- `$C80F`: switch app

Important nuance:

- The shim is not executed directly from cartridge.
- It is always copied into resident RAM first.
- The border switches to the shim color for this phase.

## Stage 5: REU State Init

The loader initializes REU-side bookkeeping in RAM, including:

- zeroing tables around `$C600-$C7FF`
- writing magic at `$C700`
- setting shim default storage drive at `$C839`
- setting `last_saved` at `$C835`

This is RAM bookkeeping, not yet the app preload pass itself.

## Stage 6: Copy Generated Layout Tables To RAM

The generated tables from `src/generated/easyflash_layout.inc` are copied into normal RAM:

- app table -> `$CC00`
- overlay table -> `$CC90`

Why:

- The loader wants a simple, mutable RAM-side table while iterating preload steps.
- It avoids repeatedly reading metadata from cartridge during the control path.

## Stage 7: Preload Launcher Snapshot

The launcher payload is copied from EasyFlash into the app window and then stashed into REU bank `0`.

Current launcher layout:

- cart start bank: `1`
- span: `2` banks
- load address: `$1000`
- payload length: `$78B0`

Detailed flow:

1. Clear `$1000-$C5FF`.
2. Set current cart bank to launcher start bank.
3. Copy launcher payload from cart to RAM at `$1000`.
4. Stash the full app window `$1000-$C5FF` (`$B600` bytes) into REU bank `0`, offset `0`.

Important nuance:

- The payload itself is about `30.2 KiB`.
- The stash is the full `46 KiB` app window.
- That means the REU snapshot stores the complete runtime RAM image shape expected by the launcher, not just the compressed-on-cart payload bytes.
- The border switches to yellow for cart-to-RAM copy, then orange for the REU stash.

## Stage 8: Preload Application Snapshots

The loader then walks every app entry in the app table.

Per app, it does:

1. Clear the full app window `$1000-$C5FF`.
2. Read app metadata from the copied RAM table at `$CC00`.
3. Select the app's EasyFlash start bank by writing `$DE00`.
4. Copy the app payload from cart to RAM at the specified load address, normally `$1000`.
5. Stash the full `$B600` app window to the app's assigned REU bank.
6. Mark that REU bank in the shim bitmap at `$C836-$C838`.

Current app REU banks are `1..16`.

Important nuances:

- This is where most of the long cold-boot time goes.
- The expensive part is not only REU DMA.
- It also repeatedly:
  - clears a large RAM window
  - copies software data from EasyFlash to RAM in 6502 loops
  - stashes a large full-window snapshot to REU

So the long "blue screen pause" is mostly a batch build of all app snapshots.

During this loop, the border repeatedly alternates:

- yellow while the selected EasyFlash bank is being copied into RAM
- orange while the completed RAM window is being stashed to REU

## Stage 9: Preload ReadyShell Overlays

After apps, the loader walks the overlay table.

Per overlay, it does:

1. Clear the overlay staging region beginning at `$1000`.
2. Select the overlay's EasyFlash bank.
3. Copy overlay payload from cart to RAM.
4. Stash `$3800` bytes of staged overlay data to the target REU bank and offset.

Current overlay layout:

- cart banks `35..42`
- target REU banks `64` and `65`
- fixed offsets:
  - `$0000`
  - `$3800`
  - `$7000`
  - `$A800`

Important nuance:

- Overlays are not executed during boot.
- They are staged and cached into REU for later ReadyShell runtime use.
- The loader uses `$1000` as a temporary staging region for this work.
- The same yellow-then-orange border pattern is used here too.

## Stage 10: Overlay Metadata Write

The loader writes a small overlay metadata block to RAM at `$C7F0`, then stashes it to:

- REU bank `$48`
- REU offset `$80F0`

Metadata includes:

- `"OV"` signature
- metadata version
- valid-mask
- overlay cache bank ids
- overlay stage length

This lets the runtime know how the overlay cache has been organized.

## Stage 11: "Verify" Stage

The boot banner includes a `"VERIFYING PRELOADS"` stage label.

Important nuance:

- In the current source, there is no substantial extra checksum/validation pass after that label is shown.
- The label exists, but the current code path goes straight into final machine normalization and launcher restore.

So the long boot time is not coming from a late verification sweep. It is overwhelmingly coming from the earlier preload loops.

## Stage 12: KERNAL and VIC Normalization

Before handing off to the launcher, the loader re-normalizes the machine:

- disables cart for REU-safe state
- restores expected CPU port values
- runs `IOINIT` and `RESTOR`
- forces VIC banking and screen setup back to the normal text baseline
- calls `CINT` and `CLRCHN`
- forces lowercase charset mapping
- restores keyboard/editor state
- clears pending CIA and VIC interrupt state

Why this matters:

- Cartridge startup may leave VIC banking or channels in a nonstandard state.
- If that is not repaired, the launcher can start with invisible text, broken channels, or stray interrupts.

This is also the reason you may briefly see text/color flashes late in boot:

- once VIC is pointed back at the expected screen,
- some previously written screen content becomes visible,
- then KERNAL text-mode init clears/changes it very quickly,
- and then the launcher appears.

## Stage 13: Restore Launcher Snapshot and Handoff

At the end of boot:

1. The loader fetches REU bank `0` back into `$1000-$C5FF`.
2. It disables unwanted interrupt sources.
3. It jumps to `$1000`.

That jump is the handoff from boot asm to the launcher program image.

Important nuance:

- The launcher is not starting directly from cartridge data.
- It is starting from a RAM image restored out of REU bank `0`.
- The border switches to orange for the final restore, then light green for the last handoff into launcher code.

## What Runs Where

### Pure asm, cartridge-first stage

- ROMH reset stub at `$E000`: `src/boot/easyflash_stub.s`

### Pure asm, RAM-resident boot stage

- main loader at `$0800`: `src/boot/boot_easyflash_asm.s`

### Pure asm, resident shim

- shim image at `$C800-$C9FF`: `src/boot/readyos_shim.inc`

### C / cc65 launcher stage

- launcher program in `$1000-$C5FF`
- source entry:
  - `src/apps/launcher/launcher_easyflash.c`
  - `src/apps/launcher/launcher.c`

### Later app launches

After boot, app launches are usually:

1. launcher saves itself back to REU bank `0`
2. shim fetches target app snapshot from REU
3. control jumps to `$1000`

So the steady-state runtime model is REU snapshot switching, not cartridge bank execution.

## Where Cartridge Banking Happens

Cartridge banking is concentrated in the boot asm preload routines.

Main register:

- `$DE00` = selected EasyFlash bank

Main routine:

- `copy_payload_from_cart`

What it does:

1. enables cartridge mapping
2. selects current cart bank
3. copies from `$8000-$9FFF`
4. if more remains, copies from `$A000-$BFFF`
5. increments cart bank
6. repeats until payload length is satisfied

This is the core "read from cartridge" loop used by launcher/app/overlay preload.

## Where Cartridge Banking Does Not Matter Anymore

Once the launcher has been restored and entered:

- normal launcher execution is from RAM
- fast app launches are from REU back into RAM
- shim helper calls are from resident RAM at `$C800`

So after cold boot, the cartridge is mostly no longer part of the hot path.

## Why the Blue Screen Pause Is So Long

The long pause at 100 percent speed is expected from the current design.

What boot is doing during that pause:

- preloading launcher snapshot
- preloading 16 application snapshots
- preloading 8 overlay snapshots
- clearing large RAM windows before each preload
- copying payloads from cartridge into RAM in software loops
- stashing large windows into REU

Roughly:

- about `531 KiB` of actual payload content gets copied from cartridge
- about `885 KiB` of snapshot/stage data gets stashed into REU
- plus a large amount of repeated RAM clearing

The visible effect is "nothing happening", but the machine is actually building all of the runtime REU state up front.

With the new stage colors, the more accurate user-facing interpretation is:

- blue background plus yellow border: cart payloads are being read
- blue background plus orange border: snapshots are being pushed into REU
- brief green border: shim-related setup work
- brief light green border: launcher handoff is about to happen

## Why You Usually Do Not See The Boot Messages

Two separate reasons:

1. The loader writes text to `$0400` before VIC screen banking is guaranteed to be normal.
2. Near the end of boot, KERNAL init and screen/editor setup rapidly clear or overwrite what was there.

So the status system mostly works as an internal/logical status layer, but not as a reliable user-visible progress display in the current order of operations.

## Summary

The EasyFlash boot process is a preload-and-snapshot pipeline:

1. cartridge stub copies loader to RAM
2. RAM loader installs shim
3. RAM loader preloads launcher/apps/overlays from EasyFlash
4. RAM loader stashes them into REU snapshots
5. machine state is normalized
6. launcher snapshot is restored from REU bank `0`
7. launcher starts in C at `$1000`

The large cold-boot delay is mostly the price of making later launches effectively instant.
