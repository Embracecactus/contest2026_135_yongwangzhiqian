# Current Progress

Last updated: 2026-08-10 GMT+8
Updated by: Codex

## Active objective

The active BK7258 boot chain is:

```text
legacy BootROM -> board-owned BL1 -> signed Manifest
-> pinned NuttX MCUboot BL2 -> signed same-slot CP/AP pair -> NuttShell
```

The old N15/N17 self-developed OTA lifecycle has been retired from active
code. Its selector, journal, staging, publication, trial/rollback, fault
injection, release-key and validation-script implementations are removed.
Historical ADRs and verification records remain evidence of work that was
previously performed; they are not descriptions of the current firmware.

## Current boot and partition baseline

- Runtime SDK: official BK7258 v3.1.1.9 only, linked through board wrappers.
- NuttX and official SDK source trees remain unchanged.
- BL1 is still a complete source implementation: clock/reset/watchdog setup,
  ECDSA-P256/SHA-256 Manifest authorization, primary-then-secondary BL2
  fallback, vector/copy checks, SRAM policy publication and handoff.
- BL2 is still the pinned NuttX MCUboot implementation. It verifies MCUboot
  image/TLV metadata and accepts only a version/counter-compatible CP/AP pair
  from the same physical slot.
- BL1 has one production object closure. It no longer links any N15/N17
  lifecycle selector, Flash writer, software journal or release key.
- The active CSV keeps contiguous CP/AP A and B pairs, two read-only BL1
  Manifest sectors, two read-only BL2 copies, LittleFS and the official
  calibration tail. It has no OTA metadata bank or authorization-policy
  sector.
- Flash MTD exposes ordinary data partitions plus read-only MCUboot/Manifest
  regions; there is currently no firmware update writer or installer.

## Verification at this checkpoint

Implementation commits on `feat/bk7258-sdk-peripheral-r2`:

- `157a2c7`: reproducible v3.1.1.9 AP peripheral SDK profile and provenance.
- `30923f9`: T5-Board GC2145/V4L2 capture and PWM lower-half integration.

On 2026-08-10, 32-job signed dual-image builds, sparse downloads and board
validation passed without modifying NuttX or SDK sources:

- `ap_smp_camera_mcuboot` captured a valid 11507-byte 640x480 MJPEG frame
  through `/dev/video0` and reported `BKCAM PASS` after SOI/EOI checks.
- `ap_smp_pwm_mcuboot` drove the attached RGB LCD backlight through P9/PWM3
  at configured 100/0/10/50/90 percent levels.  Serial reported `BKPWM PASS`
  and the owner confirmed visible brightness changes.
- Both runs reached BL2 handoff and NuttShell with no panic or fault.
- Partition, 32+2 CRC and factory-layout checks passed; LittleFS and the
  official calibration tail were preserved by sparse download.

## Other verified platform state

- CP NuttX, AP SMP, RPTUN/RPMsg/RPMsgFS, Bluetooth, Wi-Fi STA, PSRAM and the
  established peripheral wrappers remain outside this cleanup and are not
  intentionally changed.
- T5AI-Core is the default physical board. T5-Board wiring is separated under
  `boards/t5_board`; its ILI9488 RGB LCD displayed the expected color bars.
- Driver backlog conclusions and board evidence are recorded in
  [AP peripheral board evidence](verification/2026-08-09-bk7258-ap-peripheral-board-evidence.md).
- The official v3.1.1.9 AP bundle now has a reproducible board-owned
  `ap-peripherals-r2` profile.  PWM and generic DVP compile/link in the AP
  drivercheck image; the T5-Board V4L2 camera captured a valid JPEG and the
  PWM lower half visibly controlled the RGB LCD backlight.  Evidence:
  [SDK peripheral profile, PWM and DVP](verification/2026-08-10-bk7258-sdk-peripheral-profile-pwm-dvp.md).

## Honest boundary

The chain is software-rooted. It does not prove that BK7258 BootROM consumes
the repository Manifest and does not provide OTP/eFuse-backed root trust or
persistent hardware anti-rollback. No OTP/eFuse, secure-lifecycle or debug
lock bit has been written.

The current source provides authenticated boot, not a complete field-update
lifecycle. Restoring the deleted custom N15/N17 OTA state machines would be a
regression. A future updater must be designed against NuttX MCUboot semantics
and the frozen CP/AP same-slot contract.

## Next step

1. Publish and review the SDK peripheral-profile, camera and PWM commits.
2. After merge, implement the already-exported CAN lower half;
   hardware loopback remains pending until a CAN transceiver is available.
3. Only when field update is requested, design its transport, inactive-slot
   writer, confirmation and rollback flow directly around MCUboot; do not
   restore the retired N15/N17 custom journal.
4. Keep hardware Secure Boot provisioning deferred until separate authority;
   never write OTP/eFuse as part of ordinary validation.

## Fixed constraints

- Do not modify NuttX or official SDK source except temporary debugging that
  is fully restored.
- Do not mix BK7259, v4.x or BK7236 runtime artifacts into the product path.
- Private signing keys must never enter the repository, logs or memory.
- Hardware mutation, commit, push and PR actions keep their normal authority
  boundaries.
