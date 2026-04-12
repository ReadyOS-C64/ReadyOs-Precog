---
name: readyos-stability-analyst
description: Analyze ReadyOS C64/cc65 stability issues by validating memory-map and overlay contracts, scanning PRG/map artifacts, correlating VICE RAM/REU debug captures, and producing human plus agent markdown summaries.
---

# ReadyOS Stability Analyst

Use this skill when the user asks to diagnose ReadyOS crashes/hangs, REU DMA regressions, ReadyShell overlay issues, or memory-layout conflicts across apps/shim/REU regions.

## Trigger Signals
- "crash", "hang", "returns to BASIC", "stuck"
- "REU", "DMA", "overlay", "memory map", "headroom"
- "analyze logs/manifests/snapshots/maps/prgs"
- "stability report" or "pre-merge stability check"

## Hard Rules
- Treat `MEMORY_MAP.md` + `tools/memory_map_spec.json` as canonical contract.
- Hard-fail if critical contract checks drift (`verify_memory_map` / `verify_resume_contract`).
- Use artifact-first workflow; run live capture only if artifacts are missing or contradictory.
- Keep ReadyShell overlay profile explicit in reports:
  - release/default: `READYSHELL_PARSE_TRACE_DEBUG=0` (`READYSHELL_OVERLAYSIZE=0x3800`, `__OVERLAYSTART__=0x8E00`)
  - debug trace: `READYSHELL_PARSE_TRACE_DEBUG=1` (`READYSHELL_OVERLAYSIZE=0x3B00`, `__OVERLAYSTART__=0x8B00`)
- Treat REU banks `0x40`, `0x41`, and `0x43` as ReadyShell-owned fixed banks (parser cache, VM cache, debug/probe), not dynamic pool.
- For CAL26 REL debugging, use `xrelchk` harness discipline.
- Do not use `src/apps/dizzy/dizzy.c` as REL behavior reference.

## Workflow
1. Run analyzer:
- `skills/readyos-stability-analyst/scripts/run_analysis.sh --mode artifact --update-examples`

2. If runtime evidence is stale or incomplete, escalate:
- `skills/readyos-stability-analyst/scripts/run_analysis.sh --mode live`

Profile control (when reproducing profile-sensitive overlay issues):
- release/default build: `make -j1 READYSHELL_PARSE_TRACE_DEBUG=0`
- debug trace build: `make -j1 READYSHELL_PARSE_TRACE_DEBUG=1`
- explicit profile verification:
  - `READYSHELL_PARSE_TRACE_DEBUG=0 python3 tools/verify_memory_map.py`
  - `READYSHELL_PARSE_TRACE_DEBUG=1 python3 tools/verify_memory_map.py`

3. Inspect generated outputs:
- `docs/stability/reports/*_report.json`
- `docs/stability/reports/*_human.md`
- `docs/stability/reports/*_agent.md`

4. If needed, re-render markdown from a report JSON:
- `python3 skills/readyos-stability-analyst/scripts/render_human_summary.py <report.json> --human-out <path> --agent-out <path>`

## Interpretation Policy
- `status=fail`: contract blocker or critical overflow; stop and fix before proceeding.
- `status=warn`: non-blocking but meaningful instability risk or degraded evidence.
- `status=pass`: no contract drift and no significant stability findings in current evidence set.

## References
- Memory contracts: `skills/readyos-stability-analyst/references/memory_contracts.md`
- Debug surfaces: `skills/readyos-stability-analyst/references/debug_surfaces.md`
- Failure motifs: `skills/readyos-stability-analyst/references/failure_patterns.md`
