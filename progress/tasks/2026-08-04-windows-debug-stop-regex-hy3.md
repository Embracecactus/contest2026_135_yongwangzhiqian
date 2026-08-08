# Generic debug stop-regex hardware check (`ask_hy3`)

- Created: 2026-08-04T09:47:43+08:00
- Owner: primary Codex agent
- Executor: `ask_hy3`
- Scope: two capture-only, read-only COM11 sessions; no target reset or write

## Objective

Verify the newly added `--stop-regex` path in
`tools/windows-hardware-debug/scripts/debug_session_wsl.sh` against the current
BK7258 NSH console. Prove both the early-success path and the maximum-duration
fail-closed path before the N15 campaign runner depends on it.

## Required context

Read `AGENTS.md`, `memory/RULES.md`, `memory/OPERATIONS.md`,
`tools/windows-hardware-debug/SKILL.md` and
`tools/windows-hardware-debug/AI_AGENT_SOP.md`.

## Frozen scope

- COM11, 460800 8N1.
- Command in both sessions: `bkota status` (read-only).
- No reset/control action and no `-AllowTargetControl`.
- No retries beyond the two specified sessions.
- Positive output directory:
  `logs/bk7258-n15/20260804-debug-stop-regex-positive-hy3`.
- Negative output directory:
  `logs/bk7258-n15/20260804-debug-stop-regex-negative-hy3`.

Run exactly:

```bash
./tools/windows-hardware-debug/scripts/debug_session_wsl.sh \
  --console-port COM11 --baud 460800 --duration 30 \
  --action capture --command 'bkota status' \
  --stop-regex 'nsh> ' --stop-settle-ms 100 \
  --expected-regex 'BKOTA STATUS ret=0' --expected-regex 'nsh> ' \
  --fail-regex 'HardFault|ASSERT|PANIC' \
  --output-dir logs/bk7258-n15/20260804-debug-stop-regex-positive-hy3

./tools/windows-hardware-debug/scripts/debug_session_wsl.sh \
  --console-port COM11 --baud 460800 --duration 1 \
  --action capture --command 'bkota status' \
  --stop-regex '__N15_STOP_REGEX_MUST_NOT_MATCH__' \
  --expected-regex 'BKOTA STATUS ret=0' \
  --fail-regex 'HardFault|ASSERT|PANIC' \
  --output-dir logs/bk7258-n15/20260804-debug-stop-regex-negative-hy3
```

The second command is expected to exit nonzero. Do not treat that expected
negative result as a reason to retry.

## Forbidden actions

- No Flash/erase/BKFIL/PSRAM/J-Link/OTA mutation or target control.
- No source, docs, memory, task-file, commit, push or PR changes.
- Do not run `BLEDebug.EXE`.

## Acceptance and handoff

Report both exit codes and independently read both `session.json` and
`serial.json` files. Positive must be `passed`, `stop_regex_matched=true`, and
`elapsed_ms` materially below 30000. Negative must be `error`,
`stop_regex_matched=false`, preserve nonempty raw evidence, and state that the
maximum duration was reached before the stop regex matched. Return absolute
paths, byte counts and SHA-256 values. The primary agent will reopen all
evidence before accepting the feature.
