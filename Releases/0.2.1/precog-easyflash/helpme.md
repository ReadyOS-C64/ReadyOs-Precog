# readyos easyflash

This SKU is the EasyFlash cartridge build of ReadyOS `0.2.1`.

Important naming nuance:

- the files and release folder still use `easyflash`
- the on-screen loader label now reads `precog cartridge (beta)`

## what you need

- `readyos_easyflash.crt`
- `readyos_data.d64`
- REU enabled
- recommended REU size: `16MB`

## what the disk is for

The cartridge is only the cold-boot source.

- it contains the boot loader
- it contains the launcher payload
- it contains app payloads and ReadyShell overlays used for preload
- `readyos_data.d64` on drive `8` remains the normal runtime disk for docs,
  app files, saves, clipboard examples, and other disk-backed content

This SKU expects both the cartridge and the companion disk.

## mount order

Recommended order:

1. mount `readyos_data.d64` on drive `8`
2. attach `readyos_easyflash.crt` as an EasyFlash cartridge
3. make sure REU is enabled
4. reset the machine

## vice example

```sh
x64sc -reu -reusize 16384 -cartcrt readyos_easyflash.crt -drive8type 1541 -devicebackend8 0 +busdevice8 -8 readyos_data.d64
```

## boot behavior

Compared to the disk SKUs, this one has a longer cold boot.

- it preloads the launcher into REU
- it preloads each app snapshot into REU
- it preloads ReadyShell overlays into REU
- after that, normal launcher and app switching are largely REU-driven

That means a long initial preload is expected on a stock-speed C64 path.

## border colors during boot

- blue background: normal boot backdrop for the whole sequence
- light blue border: loader setup and general control flow
- green border: shim install and shared-state setup
- yellow border: copying payload from cartridge into RAM
- orange border: stashing RAM into REU or restoring from REU
- light green border: final handoff into the launcher
- red border: REU missing, waiting for keypress to return to BASIC

Long yellow and orange periods are normal. They mean the cartridge loader is
actively preloading snapshots and overlays.

## REU requirement

This cartridge SKU requires REU.

If REU is not present, the boot loader now stops early, shows:

- `REU NOT DETECTED`
- `EASYFLASH REQUIRES REU`
- `PRESS ANY KEY TO RETURN TO BASIC`

After a keypress, it returns to BASIC cold start.
