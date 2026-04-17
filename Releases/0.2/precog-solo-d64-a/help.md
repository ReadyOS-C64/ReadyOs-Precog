# precog (solo d64 subset a)

- Release Line: `0.2`
- Artifact Build: `0.2P`
- Kind: `solo-d64-a`

## Why This Variant Exists

- Standalone single-D64 subset focused on editor, reference, and dizzy.

## Artifacts

- Drive 8: `readyos-v0.2p-solo-d64-a.d64`
- Host-Side Boot PRG: `readyos-v0.2p-solo-d64-a-preboot.prg`
- Host-Side Boot PRG: `readyos-v0.2p-solo-d64-a-boot.prg`

## Included Apps

- Drive 8: `editor` - editor
- Drive 8: `hexview` - hex viewer
- Drive 8: `readme` - read.me
- Drive 8: `dizzy` - dizzy kanban

## VICE Setup

- Enable REU with `16MB`.
- The host-side boot PRGs are convenience autostart files. The disk copy of `PREBOOT` is still the normal disk-side bootstrap.
- Configure drive 8 as `1541` with true drive enabled and attach `readyos-v0.2p-solo-d64-a.d64`.

### VICE Command Example

- Autostart target: `readyos-v0.2p-solo-d64-a-preboot.prg`

```sh
x64sc -reu -reusize 16384 -drive8type 1541 -drive8truedrive -devicebackend8 0 +busdevice8 -8 readyos-v0.2p-solo-d64-a.d64 -autostart readyos-v0.2p-solo-d64-a-preboot.prg
```

## Boot

- This profile uses the direct boot chain `PREBOOT -> BOOT`.
- There is no `SETD71` stage for this variant.
- Attach the single disk on drive `8`, then autostart `readyos-v0.2p-solo-d64-a-preboot.prg` or run `LOAD "PREBOOT",8` then `RUN`.

## C64 Ultimate

- Copy the listed disk image files to the target storage.
- Enable the REU and set it to `16MB`.
- The host-side boot PRGs are optional convenience files for emulator launching; the disk-side `PREBOOT` entry is the standard hardware boot path.
- Attach the single disk image on drive `8`, then boot with `LOAD "PREBOOT",8` and `RUN`.
- This variant boots directly from `PREBOOT` into `BOOT` and does not use `SETD71`.

