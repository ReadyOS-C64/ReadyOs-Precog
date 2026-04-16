# precog (solo d64 subset c)

- Release Line: `0.2`
- Artifact Build: `0.2C`
- Kind: `solo-d64-c`

## Why This Variant Exists

- Standalone single-D64 planning subset with tasklist, calendar, and REU viewer.

## Artifacts

- Drive 8: `readyos-v0.2c-solo-d64-c.d64`
- Host-Side Boot PRG: `readyos-v0.2c-solo-d64-c-preboot.prg`
- Host-Side Boot PRG: `readyos-v0.2c-solo-d64-c-boot.prg`

## Included Apps

- Drive 8: `tasklist` - task list
- Drive 8: `cal26` - calendar 26
- Drive 8: `reuviewer` - reu viewer

## VICE Setup

- Enable REU with `16MB`.
- The host-side boot PRGs are convenience autostart files. The disk copy of `PREBOOT` is still the normal disk-side bootstrap.
- Configure drive 8 as `1541` with true drive enabled and attach `readyos-v0.2c-solo-d64-c.d64`.

### VICE Command Example

- Autostart target: `readyos-v0.2c-solo-d64-c-preboot.prg`

```sh
x64sc -reu -reusize 16384 -drive8type 1541 -drive8truedrive -devicebackend8 0 +busdevice8 -8 readyos-v0.2c-solo-d64-c.d64 -autostart readyos-v0.2c-solo-d64-c-preboot.prg
```

## Boot

- This profile uses the direct boot chain `PREBOOT -> BOOT`.
- There is no `SETD71` stage for this variant.
- Attach the single disk on drive `8`, then autostart `readyos-v0.2c-solo-d64-c-preboot.prg` or run `LOAD "PREBOOT",8` then `RUN`.

## C64 Ultimate

- Copy the listed disk image files to the target storage.
- Enable the REU and set it to `16MB`.
- The host-side boot PRGs are optional convenience files for emulator launching; the disk-side `PREBOOT` entry is the standard hardware boot path.
- Attach the single disk image on drive `8`, then boot with `LOAD "PREBOOT",8` and `RUN`.
- This variant boots directly from `PREBOOT` into `BOOT` and does not use `SETD71`.

