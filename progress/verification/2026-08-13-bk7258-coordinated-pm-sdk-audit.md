# BK7258 Coordinated PM and SDK/Tuya Audit Verification

Date: 2026-08-13 GMT+8

Branch: `feat/bk7258-coordinated-standby`

Base HEAD: `4f5a906`

State: verified for publication

## Scope and evidence boundary

This checkpoint replaces the earlier shallow-WFI-only claim with a complete
repository-owned port of the Beken v3.1.1.9 three-core coordinated low-voltage
protocol. The official CP PM state machine, BK7258 CP system-PM HAL, BK7258 AP
system-PM HAL and Tuya's `tkl_*`-over-Beken-SDK wrapper architecture were used
as read-only behavioral references. No official SDK or NuttX source was edited.

The key official distinctions retained by the port are:

- CP is the coordinator and waits for both AP cores; three independent WFI
  calls are not a coordinated standby protocol.
- AP shallow idle and AP coordinated low voltage are separate paths.
- Coordinated entry checks votes and pending IRQ/DMA, stops SysTick, publishes
  AON state, restricts wake to the retained mailbox/RTC path and restores state
  in the SDK order.
- Any incomplete handshake or unsafe condition aborts to shallow idle.

## Audit fixes

- Initialized the I2C mutex through the wrapper contract before first use.
- Made delayed DVP H264 replay failures release the SDK handle and PM clock,
  with H264-only fields contained by their configuration guard.
- Reset the IPI self-test elapsed interval for every message.
- Validated the official Flash partition 32-byte name plus big-endian CRC16,
  rejected corruption with `-EILSEQ`, and guaranteed a terminated exported name.
- Made UART post-init failures deinitialize the SDK UART, unmap its pins and
  restore the console route.
- Aligned semaphore/event wrappers with official/Tuya ANY/ALL, clear and timeout
  behavior while keeping every failure bounded.
- Kept target allocation on `kmm_malloc/kmm_free`; libc allocation is now
  available only to the host verifier.
- Updated RPTUN host mocks for PWC/status/generation/PM_WAKE traffic.
- Updated the SDK IRQ verifier to the current common wrapper path and actual
  dispatcher-safe priority policy.

The audit deliberately did not add cache maintenance to non-cacheable AP
SRAM/PSRAM, change SDK-matching Wi-Fi pbuf references, treat the BT bitfield
helper as W1C, shorten the official-style DVFS critical section, invent a USB
unregister API, or weaken fail-closed RPTUN rollback.

## Build and host verification

All of the following completed successfully:

1. Generic driver-check CP/AP unsigned dual image.
2. Camera/H264 signed CP/AP image.
3. Wi-Fi CP/AP unsigned image.
4. Final `cp_nsh_rptun_mcuboot + ap_smp_rptun_mcuboot` signed image, version
   `18.5.28`, security counter `53`.
5. SDK provenance/checksum, partition/factory-layout, CP/AP link, MCUboot
   signing and RPTUN-layout gates.
6. Host RPTUN test suite: `31 passed, 0 failed`.
7. SDK IRQ wrapper verification: `48 passed, 0 failed`.
8. SDK partition-wrapper verifier and `git diff --check`.

The final sparse package contains CRC-decoded-valid BL1, CP, AP, primary BL2
and secondary BL2 segments. Signing used temporary development software keys
outside the repository; this is not OTP/eFuse-rooted secure boot evidence.

## Physical-board verification

Target: T5-Board. Download used COM3 only. UART1/COM4 remained physically off
and was never opened. Sparse flashing wrote only these five executable ranges:

- BL1 at `0x000000`, length `0x11000`
- CP at `0x011000`, length `0x41000`
- AP at `0x165000`, length `0x22000`
- primary BL2 at `0x51d000`, length `0x4000`
- secondary BL2 at `0x53f000`, length `0x4000`

BKFIL reported every WriteFlash operation successful and finished normally.
LittleFS, `usr_config` and the calibration tail were not written. Canonical log:
`/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260813-015254`.

J-Link used the required `STAR`, SWD, 100 kHz profile on P0/P1. It wrote only
the BL2 release magic and then made read-only memory samples; it never halted
the target. The SWD route register read `0x1a` and the release word initially
read zero before `JLNK` was written.

Two live samples proved continuing coordinated cycles:

| Counter | First sample | Second sample |
|---|---:|---:|
| CP completed entries/wakes | 1,241 | 1,836 |
| AP0 deep entries | 4,543 | 6,618 |
| AP0 wakes | 1,384 | 2,039 |
| AP1 deep entries | 1,159 | 1,689 |
| AP1 wakes | 1,154 | 1,680 |

The latest CP diagnostic may contain an AP-timeout abort from a later attempt
while these success counters increase. That is expected: the port records the
most recent fail-closed rejection and does not disguise it as a successful
entry. The two monotonic samples are the hardware evidence that coordinated
entry and ordered restore continue during normal runtime.

## Remaining boundary

- P20/P21 and alternate UART routes are compile-only until matching physical
  wiring is available.
- This run proves the selected RPTUN MCUboot PM profile, not every peripheral
  coexistence combination or long-duration qualification soak.
- Development keys do not establish hardware-root trust. No OTP/eFuse,
  rollback fuse, lifecycle lock or debug lock was changed.
- The verified source checkpoint is published as one reviewed branch commit;
  PR review and merge remain separate Web operations.
