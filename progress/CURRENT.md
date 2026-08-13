# Current Progress

Last updated: 2026-08-13 GMT+8
Updated by: Codex

## Active objective

The actionable findings from Codex review session
`019ff8ae-5436-72c3-8a6f-7194fbea2fca` have been implemented against merged
baseline `3b49d8c`, built and boundedly verified on the T5-Board.  The worktree
is intentionally uncommitted on `feat/bk7258-sdk-abi-boundary`; the next
operation is owner review followed by an explicit commit/push request.

Classic Make is the authoritative validated build path.  Full CMake parity and
broader peripheral runtime coverage remain separate follow-ups.

## Hardware and debug profiles

- Target: T5-Board. P0/P1 remain dedicated to J-Link SWD, COM3 is the download
  port, and UART1/COM4 is physically switched off and was never opened.
- The retained default profile is signed `cp_nsh_wifi_rtt_mcuboot +
  ap_smp_wifi_mcuboot`, version `18.6.10`, security counter `64`. RTT0 carries
  NSH and RTT1 is configured for syslog. `_SEGGER_RTT` is `0x2802b9a0`.
- RTT memory was reduced without removing either channel: RTT0 uses
  `1024/128` bytes up/down and RTT1 uses `1024/16` bytes up/down.
- The alternate signed `cp_nsh_wifi_uart0_mcuboot + ap_smp_wifi_mcuboot`
  profile is retained. It provides a UART0/COM3 console at 115200 baud while
  preserving P0/P1 SWD; its verified image is version `18.6.7`, counter `61`.
- Boot remains BootROM -> board BL1 -> Manifest -> NuttX MCUboot BL2 -> signed
  CP/AP. BL1 and BL2 use the pre-CP `JLNK` debug hold at `0x2809f7f0`.

## Implemented

- Replaced the empty SDK delay stub and fake-success OS-adaptation operations
  with bounded SDK/NuttX-compatible behavior.
- Corrected CP standby rejection accounting and removed the redundant
  successful-path CP AON clear.
- Added process-lifetime media roots; fixed JPEG/MIC ownership, AUD/MIC stop
  completion, BLE GATT failure cleanup, I2S channel behavior and watchdog
  retry/error propagation.
- Added GT1151 IRQ preflight and generalized CP/AP RPTUN/Wi-Fi profile pairing
  and layout validation across MCUboot variants.
- Added the new media-root source to both source lists without claiming that
  the pre-existing CMake path is now fully equivalent to Classic Make.

## Verification

- Full `cp_nsh_drivercheck + ap_smp_drivercheck` build passed. SDK provenance,
  factory layout, RPTUN layout, MCUboot signing and `git diff --check` passed.
- RPTUN mailbox host tests passed `31/31`; PM activity and BL1 policy tests
  passed.
- Signed version `18.6.10`, counter `64`, was sparsely flashed through COM3;
  all five executable writes passed while data/calibration partitions were
  preserved. Log: `../../logs/bk7258-auto-debug/20260813-125919`.
- P0/P1 J-Link released the existing BL2 hold without halt/reset. RTT0 at
  `0x2802b9a0` exposed NSH and was operated directly by the agent.
- `apctl status` reported AP READY, RPTUN CONNECTED, both AP CPUs online and
  all built-in SMP/wake/lifecycle checks passed. The supervisor remained
  HEALTHY with zero fault/recovery counters after testing.
- `bkrpmsgtest all 20 30000` passed six cases and 240 total dual-CPU
  request/reply operations with zero errors and unchanged heap snapshots.
- `bkbttest all 1000 15000` passed controller info and scanning, observing
  seven advertisers on the extended 18.6.9 run. The exact final 18.6.10
  image repeated controller info successfully and ended with supervisor
  fault/recovery/consecutive counters at `0/0/0`.
- The RTT system wakelock means this retained-service run is not a new
  low-voltage-entry claim. Media/I2S/watchdog/GT1151 changes are full-build,
  not complete runtime-matrix, evidence.

Detailed proof and boundaries are in [the review closure record](verification/2026-08-13-bk7258-driver-review-closure.md).

## Fixed constraints

- Do not modify official NuttX or SDK sources for permanent changes.
- Preserve both P0/P1 J-Link/RTT and the alternate COM3/UART0 console profile.
- Do not open COM4.
- Prefer direct real-board verification over new one-off validation scripts.
- Never store credentials or development private keys; never write OTP/eFuse,
  lifecycle, rollback-fuse or debug-lock state without separate authority.
- GPT-5.6-Luna delegation remains disabled until the owner re-enables it.
