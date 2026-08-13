# Current Progress

Last updated: 2026-08-13
Updated by: Codex

## Active objective

The merged baseline is `9de9912` on `dev-ai-contest-2026`.  Implementation
commit `aba1eb0` on `refactor/bk7258-config-profiles` consolidates the BK7258
defconfigs into explicit physical-board build profiles and is published to
`fork/refactor/bk7258-config-profiles` for review.

## Implemented

- Reduced `board/bk7258/configs` from 42 historical defconfigs to 18 maintained
  profiles: eight T5AI-Core, nine T5-Board and one standalone MCUboot BL2.
- Added schema-1 `profile.conf` metadata for board, role, boot mode, class and
  CP/AP compatibility.  The catalog and supported pairs are documented in
  `board/bk7258/configs/README.md`.
- Reworked `build_dual_image.sh` to validate metadata, role Kconfig, board
  selection, symmetric RPTUN/BT/Wi-Fi features and compatibility before build.
  Packaging and signing behavior no longer depend on profile-name substrings.
- Added a lock around physical dual-image builds.  This prevents concurrent
  sessions from replacing shared NuttX `Make.defs`, generated apps Kconfig and
  `bk7258-dual` artifacts.  Read-only profile checks remain lock-free.
- Added an explicit CI-profile gate and retained external-key requirements for
  MCUboot profiles.  Stale NuttX configs whose old profile directory was
  removed are repaired narrowly before normal configuration.
- Updated active build/debug documentation and source verifiers to canonical
  profile paths.  Historical evidence retains old names but is not a current
  build instruction.
- Fixed the non-Wi-Fi CP BT-IPC link boundary: when `libwifi.a` is present as
  the official PHY closure it owns `rwnxl_set_wifi_low_vol_flag`; the board
  stub remains only for images that do not provide that SDK symbol.
- Updated the PSRAM contract verifier for the new platform/bringup layering and
  metadata-driven profile selection.

## Verification

- All 10 maintained CP/AP pairings pass `BK7258_PROFILE_CHECK_ONLY=YES`.
  Cross-board, incompatible-service and unauthorized CI pairings are rejected.
- Full direct T5AI-Core base CP/AP build passed SDK, BL1, package, partition,
  factory-layout and SDK-wrapper gates.
- T5AI-Core PSRAM CP/AP compiled, linked and packaged; BLE, RPTUN, factory and
  SDK-wrapper gates passed.  The corrected PSRAM source/ELF verifier passed
  directly.  A pre-lock rerun exposed and diagnosed concurrent shared-tree
  corruption rather than a firmware failure.
- With the new lock, full raw T5-Board
  `t5_board_cp_drivercheck + t5_board_ap_drivercheck` passed CP/AP build,
  authoritative CP restore, packaging, factory-layout, SDK-wrapper and RPTUN
  ELF checks.
- `board/bk7258/tests/run_tests.sh`, modified shell/Python syntax checks,
  the 18-defconfig/18-metadata inventory and `git diff --check` pass.
- No target flash, reset, UART, RTT or J-Link action was performed in this
  phase.  MCUboot product profiles were metadata-checked only because signing
  keys are external.

Detailed scope and evidence are in the [profile-consolidation record](verification/2026-08-13-bk7258-config-profile-consolidation.md).

## Next action

Open and merge the published consolidation PR.  After merge, select one
canonical T5-Board product profile for the next hardware/application stage;
do not revive retired checkpoint defconfigs or bypass profile pairing.

## Fixed constraints

- Official NuttX/apps and Beken SDK source remain read-only.
- Preserve P0/P1 SWD/RTT and COM3; never open COM4.
- Do not add one-off verification scripts when an existing real-board path is
  available.
- Do not commit, push, flash or touch OTP/eFuse/lifecycle/debug locks without
  corresponding owner authority.
- GPT-5.6-Luna delegation remains disabled until the owner re-enables it.
