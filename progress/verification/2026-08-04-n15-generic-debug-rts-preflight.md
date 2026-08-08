# N15 generic debug / COM7 RTS preflight

- Verified: 2026-08-04T09:34:32+08:00
- Executor for the final capture: `ask_hy3`
- Independent reviewer: primary Codex agent
- Tool: `tools/windows-hardware-debug`
- Target action: one authorized COM7 RTS pulse, asserted for 150 ms
- Result: **reset-recovery capture PASS; complete power removal not tested**

## Baseline

The generic toolkit enumerated Windows `COM11`, `COM12`, `COM7` and `COM9`.
A capture-only COM11/460800 session sent `bkota status` and passed all frozen
checks: erased metadata, A mapping, both runtime write gates zero, healthy AP
supervisor, no injection and no armed fault. Its evidence is under
`logs/bk7258-n15/20260804-n15v-generic-debug-status/`:

- `serial.raw` SHA-256:
  `ce84d848d5d8ba3d7d13bb51ec77d86641449356fc59649bc3402f02bc707eb3`
- `session.json` SHA-256:
  `da7c0b01fe987e1ee78f6c1c2f2e5203ac5c100985f7b1f5b981c0ec05ed9eb8`

## Timeout calibration

An initial 8-second generic reset session correctly failed its complete-boot
gate. It captured and matched `BClk`, proving the RTS action reached the
target, but calibration had not completed before the window ended, so NSH and
the prompt were absent. The tool returned `status=error`; this run remains
negative timing evidence and was not relabeled as PASS.

## Delegated 25-second capture

The frozen [`ask_hy3` task packet](../tasks/2026-08-04-n15-rts-debug-capture-hy3.md)
authorized exactly one additional reset and no Flash, PSRAM, OTA, J-Link write,
source edit, commit or push. `ask_hy3` ran the exact generic-tool command once.
The primary agent then reopened the raw capture and JSON and independently
verified:

- command exit code `0`, `session.json` status `passed`;
- COM11 opened before the COM7 action;
- COM7 RTS asserted for 150 ms;
- raw size `426` bytes;
- `serial.raw` SHA-256:
  `ed66ab84bb0fdc297fd79b627faa3af1a51a9e00e1ee2615328378a24a235370`;
- `session.json` SHA-256:
  `2ea7506de110357c9913a03c4c1dab0be0062da0458c37a2ebe7393ac19137df`;
- `BClk A5=8407A76C A9=787BC8A4` matched;
- `NuttShell (NSH)` matched;
- `nsh> ` matched;
- `HardFault|ASSERT|PANIC` did not match.

Canonical evidence:

- `logs/bk7258-n15/20260804-n15v-generic-debug-rts-reset-25s-hy3/serial.raw`
- `logs/bk7258-n15/20260804-n15v-generic-debug-rts-reset-25s-hy3/session.json`

## Claim boundary

This proves the generic toolkit and delegated workflow can capture a complete
COM7-RTS reset recovery while USB and UART remain online. It does not prove
BK7258 VDD removal, a controlled complete power cycle, analog brownout or the
owner-confirmed but still unverified J-Link RST command path. No generation-42
candidate was loaded, staged or published during this preflight.
