# Debug Surfaces

Primary runtime capture source:
- `logs/vice_auto_*/manifest.json`
- `logs/vice_auto_*/trace.md`
- `logs/vice_auto_*/state/*.json`

## RAM debug surfaces
- Screen buffer: `$0400-$07E7`
- Boot preload markers: `$C007-$C00C`
- Shim data: `$C820-$C83F`
- Shim debug ring: `$C980-$C99F`
- ReadyShell RAM ring: `$C7A0-$C7DF` with head at `$C7F0`
- REU registers: `$DF00-$DF08`
- ReadyShell overlay window base:
- release/default (`READYSHELL_PARSE_TRACE_DEBUG=0`): `$8E00`
- debug trace (`READYSHELL_PARSE_TRACE_DEBUG=1`): `$8B00`

## REU debug surfaces
- ReadyShell REU ring head: `0x43F000`
- ReadyShell REU ring payload: `0x43F010`, length `0x0200`
- Overlay cache previews: `0x400000`, `0x410000`
- Fixed ReadyShell-owned REU banks: `0x40`, `0x41`, `0x43` (parser cache, VM cache, debug/probe)

## Existing probes/tools
- `tools/vice_readyshell_automation.py` (captures manifest/trace/state)
- `tools/readyshell_reu_probe.py` (overlay compare + REU ring dump)
- `tools/vice_tasks/*` (composable task runner primitives)

Use artifact-first analysis; use live capture only when artifact evidence is missing or contradictory.
