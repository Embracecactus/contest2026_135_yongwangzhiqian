# Current Progress

Last updated: 2026-08-13
Updated by: Codex

## Active objective

The baseline is `0db9607` on `dev-ai-contest-2026`.  The current uncommitted
branch is `refactor/bk7258-bringup-layers` and completes the physical-board
configuration and bringup-layer refactor before defconfig/packaging cleanup.

## Implemented

- Split the former monolithic board file into thin NuttX boot/app entry points,
  one mandatory platform-lifetime layer and one application-facing
  procfs/MTD/filesystem bringup layer.  Mandatory SDK/IPC/PM/AP ordering is
  unchanged and remains idempotent.
- Added selected-board early and attached-device hooks for T5-Board and
  T5AI-Core.  T5-Board now owns LCD, GT1151, camera and RGB-backlight
  validation registration; T5AI-Core supplies explicit empty hooks.
- Defined the configuration boundary for all peripherals: physical variants
  own fixed electrical facts and connector conflicts; shared `chip/` code owns
  controller mechanics and NuttX lower halves; runtime I2C/SPI frequency,
  mode and word width remain transaction inputs.
- Made the configured I2C/SPI controller number authoritative end to end:
  initialization, SDK unit selection and `/dev/i2cN` or `/dev/spiN`
  registration now use the same Kconfig value.  The I2C range is restricted
  to the two BK7258 controllers and an unused SPI timeout option was removed.
- Moved T5-Board camera/backlight/touch fixed frequencies or limits into the
  board binding and added symmetric capability declarations for both variants.
- Generalized the board LED/key lower half and diagnostics for declared
  active-high/low polarity, pull direction and default press edge without
  changing either current board's behavior.
- Added both selected-board hook sources to Classic Make and CMake, including
  the Classic dependency path needed for variant-local sources.

## Verification

- CMake builds passed for `cp_nsh_drivercheck` (T5AI-Core),
  `ap_smp_drivercheck` (T5-Board) and `ap_smp` (T5AI-Core).  Logs confirm each
  selected variant hook compiled and the images linked/postbuilt.
- Classic `cp_nsh_drivercheck + ap_smp_drivercheck` completed BL1, CP, AP,
  restored CP, dual-image packaging, factory-layout and RPTUN-layout checks.
- A temporary clean CMake build selected I2C1 and SPI1.  Generated config and
  disassembly proved SDK unit 1 and device minor 1 reached both drivers; the
  maintained drivercheck defconfig was then restored byte-for-byte.  A final
  standard Classic dual build and package completed successfully afterwards.
- A temporary generated configuration enabled both manual GPIO diagnostics;
  both commands compiled and linked, after which standard `cp_nsh_drivercheck`
  was restored and rebuilt successfully without changing its defconfig.
- `board/bk7258/tests/run_tests.sh` passed: RPTUN mailbox 31 checks, BL1 policy
  test and PM activity test.
- A hardware-compatible signed T5-Board candidate was prepared from
  `cp_nsh_drivercheck_rtt_mcuboot + ap_smp_camera_h264_mcuboot`, version
  `18.6.26`, security counter `80`.  CP/AP pairing, BL1/BL2, partition,
  factory-layout and RPTUN-layout gates passed.
- With owner authorization, that exact candidate was sparsely flashed through
  COM3.  BKFIL reported `Writing Flash OK` and `{All Finished Successfully}`;
  the log is `logs/bk7258-auto-debug/20260813-203204`.  P0/P1 SWD identified
  STAR r1p0 at about 3.29 V, RTT0 reached NSH, RTT1 carried live AP/DVP/H.264
  syslog, and COM4 was never opened.
- `apctl status` reported AP `READY`, RPTUN `CONNECTED`, supervisor `HEALTHY`
  and CPU2 `SCHEDULER_ONLINE`, all with error zero.  AP SMP reported
  `PASSED(4)`, `online_mask=0x3`.  The T5-Board camera hook registered and its
  retained H.264 diagnostic passed with 20288 bytes, checksum `0x63fad9a7`,
  DVFS `3 -> 6 -> 3`, and balanced sleep/clock teardown votes.
- `git diff --check` passes.  No official NuttX/apps/SDK source was changed.
- Current board polarities and device ordering remain intentionally unchanged;
  the T5-Board runtime regression gate is closed.

Detailed evidence and scope limits are in the [bringup/board-layer
record](verification/2026-08-13-bk7258-bringup-board-layers.md).

## Next action

Review and publish the current refactor when authorized.  The next separate
phase should model one physical board with several runnable product profiles,
reduce profile duplication and make CP/AP packaging consume explicit
compatible board/profile metadata.  Do not duplicate the board pin database
in those defconfigs or treat the drivercheck pair as a physical release pair:
its CP selects T5AI-Core while its AP selects T5-Board for compile coverage.

## Fixed constraints

- Official NuttX/apps and Beken SDK source remain read-only.
- Preserve P0/P1 SWD/RTT and COM3; never open COM4.
- Do not add one-off verification scripts when an existing real-board path is
  available.
- Do not commit, push, flash, or touch OTP/eFuse/lifecycle/debug locks without
  the corresponding owner authority.
- GPT-5.6-Luna delegation remains disabled until the owner re-enables it.
