# N15 generic-debug RTS capture task packet (`ask_hy3`)

- Created: 2026-08-04T09:29:53+08:00
- Owner: primary Codex agent
- Executor: `ask_hy3`
- Scope: one bounded reset-synchronized evidence capture; no OTA generation
  mutation

## Objective

Use the repository `tools/windows-hardware-debug` toolkit to prove that the
known COM7 RTS pulse resets the current BK7258 target and that COM11 captures a
complete return to NSH when the capture window is long enough. The earlier
8-second generic-tool run saw `BClk` but ended before NSH, so it is failed
evidence rather than a PASS.

## Required context

Read:

- `AGENTS.md`
- `memory/RULES.md`
- `memory/OPERATIONS.md`
- `tools/windows-hardware-debug/SKILL.md`
- `tools/windows-hardware-debug/AI_AGENT_SOP.md`

## Frozen execution scope

- Console: COM11, 460800 8N1.
- Reset control: COM7 RTS, asserted, 150 ms.
- Action: exactly one reset-synchronized capture, 25 seconds.
- User authorization: COM7 RTS reset is explicitly authorized.
- Output directory:
  `logs/bk7258-n15/20260804-n15v-generic-debug-rts-reset-25s-hy3`.
- Required positive expressions:
  - `BClk A5=[0-9A-Fa-f]{8} A9=[0-9A-Fa-f]{8}`
  - `NuttShell \(NSH\)`
  - `nsh> `
- Forbidden expression: `HardFault|ASSERT|PANIC`.

Use this exact command from the contest repository root:

```bash
./tools/windows-hardware-debug/scripts/debug_session_wsl.sh \
  --console-port COM11 \
  --reset-port COM7 \
  --baud 460800 \
  --duration 25 \
  --action serial-pulse \
  --pulse-mode RTS \
  --pulse-active-level Asserted \
  --pulse-ms 150 \
  --allow-target-control \
  --expected-regex 'BClk A5=[0-9A-Fa-f]{8} A9=[0-9A-Fa-f]{8}' \
  --expected-regex 'NuttShell \(NSH\)' \
  --expected-regex 'nsh> ' \
  --fail-regex 'HardFault|ASSERT|PANIC' \
  --output-dir logs/bk7258-n15/20260804-n15v-generic-debug-rts-reset-25s-hy3
```

## Forbidden actions

- No Flash, erase, BKFIL, PSRAM load/write, J-Link memory/register write,
  arbitrary J-Link command file, OTA command or generation advance.
- No source, documentation or project-memory edits.
- No additional reset/retry if the one run fails.
- No commit, push or PR action.
- Do not run `BLEDebug.EXE`.

## Acceptance and handoff

Return only an evidence handoff containing:

- command exit status and `session.json` status;
- absolute `serial.raw` and `session.json` paths;
- raw SHA-256 and byte count;
- each expected/fail-regex result;
- whether target-side `BClk`, NSH and prompt prove the reset;
- any ambiguity or failure reason.

The primary agent independently reopens the evidence and decides whether the
generic reset-capture gate passes. This task cannot authorize generation 42.

## Result

- Completed: 2026-08-04T09:34:32+08:00.
- Executor result: exit `0`, `session.json` `passed`.
- Primary review: raw byte count, SHA-256 and every expected/fail expression
  independently matched the handoff.
- Verdict: task complete; generic COM7-RTS reset-capture gate PASS.
- Evidence: [verification record](../verification/2026-08-04-n15-generic-debug-rts-preflight.md).
