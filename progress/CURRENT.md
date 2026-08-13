# Current Progress

Last updated: 2026-08-13 GMT+8
Updated by: Codex

## Active objective

BK7258 per-image CMake parity has been implemented and verified against merged
baseline `1f31506599d0`.  The worktree is intentionally uncommitted on
`feat/bk7258-cmake-parity`; the next operation is owner review followed by an
explicit commit/push request.

Classic Make remains the signed dual-image packaging and board-flash path.
This phase proves CMake source/config/archive/link/postbuild parity for
individual CP/AP images; it does not claim that the CMake artifacts themselves
were flashed or that `build_dual_image.sh` has a CMake backend.

## Hardware and debug profiles

- Target: T5-Board. P0/P1 remain dedicated to J-Link SWD, COM3 is the download
  port, and UART1/COM4 is physically switched off and was never opened.
- The retained profile is signed `cp_nsh_wifi_rtt_mcuboot +
  ap_smp_wifi_mcuboot`, version `18.6.11`, security counter `65`. RTT0 carries
  NSH and RTT1 carries syslog. `_SEGGER_RTT` is `0x2802b9a0`.
- Boot remains BootROM -> board BL1 -> Manifest -> NuttX MCUboot BL2 -> signed
  CP/AP. BL2 holds immediately before CP at `0x2809f7f0` for P0/P1 attach.

## Implemented

- Added one shared CMake SDK-bundle selector for the `legacy` and `v3.1.1.9`
  CP/AP roles; invalid or incomplete bundles fail at configure time.
- Replaced the stale `chip` target and `${BOARD_DIR}` assumptions with the
  NuttX CMake `arch` target and `${NUTTX_BOARD_DIR}`.
- Mirrored Classic Make source gates and ordering for PM, RPTUN/tests,
  peripherals, media, camera, CAN, USB host and physical-board bindings.
- Mirrored SDK static-library closures, forced archive members, symbol wraps,
  MCUboot include policy, entry/build-id options and board postbuild output.
- Added a board-owned CMake 4 compatibility floor for the older bundled
  OpenAMP/libmetal policy declarations. Official NuttX/apps/SDK remain
  unchanged.

## Verification

- CMake 4.0.2 built four clean profiles: CP/AP drivercheck and CP/AP
  drivercheck MCUboot. All emitted ELF, raw image and CRC postbuild artifacts.
- Every ELF has exactly one `_vectors`, `__start` and
  `systick_initialize`; entry points are in the correct CP/AP XIP partitions.
- Classic `cp_nsh_drivercheck + ap_smp_drivercheck` dual build passed SDK
  checksums, partition/factory/RPTUN layout and packaging checks.
- RPTUN mailbox tests passed `31/31`; PM activity and BL1 policy tests passed;
  `git diff --check` passed.
- Signed Classic Make version `18.6.11`/counter `65` was sparsely written over
  COM3. BL1, primary/secondary BL2, CP and AP all passed; LittleFS,
  `usr_config`, calibration, OTP and eFuse were not written. Download evidence:
  `../../logs/bk7258-auto-debug/20260813-141836`.
- P0/P1 J-Link identified STAR, observed `VTOR=0x28010800`, released only the
  BL2 `JLNK` word, and confirmed the live RTT block and preserved pin route.
- RTT0 reported AP READY, RPTUN CONNECTED, both AP CPUs online and all SMP,
  affinity, semaphore-wake/loop and lifecycle checks PASSED.
- `bkrpmsgtest all 20 30000` passed six runs and 240 dual-CPU request/reply
  operations with zero errors and unchanged heap snapshots. Bluetooth info
  passed. Final supervisor state was HEALTHY with fault/recovery/consecutive
  counters `0/0/0`.
- Temporary development signing keys were permission `0600`, remained under
  `/tmp`, and were deleted after verification.

Detailed proof and the CMake-versus-board evidence boundary are in the
[CMake parity verification record](verification/2026-08-13-bk7258-cmake-parity.md).

## Fixed constraints

- Do not modify official NuttX/apps or SDK sources for permanent changes.
- Preserve P0/P1 J-Link/RTT and the alternate COM3/UART0 console profile.
- Do not open COM4.
- Prefer direct real-board verification over new one-off validation scripts.
- Never store credentials or private keys; never write OTP/eFuse, lifecycle,
  rollback-fuse or debug-lock state without separate authority.
- GPT-5.6-Luna delegation remains disabled until the owner re-enables it.
