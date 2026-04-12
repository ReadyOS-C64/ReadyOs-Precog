---
name: readyos-vice-dotnet-artifact-analyst
description: Run or reuse ReadyOS .NET VICE task runs, then analyze generated vice_auto artifacts (manifest, stage outputs, screen/state/memory/REU dumps, register captures) and produce crash-path summaries that complement readyos-stability-analyst.
---

# ReadyOS VICE Dotnet Artifact Analyst

Use this skill when the user wants analysis centered on `.NET ViceTasks` automation artifacts (`logs/vice_auto_*`) and wants deterministic crash-path evidence from generated run outputs.

## Trigger Signals
- "use the dotnet VICE tool"
- "analyze latest vice_auto artifacts"
- "compare two vice_auto runs"
- "manifest/degraded steps/register dump/debug ring"
- "post_input drops to BASIC"

## Hard Rules
- Prefer artifact-first analysis from `logs/vice_auto_*`.
- Use `.NET ViceTasks` (`tools/vice_tasks_dotnet`) for automated capture when a fresh run is required.
- Treat `manifest.json` + stage artifacts as primary truth; treat generated summaries as secondary.
- Always label ReadyShell overlay profile in conclusions:
- release/default (`READYSHELL_PARSE_TRACE_DEBUG=0`): `READYSHELL_OVERLAYSIZE=0x3800`, `__OVERLAYSTART__=0x8E00`
- debug trace (`READYSHELL_PARSE_TRACE_DEBUG=1`): `READYSHELL_OVERLAYSIZE=0x3B00`, `__OVERLAYSTART__=0x8B00`
- Treat REU banks `0x40`, `0x41`, and `0x43` as fixed ReadyShell ownership (parser cache, VM cache, debug/probe), not generic allocations.
- When stability-level interpretation is requested, run `$readyos-stability-analyst` against the same run root.

## Workflow
1. If a run root is provided, analyze it directly.
2. Otherwise, run the probe plan with .NET ViceTasks and analyze the latest run.
3. Produce a concise artifact summary:
- overall status, failed/degraded steps
- key stage outcomes (`verify_1_echo`, `check_prompt_post_input`, post-input captures)
- post-input screen state (`RS>` vs `READY.`)
- register capture (`debug_post_input_regs`)
- RAM debug ring/head (`$C7A0/$C7F0`) and profile-aware overlay window samples (`$8E00` release / `$8B00` debug)
4. Optionally compare against a baseline run.
5. If needed, chain to `$readyos-stability-analyst` for contract-level findings.

## Commands
- Run (or reuse latest) and summarize:
`skills/readyos-vice-dotnet-artifact-analyst/scripts/run_dotnet_probe_and_analyze.sh`

- Run with explicit ReadyShell parse-trace profile:
`skills/readyos-vice-dotnet-artifact-analyst/scripts/run_dotnet_probe_and_analyze.sh --parse-trace-debug 0`
`skills/readyos-vice-dotnet-artifact-analyst/scripts/run_dotnet_probe_and_analyze.sh --parse-trace-debug 1`

- Analyze a specific run root:
`python3 skills/readyos-vice-dotnet-artifact-analyst/scripts/summarize_dotnet_vice_artifacts.py --run-root logs/vice_auto_YYYYMMDD_HHMMSS`

- Compare two runs:
`python3 skills/readyos-vice-dotnet-artifact-analyst/scripts/summarize_dotnet_vice_artifacts.py --run-root <new> --compare-root <old>`

## References
- Artifact interpretation checklist: `skills/readyos-vice-dotnet-artifact-analyst/references/artifact_playbook.md`
- C64/cc65 web research toolkit: `skills/readyos-vice-dotnet-artifact-analyst/references/c64_cc65_web_sources.md`
- .NET ViceTasks behavior: `tools/vice_tasks_dotnet/README.md`
- Stability companion skill: `skills/readyos-stability-analyst/SKILL.md`
