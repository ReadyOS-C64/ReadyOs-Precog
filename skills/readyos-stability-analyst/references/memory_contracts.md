# ReadyOS Memory Contracts

Canonical sources:
- `MEMORY_MAP.md`
- `tools/memory_map_spec.json`

## Critical RAM windows
- App runtime snapshot: `$1000-$C5FF` (`$B600` bytes)
- REU metadata/system table: `$C600-$C7FF`
- Shim resident region: `$C800-$C9FF`
- Hardware I/O: `$D000-$DFFF`

## Shim jump/data anchors
- Jump table starts at `$C800`
- Shim data window: `$C820-$C83F`
- Core bank state bytes: `$C834-$C837`

## ReadyShell overlay contract
- `__HIMEM__ = $C600`
- Overlay size is profile-based:
- release/default (`READYSHELL_PARSE_TRACE_DEBUG=0`): `READYSHELL_OVERLAYSIZE = $3800`, `__OVERLAYSTART__ = $8E00`
- debug trace (`READYSHELL_PARSE_TRACE_DEBUG=1`): `READYSHELL_OVERLAYSIZE = $3B00`, `__OVERLAYSTART__ = $8B00`
- ReadyShell fixed REU bank ownership:
- `0x40` -> `REU_RS_OVL1` (overlay1 cache region `0x400000`)
- `0x41` -> `REU_RS_OVL2` (overlay2 cache region `0x410000`)
- `0x43` -> `REU_RS_DEBUG` (debug/probe region `0x43F000+`)
- These banks must not be handed out by dynamic allocation.
- Overlay REU cache offsets:
- `0x400000` overlay1
- `0x410000` overlay2
- `0x43F000` debug head
- `0x43F010` debug ring data

Profile control commands:
- build release/default: `make -j1 READYSHELL_PARSE_TRACE_DEBUG=0`
- build debug trace: `make -j1 READYSHELL_PARSE_TRACE_DEBUG=1`
- verify release/default contract: `READYSHELL_PARSE_TRACE_DEBUG=0 python3 tools/verify_memory_map.py`
- verify debug contract: `READYSHELL_PARSE_TRACE_DEBUG=1 python3 tools/verify_memory_map.py`

## Hard gates
- `python3 tools/verify_memory_map.py`
- `python3 tools/verify_resume_contract.py`
- `python3 verify.py`

Contract drift in shim/app/REU reserved windows is a blocking failure.
