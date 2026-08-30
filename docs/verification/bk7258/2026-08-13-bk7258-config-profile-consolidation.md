# BK7258 Config Profile Consolidation

- Date: 2026-08-13
- Baseline: `9de9912`
- Implementation commit: `aba1eb0`
- Branch: `refactor/bk7258-config-profiles`
- Remote: `fork/refactor/bk7258-config-profiles`
- State: implementation and host/build verification complete; published for PR

## Conclusion

`configs/` now describes supported firmware profiles rather than preserving
every bringup checkpoint.  A profile belongs to one physical PCB and one
firmware role; compatible CP/AP images are paired by explicit metadata.  Fixed
electrical facts remain in the board variant, so multiple profiles do not
duplicate the pin database introduced by ADR-023.

The maintained catalog contains 18 defconfigs and 18 matching metadata files:
eight T5AI-Core profiles, nine T5-Board profiles and standalone
`bl2_mcuboot`.  The previous 42-defconfig surface is retired.

## Pairing and packaging gates

`build_dual_image.sh` validates before any build:

- metadata schema, physical board, CP/AP role, boot mode and profile class;
- Kconfig role and physical-board selection;
- explicit compatibility ID;
- symmetric RPTUN, Bluetooth IPC and Wi-Fi VNET features;
- raw versus MCUboot metadata/Kconfig agreement;
- explicit authorization for CI-only pairs;
- external signing/Manifest keys for MCUboot product builds.

`BK7258_PROFILE_CHECK_ONLY=YES` exercises these gates without building or
reading signing keys.  All ten documented compatible pairs passed.  Negative
checks rejected a T5AI-Core/T5-Board pair, a T5-Board app/Wi-Fi compatibility
mismatch and a drivercheck pair without the CI allow gate.

The physical build path now takes
`/tmp/openvela-bk7258-build-$UID.lock`.  This was added after another active
session replaced `nuttx/Make.defs`, removed a generated apps Kconfig between
write/read and overwrote output ELF files during a PSRAM rerun.  With the lock,
the subsequent T5-Board dual build completed without shared-tree corruption.

## Build evidence

The following entity builds used official SDK v3.1.1.9 source/bundles:

1. `t5ai_core_cp_base + t5ai_core_ap_base` completed BL1, CP, AP, restored CP,
   dual packaging, partition, factory-layout and SDK-wrapper verification.
2. `t5ai_core_cp_psram_validation + t5ai_core_ap_psram_validation` compiled,
   linked and packaged.  BLE, RPTUN, factory-layout and SDK-wrapper checks
   passed.  The run first exposed a duplicate
   `rwnxl_set_wifi_low_vol_flag`: CP BT IPC links the SDK Wi-Fi archive as its
   PHY closure even when Wi-Fi VNET is disabled.  The stub guard was corrected
   so the archive owns that symbol; CP/AP relink then passed.  After updating
   the stale bringup-layer/profile-name assumptions, the PSRAM source/ELF
   verifier passed directly against the generated images.
3. With the build lock active,
   `t5_board_cp_drivercheck + t5_board_ap_drivercheck` completed the full raw
   dual-image flow.  CP was 245008 raw bytes and AP was 235960 raw bytes;
   factory-layout, SDK partition-wrapper and RPTUN ELF gates passed.

The PSRAM wrapper's pre-lock full rerun was intentionally not recorded as an
end-to-end PASS: another session changed the shared configuration at its final
`savedefconfig`.  Both firmware roles had already linked, and the relevant
source/ELF verifier passed independently.  The later locked T5-Board run is the
acceptance proof for the concurrency fix.

## Regression evidence

- `board/bk7258/tests/run_tests.sh`: PASS, including RPTUN mailbox `0/31`
  failed, BL1 policy and PM activity.
- Shell syntax for changed scripts: PASS.
- Python bytecode compilation for changed verifiers: PASS.
- Inventory: exactly 18 `defconfig` and 18 `profile.conf` files.
- Lock contention negative test: a second physical build timed out at the
  configured one-second boundary before touching the build tree.
- `git diff --check`: PASS.

No board flash, reset, COM port, RTT or J-Link operation was performed.  No
official NuttX, apps or SDK source change is part of this phase.  Signed
MCUboot profiles were metadata-checked but not entity-built because their keys
remain external and no fresh hardware/deployment authority was given.
