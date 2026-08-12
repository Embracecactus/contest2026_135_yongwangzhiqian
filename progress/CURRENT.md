# Current Progress

Last updated: 2026-08-13 GMT+8
Updated by: Codex

## Active objective

The complete BK7258 coordinated low-voltage PM phase and the subsequent
official-SDK/Tuya wrapper audit are implemented and board-verified on
`feat/bk7258-coordinated-standby`, based on `4f5a906`.

NuttX remains the PM owner. Repository-owned board wrappers now implement the
v3.1.1.9 three-core protocol instead of treating three independent WFI calls as
standby: CP coordinates both AP cores, PWC/mailbox and AON state, and every
unsafe or incomplete transition fails closed to ordinary shallow idle.

## Current hardware and configuration baseline

- The verified target is T5-Board. SWD is P0/P1, firmware download is COM3,
  and UART1/COM4 is physically switched off. COM4 was not opened.
- The verified signed profile is `BootROM -> board-owned BL1 -> Manifest ->
  NuttX MCUboot BL2 -> signed CP/AP`. BL2 holds immediately before CP handoff;
  writing `JLNK` to `0x2809f7f0` releases CP.
- The final board image is version `18.5.28`, security counter `53`, with CP/AP
  `*_rptun_mcuboot` configurations, CP-target SWD group 1 (P0/P1), RTT/no UART
  console, and BL2 boot hold enabled.
- SWD group, target core, console UART/RTT/NONE, baud rate, frame and pin route
  remain independent Kconfig inputs. P20/P21 and alternate UART routes are
  compile-verified only.
- Official NuttX and Beken SDK v3.1.1.9 sources remain unchanged. Permanent
  adaptation is restricted to repository-owned board wrappers and scripts.

## Verified implementation

- CP coordinates the low-voltage request and waits for both AP WFI reports.
  AP cores stop/restore SysTick, reject pending IRQ/DMA or invalid votes, set
  AON WFI state, retain the mailbox wake path and enter/leave `SLEEPDEEP` in
  the official ordering. RTC wake and NuttX tick compensation are active.
- Continuous runtime PM diagnostics show real completed low-voltage cycles;
  aborts remain bounded and observable rather than bypassing safety checks.
- The official SDK/Tuya audit corrected I2C mutex initialization, DVP H264
  failure cleanup, IPI self-test timing, Flash partition CRC/name validation,
  UART rollback, semaphore/event semantics, host/target allocation boundaries,
  RPTUN mailbox mocks and the stale SDK IRQ verifier.
- The audit also confirmed several deliberate nonchanges: AP SRAM/PSRAM is
  non-cacheable, Wi-Fi pbuf references match the SDK, the BT helper is ordinary
  bitfield RMW, the DVFS critical section follows the SDK, USB has no official
  unregister, and RPTUN rollback remains intentionally fail closed.
- Generic, camera/H264, Wi-Fi and final signed PM dual-image builds all pass.
  The partition/factory-layout and SDK provenance gates pass.
- Host RPTUN unit tests pass `31/31`; SDK IRQ verification passes `48/48`;
  `git diff --check` passes.
- COM3 sparse flashing wrote only BL1, CP, AP and the two BL2 copies. BKFIL
  reported all five writes successful; LittleFS, `usr_config` and calibration
  were preserved. Canonical hardware log:
  `../../logs/bk7258-auto-debug/20260813-015254` from this repository.
- J-Link used exactly `STAR`, SWD and 100 kHz without halting the target. Across
  two read-only samples CP completed cycles increased from 1,241 to 1,836;
  AP0 deep entries from 4,543 to 6,618 and AP1 from 1,159 to 1,689. Wake counts
  increased with them, proving continued coordinated runtime entry and restore.

Detailed commands, results and proof boundaries are in
[the coordinated-PM and SDK/Tuya audit record](verification/2026-08-13-bk7258-coordinated-pm-sdk-audit.md).

## Honest boundary

The signed chain still uses development software keys and is not rooted in
OTP/eFuse. No OTP/eFuse, lifecycle, rollback-fuse or debug-lock bit was written.

P20/P21, alternate UART wiring and peripheral-specific physical profiles have
not all been tested on matching boards. A diagnostic `last_abort_reason` may
describe the most recent rejected attempt even while completed sleep/wake
counters continue increasing; this is expected fail-closed behavior, not a
claim that every idle opportunity reaches low voltage.

## Next step

1. Open and review the published branch as a PR against `dev-ai-contest-2026`.
2. Run longer PM/peripheral coexistence soak tests if product qualification
   requires them; keep every standby entry condition fail closed.
3. Validate P20/P21 and alternate UART routes only with matching physical wiring.

## Fixed constraints

- Do not modify NuttX or official SDK sources for the permanent solution.
- Do not use COM4 in the P0/P1 SWD/RTT profile; use COM3 only for download.
- Never halt the running board for evidence collection.
- Never place development private keys in the repository, logs or memory.
- Never write OTP/eFuse or debug locks without separate explicit authority.
- GPT-5.6-Luna delegation is temporarily disabled; the main model owns coding
  and debugging until the user explicitly re-enables it.
