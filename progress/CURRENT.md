# Current Progress

Last updated: 2026-08-22
Updated by: Codex

## Objective

The rebuilt T5Board Wi-Fi paired OTA path is complete through staging, trial,
health confirmation and retained confirmed boot. The next phase is automatic
health confirmation, followed by the real T5Board TF file-source path while
keeping the same Manager/source/Pair Installer architecture.

## Repository state

- Repository: `contest2026_135_yongwangzhiqian`
- Branch: `feat/bk7258-standard-paired-ota`
- Implementation commit: `b506f2e` (`feat(bk7258): rebuild native platform
  and paired OTA`); this project-memory checkpoint is committed on top.
- Unverified CH32 files/manifest lines, historical logs and the tests-only
  newline change remain intentionally outside the publication scope.
- Official project build entry resolves the content-locked Arm GCC 10.3.1
  toolchain. No test target ran.

## Accepted architecture

- [ADR-031](../memory/decisions/ADR-031-bk7258-standard-mcuboot-paired-direct-xip-ota.md)
  owns paired MCUboot trial/revert/confirm.
- [ADR-032](../memory/decisions/ADR-032-bk7258-unified-field-ota-platform.md)
  owns the AP Manager, interchangeable sources, RPMsg and CP Pair Installer.
- [ADR-033](../memory/decisions/ADR-033-openvela-native-beken-layout-and-gcc10.md)
  owns the native Beken projection and content-locked GCC10.

## Verified checkpoint

- T5Board uses one-line TF/SDIO and UART0/COM3. P0/P1 SWD targets CP; UART1 is
  disabled and the P1 user LED is suppressed by the existing conflict gate.
- A clean GCC10 CP/AP/BL2/BL1 build passed for layout
  `bk7258-5641c11040abf787`. The signed network19 baseline package passed
  eight-image structure and public BL1/BL2/CP/AP signature verification.
- The clean build and eight-image public-trust verification passed again after
  removing the temporary AP-side ARP observation fields used during routing
  diagnosis.
- CP remains the only on-chip Flash writer. SDK `mb_flash_*` coordination is
  retained; the rejected no-op wrapper caused a hardware NMI and was removed.
- OTA progress is published through a 44-byte seqlock snapshot in the unused
  RPTUN resource-table gap. Progress no longer consumes one-way RPMsg vring
  buffers; READ/DATA, control and COMPLETE remain versioned RPMsg messages.
- The Pair Installer services the live NuttX-owned watchdog and yields after
  every successfully completed Flash sector. It never disables the watchdog;
  a stuck sector still times out.
- Authorized HTTP Range staging of signed `1.1.0+4`/counter 4 completed all
  CP/AP erase, AP write/hash, CP write/hash and final CP-sector-0 commit.
  Manager reached READY_TO_REBOOT with `progress=1/1`, error 0.
- The first payload proved healthy trial B and unconfirmed revert A. The final
  network19 payload then completed staging, booted healthy trial B, returned
  `active CP/AP pair confirmed`, and remained active B after another reset.
- Final status is confirmed active B, inactive A, AP READY, CPU2 online mask
  3, RPTUN connected, Manager idle and no AP fault.
- Detailed evidence:
  [T5Board OTA runtime](verification/2026-08-21-bk7258-t5board-ota-runtime.md).

## Confirmed release payload

- New apps-only package: network19, version `1.2.0+5`, security counter 5.
- Package SHA-256:
  `dc675a2cc48479425b9d263a8912a148b42da0d51ec255bbfe6551b107c46e9f`.
- Package structure and public MCUboot CP/AP signatures pass.
- This payload was staged and confirmed on hardware. No Range service is
  running.

## Exact next action

Implement the automatic CP/AP health-confirm policy so production firmware
does not depend on the operator `bkota confirm` command. Then identify the
actual mounted T5Board TF filesystem and run the same signed pair installer
through `apply-file` using the one-line SDIO profile.

## Remaining platform scope

- T5Board TF file-source staging still needs the real card filesystem path;
  Windows `D:` is ToyAI storage and is not T5Board evidence.
- T5AI-Core HTTPS selection/runtime, BLE-only transfer, durable TF/NAND
  resume, UART/USB field transport, resource/model updates and delta remain.
- The two exact temporary Windows firewall rules remain because Windows
  rejected removal without administrator privilege. Temporary AP-side ARP
  observation fields were removed before publication.

## Current prohibitions

- Do not read or reuse historical OTA adaptation code or documentation.
- Do not add, modify or run tests.
- Do not rotate roots, erase the whole chip, write OTP/eFuse/lifecycle,
  calibration or persistent-data regions, or enable debug lock.
- Do not search for, print or record private-key contents/paths or credentials.
