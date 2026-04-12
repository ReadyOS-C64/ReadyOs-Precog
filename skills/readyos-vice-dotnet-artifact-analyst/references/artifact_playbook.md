# Artifact Playbook

## Primary Inputs
- `logs/vice_auto_*/manifest.json`
- `logs/vice_auto_*/stages/*/step.json`
- `logs/vice_auto_*/stages/*/error.json` (if present)
- `logs/vice_auto_*/screen_decoded/*.txt`
- `logs/vice_auto_*/dumps/mem_*.bin`
- `logs/vice_auto_*/stages/debug_post_input_regs/monitor_command.txt`

## Read Order
1. Manifest status and failed/degraded step IDs.
2. Step-local error payloads.
3. Post-prompt/post-input decoded screens.
4. Register capture and RAM debug ring evidence.

## ReadyShell Crash Triage Shortlist
- `verify_1_echo` status
- `check_prompt_post_input` status
- `screen_decoded/post_input.txt` contains `READY.` vs `RS>`
- `$C7A0/$C7F0` ring progression markers
- profile-specific overlay window bytes (`$8E00` release / `$8B00` debug) to confirm content vs BASIC text

## ReadyShell Overlay Profile Matrix (must be explicit in analysis)
- release/default:
- `READYSHELL_PARSE_TRACE_DEBUG=0`
- `READYSHELL_OVERLAYSIZE=0x3800`
- `__OVERLAYSTART__=0x8E00`
- debug trace:
- `READYSHELL_PARSE_TRACE_DEBUG=1`
- `READYSHELL_OVERLAYSIZE=0x3B00`
- `__OVERLAYSTART__=0x8B00`

When sampling RAM for overlay-window sanity, use profile-specific base (`0x8E00` or `0x8B00`) rather than assuming a fixed start.
For REU interpretation, treat banks `0x40`, `0x41`, and `0x43` as ReadyShell-owned fixed banks:
- `0x40` overlay1 cache (`0x400000`)
- `0x41` overlay2 cache (`0x410000`)
- `0x43` debug/probe (`0x43F000+`)

## Notes
- Some REU sampled dumps may be text-monitor fallback; use them as secondary evidence unless corroborated.
- For memory-map/contract interpretation, pair this with `$readyos-stability-analyst`.

## 2026-02-23 Learnings: REU Math + Metadata
- Treat sampled REU previews as advisory unless `bank_source` and bank mapping are explicit.
- Current sampled fallback path (`text_monitor_reu_fetch`) reads by absolute REU offset using:
  - `bank = (abs_off >> 16) & 0xFF`
  - `off16 = abs_off & 0xFFFF`
- Current binary-bank sampled path in dotnet runner can mis-map chunks by selecting:
  - `reuBanks[(off >> 16) % reuBanks.Count]`
  - This is not equivalent to absolute bank ID semantics for analysis.
- `dump.reu mode: binary_bank` is currently unsupported in this runner build; use `sampled` or `full`.

## Metadata Needed For Reliable Interpretation
- `requested_abs_offset` (already present as `offset`)
- `resolved_bank_id` used for fetch
- `resolved_off16` used for fetch
- `addressing_mode` (`absolute_reu_offset` vs `bank_list_indexed`)
- `bank_source` (`binary_bank` vs `text_monitor_reu_fetch`)
- `reu_bank_list` snapshot (bank id + name) at capture time

## Analyst Rule
- If `addressing_mode` is missing, classify REU overlay-slot conclusions as `low confidence`.
- Upgrade confidence only when sampled chunk bytes are confirmed against on-disk overlay payloads with explicit offset math.

## 2026-02-23 Learnings: Symbol Entry Sanity (cc65 Overlay)
- For crash-at-callsite cases, always verify that the map symbol entry address points to executable bytes in the matching overlay payload (`obj/rsparser.prg`, `obj/rsvm.prg`, etc), not zero/data.
- Quick check:
  - `symbol_addr` from `obj/readyshell.map`
  - `overlay_load` from first two bytes of the matching named overlay PRG in `obj/`
  - `offset = symbol_addr - overlay_load`
  - inspect bytes at `offset` for plausible prologue (e.g., `JSR pushax`).
- If entry bytes are zero/data while a valid prologue exists later by a fixed delta, suspect cc65 storage placement around that function (for example large static token buffers) and treat as probable wrong-entry root cause.
