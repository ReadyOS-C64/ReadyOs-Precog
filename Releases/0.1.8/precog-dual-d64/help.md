# precog (dual d64)

- Release Line: `0.1.8`
- Artifact Build: `0.1.8D`
- Kind: `dual-d64`

## Why This Variant Exists

- Reduced-content profile for two 1541 drives. It keeps the core productivity apps that fit on dual D64 media.

## Artifacts

- Drive 8: `readyos-v0.1.8d-dual-d64_1.d64`
- Drive 9: `readyos-v0.1.8d-dual-d64_2.d64`
- Host-Side Boot PRG: `readyos-v0.1.8d-dual-d64-preboot.prg`
- Host-Side Boot PRG: `readyos-v0.1.8d-dual-d64-boot.prg`

## Included Apps

- Drive 9: `editor` - editor
- Drive 8: `quicknotes` - quicknotes
- Drive 9: `calcplus` - calc plus
- Drive 8: `clipmgr` - clipboard
- Drive 9: `tasklist` - task list
- Drive 9: `simplefiles` - simple files
- Drive 9: `game2048` - 2048 game
- Drive 9: `deminer` - deminer
- Drive 8: `cal26` - calendar 26

## VICE Setup

- Enable REU with `16MB`.
- The host-side boot PRGs are convenience autostart files. The disk copy of `PREBOOT` is still the normal disk-side bootstrap.
- Configure drive 8 as `1541` with true drive enabled and attach `readyos-v0.1.8d-dual-d64_1.d64`.
- Configure drive 9 as `1541` with true drive enabled and attach `readyos-v0.1.8d-dual-d64_2.d64`.

### VICE Command Example

- Autostart target: `readyos-v0.1.8d-dual-d64-preboot.prg`

```sh
x64sc -reu -reusize 16384 -drive8type 1541 -drive8truedrive -devicebackend8 0 +busdevice8 -8 readyos-v0.1.8d-dual-d64_1.d64 -drive9type 1541 -drive9truedrive -devicebackend9 0 +busdevice9 -9 readyos-v0.1.8d-dual-d64_2.d64 -autostart readyos-v0.1.8d-dual-d64-preboot.prg
```

## Boot

- This profile uses the direct boot chain `PREBOOT -> BOOT`.
- There is no `SETD71` stage for this variant.
- Attach all listed disks before boot, then autostart `readyos-v0.1.8d-dual-d64-preboot.prg` or run `LOAD "PREBOOT",8` then `RUN`.

## C64 Ultimate

- Copy the listed disk image files to the target storage.
- Enable the REU and set it to `16MB`.
- The host-side boot PRGs are optional convenience files for emulator launching; the disk-side `PREBOOT` entry is the standard hardware boot path.
- Attach all listed disk images to their matching drives before boot, then run `LOAD "PREBOOT",8` and `RUN`.
- This variant boots directly from `PREBOOT` into `BOOT` and does not use `SETD71`.

