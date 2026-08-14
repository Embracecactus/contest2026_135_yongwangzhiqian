# Current Progress

Last updated: 2026-08-14
Updated by: Codex

## Active objective

The merged baseline is `f717e652df0f` on `dev-ai-contest-2026`.  The
Wi-Fi/Bluetooth worker-lifecycle implementation is committed as `54f4571` on
`fix/bk7258-radio-worker-lifecycle` and published to the matching `fork` branch.
It closes the remaining boundary against the official v3.1.1.9 SDK and Tuya
ownership model, with paired build and T5-Board evidence.

## Implemented

- Upgraded the PM RPMsg protocol to version 4 with generation/sequence
  identity, same-request retries, CP-side cached reply replay and exactly-once
  clock/frequency side effects.  AP local ownership is committed once even if
  the optional local timebase refresh fails.
- Added complete RPMsg health registration rollback and staged RPMsg-test
  semaphore/thread/callback cleanup.
- Completed the SDK OS-adapter queue, recursive-mutex, IRQ-state and bounded
  semaphore contracts.  `rtos_delete_thread()` remains aligned with the
  official v3.1.1.9 SDK contract; the review request to clear its caller-owned
  handle was not applied.
- Made I2C reset, timer stop, WDT, USB-host and scale/rotate ownership state
  follow vendor return values and retain retryable teardown tokens on failure.
  Added the MIC null-argument release-build guard.  Added default-off WDT/timer
  fault-injection diagnostics and corrected the timer channel range/default to
  3 through 5, matching the pinned SDK capability mask `0x38`.
- Aligned the camera and PWM AP validation profiles with the required SMP,
  AP-supervisor and coordinated-PM topology.  The dual-image builder now rejects
  missing supervisor/PM symmetry and insufficient AP PM domains.
- Preserved the official SDK self-delete ABI and made Bluetooth Controller
  state transactional across AP/CP.  CP publishes an authoritative active bit
  only after real SDK success; AP reconciles lost replies or retains UNKNOWN
  ownership on mismatch.  Wi-Fi remains whole-chip lifetime and rejects
  AP-only restart while active.  See [ADR-025](../memory/decisions/ADR-025-bk7258-radio-lifecycle-boundary.md).

## Verification

- Full signed production build passed for
  `t5_board_cp_app_mcuboot + t5_board_ap_camera_validation_mcuboot`, MCUboot
  version `18.6.42`, security counter `96`.  The production images exclude PM,
  WDT and timer fault-injection symbols.
- Final sparse COM3 download wrote only CP `0x11000..0x4e000` and AP
  `0x165000..0x195000`; BKFIL reported both writes passed and
  `All Finished Successfully`.  BL1, BL2, secondary slot, LittleFS,
  `usr_config`, calibration and OTP/eFuse were not written.
- P0/P1 SWD board state showed CP executing NuttX, RPTUN `CONNECTED`, AP
  supervisor `HEALTHY`, CPU2 `SCHEDULER_ONLINE`, both AP CPUs online and camera
  validation `PASSED` with a 7307-byte JPEG.  The production WDT is active at
  its 8000 ms timeout.
- CP/AP PM endpoints both reached sequence 22 with clock references, frequency
  votes and pending transactions all zero.  Earlier fault-injection board runs
  proved that one or three dropped replies caused one CP commit and AP recovery,
  not duplicated references.
- The mailbox suite passed 31/31 checks; PM activity and BL1 policy tests
  passed.  Ten supported CP/AP metadata pairs passed, as did `git diff --check`
  and build-script syntax validation.
- Real-board validation images injected WDT initialization/stop/PM-restore and
  timer-stop failures.  Every failure returned `-EIO`, preserved truthful
  software/hardware ownership, and recovered on retry.  The first timer run
  also found and closed the unsupported channel-1 default.
- The Bluetooth validation image completed ten Controller close/reopen cycles
  on each of two independent COM3 RTS cold boots.  AP and CP both reported
  init 11/11, deinit 10/10 and zero errors; HCI info/stats remained healthy.
  Final production `18.6.45`/counter 99 was restored with test cycles disabled
  and reported init 1/1, deinit 0/0.  Wi-Fi status and the active-Wi-Fi AP
  restart fail-closed boundary were also exercised.

Detailed source-to-board evidence is in the
[runtime-contract verification record](verification/2026-08-14-bk7258-runtime-contracts.md)
and [radio-lifecycle verification record](verification/2026-08-14-bk7258-radio-worker-lifecycle.md).

## Remaining boundary

- The original 66/100 score describes the audited baseline.  The named
  actionable defects are closed in this working tree, but only a new independent
  review can assign a replacement score.
- Vendor radio failure injection, MIC pause/resume pressure and high-rate USB
  attach remain useful.  Bluetooth Controller repetition and the Wi-Fi
  fail-closed boundary are now board-proven; full NuttX Host re-registration
  remains unsupported rather than simulated.
- Broad checkpatch cleanup was not mixed into this correctness phase.
- MCUboot signing is still software-rooted for development.  OTP anti-rollback
  floor advancement and irreversible production secure-boot activation remain
  separately authorized work.

## Next action

Review and merge the published radio-lifecycle pull request.  Follow with MIC
pause/resume and USB attach lifecycle pressure; do not claim mass-production
acceptance from the current board run alone.

## Fixed constraints

- Official NuttX/apps and Beken SDK source remain read-only.
- Preserve P0/P1 SWD/RTT and COM3; never open COM4.
- Do not add one-off verification scripts when an existing real-board path is
  available.
- Do not commit, push, flash or touch OTP/eFuse/lifecycle/debug locks without
  corresponding owner authority.
- GPT-5.6-Luna delegation remains disabled until the owner re-enables it.
