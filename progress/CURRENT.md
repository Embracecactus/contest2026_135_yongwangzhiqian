# Current Progress

Last updated: 2026-08-13 GMT+8
Updated by: Codex

## Active objective

The BK7258 peripheral/SDK activity coexistence follow-up on
`feat/bk7258-pm-peripheral-coexistence`, based on merged commit `178adaf`, is
implemented and real-board verified. The next operation is an intentional
commit and push; PR creation remains a separate Web action by the owner.

NuttX remains the PM owner. Repository wrappers aggregate SDK activity, gate
coordinated standby and recover the AP1 release edge without starting the SDK
FreeRTOS PM state machine.

## Hardware and debug profiles

- Target: T5-Board. P0/P1 remain dedicated to J-Link SWD, COM3 is the download
  port, and UART1/COM4 is physically switched off and was never opened.
- The retained default profile is signed `cp_nsh_wifi_rtt_mcuboot +
  ap_smp_wifi_mcuboot`, version `18.6.8`, security counter `62`. RTT0 carries
  NSH and RTT1 is configured for syslog. `_SEGGER_RTT` is `0x2802b9a0`.
- RTT memory was reduced without removing either channel: RTT0 uses
  `1024/128` bytes up/down and RTT1 uses `1024/16` bytes up/down.
- The alternate signed `cp_nsh_wifi_uart0_mcuboot + ap_smp_wifi_mcuboot`
  profile is retained. It provides a UART0/COM3 console at 115200 baud while
  preserving P0/P1 SWD; its verified image is version `18.6.7`, counter `61`.
- Boot remains BootROM -> board BL1 -> Manifest -> NuttX MCUboot BL2 -> signed
  CP/AP. BL1 and BL2 use the pre-CP `JLNK` debug hold at `0x2809f7f0`.

## Implemented

- Added a bounded 64-bit set-state activity aggregator for the 38 SDK module
  IDs and wrapped
  `bk_pm_module_vote_sleep_ctrl()` on CP and AP. CP preserves SDK wake-source
  effects; AP converts the unused FreeRTOS PM call into a NuttX admission gate.
- Published aggregate SDK activity with module-clock and DVFS votes so CP
  rejects standby while a peripheral operation is active.
- Kept PWC RELEASE worker-owned until AP1 withdraws AON WFI. A scoped physical
  replay closes the stale software-pending/no-mailbox-edge case without
  changing ordinary scheduler IPI coalescing.
- Added retained camera evidence and Wi-Fi DHCP/heap packet diagnostics.
- Added the UART0/COM3 console configuration as an alternate profile; the RTT
  profile and J-Link route remain available and are not disabled.

## Verification

- Host RPTUN mailbox tests passed `31/31`; BL1 policy and PM activity tests
  passed. SDK provenance, MCUboot signing, layout checks and `git diff --check`
  passed.
- Camera/H264 returned result `0`, 21,312 bytes and checksum `0x40d20d9d`.
  DVFS returned `120 -> 480 -> 120 MHz`; sleep/clock votes cleared afterwards.
  AP1 deep entries, wakes and heartbeat continued advancing after the release
  replay fix.
- RPMsg passed all six idle/load and 1/64/464-byte cases from both AP cores
  with zero errors and unchanged heap accounting. Bluetooth controller info,
  a three-second scan and post-scan statistics passed.
- The UART0/COM3 profile obtained `192.168.0.100` by DHCP. Its CP diagnostic
  showed minimum free heap `26,256` bytes against the SDK's `10,240`-byte
  reserve, and coordinated PM advanced to 2,001 attempts, 129 entries and 129
  wakeups. Log: `../../logs/bk7258-auto-debug/20260813-113709`.
- The final RTT image was sparsely flashed through COM3; all five executable
  writes passed while LittleFS, `usr_config` and calibration were preserved.
  BL1/BL2 were released through P0/P1 J-Link. Log:
  `../../logs/bk7258-auto-debug/20260813-114819`.
- RTT0 was operated directly by the agent and exposed NSH. A bounded Wi-Fi
  connection completed with `status=0`, `link=3`, address `192.168.0.100` and
  router `192.168.0.1`.
- Final RTT CP diagnostics recorded three DHCP sends, one Discover, two
  Requests, one Offer and one ACK; SDK send result was zero. Minimum free heap
  was `22,104` bytes and minimum margin over the SDK reserve was `11,864`
  bytes, closing the earlier `ERR_TIMEOUT(-3)` failure.
- In the RTT profile the NuttX `system` wakelock kept PM attempts/entries at
  zero. This is expected retained-debug behavior, not a low-voltage-entry
  claim; the UART0 and camera profiles provide the coordinated-PM evidence.
- RTT1 was configured and selected successfully and the target accepted a
  syslog probe. The final RTT1 probe body was not captured because the host
  approval service interrupted that read; it is not claimed as a captured
  channel-content pass.

Detailed proof and boundaries are in [the coordinated-PM verification
record](verification/2026-08-13-bk7258-coordinated-pm-sdk-audit.md).

## Fixed constraints

- Do not modify official NuttX or SDK sources for permanent changes.
- Preserve both P0/P1 J-Link/RTT and the alternate COM3/UART0 console profile.
- Do not open COM4.
- Prefer direct real-board verification over new one-off validation scripts.
- Never store credentials or development private keys; never write OTP/eFuse,
  lifecycle, rollback-fuse or debug-lock state without separate authority.
- GPT-5.6-Luna delegation remains disabled until the owner re-enables it.
