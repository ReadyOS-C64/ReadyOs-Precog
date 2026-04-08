# precog (d81)

- Version: `0.1.8T`
- Kind: `d81`

## Artifacts

- Drive 8: `readyos-v0.1.8t-d81.d81`
- Host PRG: `readyos-v0.1.8t-d81-preboot.prg`
- Host PRG: `readyos-v0.1.8t-d81-boot.prg`

## Included Apps

- Drive 8: `editor` - editor
- Drive 8: `quicknotes` - quicknotes
- Drive 8: `calcplus` - calc plus
- Drive 8: `hexview` - hex viewer
- Drive 8: `clipmgr` - clipboard
- Drive 8: `reuviewer` - reu viewer
- Drive 8: `tasklist` - task list
- Drive 8: `simplefiles` - simple files
- Drive 8: `simplecells` - simple cells (alpha)
- Drive 8: `game2048` - 2048 game
- Drive 8: `deminer` - deminer
- Drive 8: `cal26` - calendar 26
- Drive 8: `dizzy` - dizzy kanban
- Drive 8: `readme` - read.me
- Drive 8: `readyshell` - ready shell (demo)

## VICE Setup

- Enable REU with `16MB`.
- Configure drive 8 as `1581` with true drive enabled and attach `readyos-v0.1.8t-d81.d81`.

### VICE Command Example

- Autostart target: `readyos-v0.1.8t-d81-preboot.prg`

```sh
x64sc -reu -reusize 16384 -drive8type 1581 -drive8truedrive -devicebackend8 0 +busdevice8 -8 readyos-v0.1.8t-d81.d81 -autostart readyos-v0.1.8t-d81-preboot.prg
```

## Boot

- This profile uses the direct boot chain `PREBOOT -> BOOT`.
- There is no `SETD71` stage for this variant.
- Attach the single disk on drive `8`, then autostart `readyos-v0.1.8t-d81-preboot.prg` or run `LOAD "PREBOOT",8` then `RUN`.

## C64 Ultimate

- Copy the listed disk image files to the target storage.
- Enable the REU and set it to `16MB`.
- Attach the single disk image on drive `8`, then boot with `LOAD "PREBOOT",8` and `RUN`.
- This variant boots directly from `PREBOOT` into `BOOT` and does not use `SETD71`.

