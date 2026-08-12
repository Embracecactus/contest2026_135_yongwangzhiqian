# Current Progress

Last updated: 2026-08-12 GMT+8
Updated by: Codex

## Active objective

The first BK7258 PM phase, T5-Board P0/P1 J-Link recovery, and the
debug/UART/bringup configuration foundation are implemented and board-verified
in the uncommitted worktree on `feat/bk7258-pm-idle` at `24428f4`.

This checkpoint intentionally implements only ordinary shallow idle. It does
not claim a complete port of the v3.1.1.9 coordinated low-voltage PM protocol.

## Current boot and hardware baseline

- The verified target is T5-Board. SWD uses P0/P1, firmware download uses
  COM3, and UART1/COM4 is physically switched off. COM4 was not opened.
- The verified direct profile is `BootROM -> board-owned reconstructed BL1 ->
  CP/AP` with `BL1_USE_BL2=0`. It does not modify a vendor bootloader binary.
  The separately retained signed profile inserts Manifest + NuttX MCUboot BL2.
- BL1 validates the direct CP vector, restores the CP `MSPLIM`, establishes
  P0/P1 SWD and waits after its final security/cache/watchdog cleanup. Writing
  `JLNK` to `0x2809f7f0` releases CP; the unbounded hold cannot be reset by
  APB/AON watchdogs.
- NuttX and the official SDK v3.1.1.9 source trees remain unchanged; board
  wrappers own the adaptation.
- SWD group, target core and boot hold are explicit Kconfig inputs. Console
  transport is independently NONE, RTT or UART0/1/2, with baud/frame/route
  settings and paired-image pin-conflict checks. P20/P21 and alternate UART
  routes remain compile-verified, not T5-Board-verified.
- Mandatory SDK/IPC/PM/AP lifecycle setup runs from idempotent
  `board_late_initialize()`. Procfs, MTD nodes and filesystem mounts remain
  application-level bringup.

## Verified at this checkpoint

- CP DVFS/module-clock voting remains active. NuttX PM accepts only
  `PM_NORMAL`, `PM_IDLE`, and `PM_RESTORE`; `PM_STANDBY`/`PM_SLEEP` fail closed.
- Ordinary non-RTT CP idle reaches clear-SLEEPDEEP then DSB -> WFI -> ISB
  through `pm_idle()`; AP uses the same shallow primitive. The RTT diagnostic
  CP profile deliberately stays awake so J-Link remains attachable.
- GPIO default-map suppression prevents the SDK all-pin initializer from
  reclaiming P0/P1. GPIO1's board LED lower-half is skipped when P0/P1 SWD or
  UART1 owns the pin.
- Transport-only AP builds no longer request the radio-only 320 MHz startup
  vote. This removed the observed AP startup error `0x1a`.
- CP periodic SysTick no longer depends on `up_timer_set_lowerhalf()` when
  `CONFIG_TIMER_ARCH` is absent. The direct SysTick ISR calls
  `nxsched_process_timer()`, allowing timed sleeps, board late init and the CP
  RPMsg worker to progress.
- The final `cp_nsh_rptun_rtt + ap_smp_rptun` dual build passed SDK provenance,
  BL1, CP/AP link, partition/factory-layout, wrapper and RPTUN layout checks.
- Sparse flashing through COM3 wrote only BL1, CP and AP; all three writes
  passed. Slot B, LittleFS and calibration tail were preserved. Canonical log:
  `../logs/bk7258-auto-debug/20260812-204429` from the workspace root.
- At the BL1 hold, J-Link observed `PC=0x020002ba`, `VTOR=0x02000000`, SWD
  function `0x22`, P0/P1 control `0x00050048`, and CPU0 route `0`.
- More than 12 seconds after release, J-Link reattached successfully. Runtime
  evidence showed `VTOR=0x28010800`, SysTick enabled (`CTRL=0x00010007`), the
  full startup trace through stage `0x203`, platform/watchdog initialization
  flags `1`, RPTUN CONNECTED with flags `0x7fff`, APBS READY/error 0, CPU2
  RUNNING, and SMP PASSED/error 0/online mask `3`.
- The target was resumed before J-Link exited. `git diff --check` passes. No
  commit or push has been made.

Detailed evidence and proof boundaries are in
[PM idle and J-Link recovery](verification/2026-08-12-bk7258-pm-idle-jlink.md).

## Honest boundary

Phase one is equivalent to the official SDK only at the ordinary shallow-WFI
hardware primitive. It does not implement CP sleep voting, both AP WFI votes,
AON/mailbox-only wake, pending IRQ/DMA/SysTick handling, SLEEPDEEP entry, or
the corresponding restore sequence. Those remain a later low-voltage phase.

The retained signed boot chain is authenticated by development software keys,
not OTP/eFuse hardware root trust. No OTP/eFuse, lifecycle, rollback fuse, or
debug-lock bit has been written.

## Next step

1. Review and commit the bounded PM/debug/UART/bringup changes when authorized.
2. Validate P20/P21 and alternate UART routes only on matching physical wiring.
3. Implement low-voltage standby as a separate phase containing the complete
   CP + two-AP + AON/mailbox + IRQ/DMA/SysTick wake/restore protocol.

## Fixed constraints

- Do not modify NuttX or official SDK sources for the permanent solution.
- Do not use COM4 in the P0/P1 SWD/RTT profile; use COM3 only for download.
- Never place development private keys in the repository, logs, or memory.
- Never write OTP/eFuse or debug locks without separate explicit authority.
