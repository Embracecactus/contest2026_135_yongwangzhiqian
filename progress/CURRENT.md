# Current Progress

Last updated: 2026-08-13 GMT+8
Updated by: Codex

## Active objective

The BK7258 private SDK ABI boundary phase is implemented and verified on top
of merged baseline `53d826d`.  It is published from
`refactor/bk7258-sdk-abi-contract` for owner review and merge.

This phase centralizes the private v3.1.1.9 declarations and layout guards used
by AP mailbox/CAN/RTC and CP Bluetooth/shared-PHY code.  It also closes the
review question around AP IPI routing: the SDK sender transports only
`param2`, while the receive bridge reconstructs `param0` from the physical
source endpoint and `param1` from the AP-local CPU index.  Sender-side writes
to `param0/param1` would therefore not repair or strengthen this protocol.

## Hardware and debug profiles

- Target: T5-Board. P0/P1 remain dedicated to J-Link SWD, COM3 is the download
  port, and UART1/COM4 is physically switched off and was never opened.
- The retained profile is signed `cp_nsh_wifi_rtt_mcuboot +
  ap_smp_wifi_mcuboot`, version `18.6.12`, security counter `66`. RTT0 carries
  NSH and RTT1 carries syslog. `_SEGGER_RTT` is `0x2802b9a0`.
- Boot remains BootROM -> board BL1 -> Manifest -> NuttX MCUboot BL2 -> signed
  CP/AP. BL2 holds immediately before CP at `0x2809f7f0` for P0/P1 attach.

## Implemented

- Added `chip/include/bk7258_sdk_abi.h` as the single board-private boundary
  for immutable SDK symbols, numeric partition details, callback layouts and
  compile-time ABI guards.
- Removed the scattered shadow declarations and duplicated callback structures
  from CAN, RTC and Bluetooth controller wrappers.
- Centralized AP mailbox route validation and documented the v3.1.1.9
  send/receive responsibility split.
- Retained the existing runtime non-cacheable shared-SRAM MPU gate instead of
  adding redundant cache maintenance to an explicitly non-cacheable region.

## Verification

- Classic Make full dual `cp_nsh_drivercheck + ap_smp_drivercheck` passed SDK
  checksum, partition/factory, wrapper and RPTUN-layout checks.
- CMake CP and AP drivercheck builds passed. RPTUN mailbox tests passed
  `31/31`; PM activity and BL1 policy tests passed; `git diff --check` passed.
- The signed version `18.6.12`/counter `66` image was sparsely written over
  COM3. BL1, primary/secondary BL2, CP and AP all passed; LittleFS,
  `usr_config`, calibration, OTP and eFuse were not written. Download evidence:
  `../../logs/bk7258-auto-debug/20260813-150348`.
- P0/P1 J-Link identified STAR, released only the BL2 `JLNK` word, observed
  `VTOR=0x28010800`, and confirmed that both pins retained function `0x22` and
  control `0x00050048`.
- RTT0 `apctl status` reported AP READY, RPTUN CONNECTED, CPU2
  SCHEDULER_ONLINE and supervisor HEALTHY with zero faults/recoveries/errors.
- Two J-Link snapshots showed advancing AP/CP heartbeats, bidirectional RPTUN
  sequence counters, AP SMP requests and CPU1 IPI handler counters. IPI
  duplicate/lost/send-failure/spurious/stale counters remained zero.
- CP Bluetooth initialization state showed IPC, MAC, PHY and Wi-Fi controller
  dependencies ready, one vendor-init call and result zero.
- Temporary development signing keys were permission `0600`, remained under
  `/tmp`, and were deleted after verification.

Detailed proof and the evidence boundary are in the
[SDK ABI contract verification record](verification/2026-08-13-bk7258-sdk-abi-contract.md).

## Fixed constraints

- Do not modify official NuttX/apps or SDK sources for permanent changes.
- Preserve P0/P1 J-Link/RTT and the alternate COM3/UART0 console profile.
- Do not open COM4.
- Prefer direct real-board verification over new one-off validation scripts.
- Never store credentials or private keys; never write OTP/eFuse, lifecycle,
  rollback-fuse or debug-lock state without separate authority.
- GPT-5.6-Luna delegation remains disabled until the owner re-enables it.
