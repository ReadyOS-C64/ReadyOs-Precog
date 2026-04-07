# Clipboard Bundle SEQ Format

`clipmgr` can save a selected set of clipboard history items into one SEQ file.
The format is binary-safe: entry bytes are stored exactly as clipped, and record
boundaries come only from explicit header metadata.

## Layout

- Bytes `0..4`: ASCII magic `RCLP1`
- Byte `5`: version, currently `1`
- Byte `6`: entry count (`1..16`)
- Byte `7`: flags, currently `0`

Each entry then stores:

- Byte `0`: clipboard type, currently `1` (`CLIP_TYPE_TEXT`)
- Bytes `1..2`: payload length, little-endian `u16`
- Bytes `3..`: raw payload bytes

## Import Semantics

- Save order is the same order shown in `clipmgr` from top to bottom.
- Import replays entries so that the visible clipboard history order matches the
  file order after re-adding them to history.
- Plain SEQ files without the `RCLP1` header still load as one clipboard item.
