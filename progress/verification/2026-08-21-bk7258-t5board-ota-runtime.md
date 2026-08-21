# Verification: T5Board unified OTA runtime

- Date and time zone: 2026-08-22 Asia/Shanghai
- Verifier: Codex
- Source state: uncommitted worktree at `18510b9a29f6`
- Hardware: T5Board, CP console/download on COM3, one-line TF profile
- Toolchain: official Arm GCC 10.3-2021.10

## Result

**CLEAN_BUILD_PASS / PACKAGE_TRUST_PASS / HTTP_PAIR_STAGING_PASS /
DIRECT_XIP_TRIAL_PASS / DIRECT_XIP_REVERT_PASS /
PAIR_CONFIRM_PASS / CONFIRMED_B_REBOOT_RETENTION_PASS.**

No test target ran. Verification used clean firmware builds, signed package
checks, COM3 download, live Wi-Fi HTTP Range staging and physical resets.

## Final implementation findings

- AP HTTP source uses a 16 KiB bounded Range cache, closes each response
  before CP Flash pauses, and supports socket shutdown for cancellation.
- APPLY acknowledges acceptance asynchronously, so status/cancel can use the
  control plane while staging continues.
- The original 4 KiB progress message cadence exhausted the eight-entry
  one-way vring after six reports. Raising the cadence to 64 KiB moved the
  exact stop from 24 KiB to 384 KiB and proved the cause but did not fix it.
- SDK Flash START/END coordination is mandatory. Replacing `mb_flash_*` with
  no-op wrappers caused CP NMI watchdog failure; those wrappers were removed.
- Progress now uses a 44-byte generation/session-bound seqlock snapshot in the
  unused gap between the RPTUN resource table and carveout. AP synchronizes it
  during status and normal OTA events. RPMsg remains responsible for bounded
  READ/DATA, control, cancellation and final completion.
- Continuous Flash work delayed NuttX watchdog automonitor service. The Pair
  Installer now feeds the already-started lower-half-owned watchdog and yields
  for 1 ms only after each successful sector. It does not disable or extend
  the watchdog, so a stuck operation remains fail-safe.

## Build and signed artifacts

- Layout: `bk7258-5641c11040abf787` (`removable-block`).
- Clean network19 CP/AP/BL2/BL1 build: PASS with GCC 10.3.1.
- Signed network19 baseline package:
  `bfb2f6bbec0a20fc38eab957ac4265197084968200d72b0e2dfc254de081fd57`.
- Baseline package structure and public BL1/BL2/CP/AP signatures: PASS.
- Staging payload was signed apps-only `1.1.0+4`, counter 4, package SHA-256
  `082dd77331bf6174464bd5785394045ad691e4d1048616865ec5a9fed6eeee1f`.
- Final confirmed payload is signed apps-only `1.2.0+5`, counter 5, package
  SHA-256
  `dc675a2cc48479425b9d263a8912a148b42da0d51ec255bbfe6551b107c46e9f`.
  Its package structure and public MCUboot CP/AP signatures pass.
- Publication cleanup removed only unused AP-local ARP observation counters.
  A subsequent clean GCC10 build passed; its eight-image package and public
  BL1/BL2/CP/AP trust verification passed with package SHA-256
  `a787bb6dcf44d7a2ee67e6a3f6f71e9dcccdf45e35c6ca07531f1db33ba45f39`.
  This cleanup artifact was not downloaded; physical acceptance below applies
  to the preceding network19 artifact.

## Hardware sequence

1. Before every apps-only baseline write, J-Link read the 96-byte BL1 and
   BL2 A/B trust sections. All three matched the current ELF trust sections.
2. BKFIL wrote only confirmed CP A `[0x11000,+0x154000]` and AP A
   `[0x165000,+0x121000]`; it reported erase/write/protection success.
3. Active A booted with AP READY, CPU2 online mask `0x3`, RPTUN connected,
   Manager idle and no recorded AP fault.
4. The board associated through Wi-Fi and fetched the authorized catalog and
   CP/AP objects through the router NAT path to the Windows wired server.
5. Live Manager snapshots advanced through CP erase, AP erase, AP write,
   CP write and COMPLETE. Final state was READY_TO_REBOOT, `progress=1/1`,
   error 0. CP sector 0 was written only after both complete SHA-256 checks.
6. RTS reset produced BL2 selection of B. Runtime status reported active B,
   AP READY, CPU2 online, RPTUN connected and no fault. No confirm was issued.
7. A second RTS reset produced BL2 revert selection of A. Runtime status
   reported active A with the same healthy conditions.
8. The final network19 payload repeated complete HTTP staging and reached
   READY_TO_REBOOT with `progress=1/1`, error 0.
9. Trial B passed the AP READY, CPU2 online, RPTUN connected and no-fault
   health gate. `bkota confirm` returned `active CP/AP pair confirmed`.
10. A final RTS reset selected B again. Runtime status remained active B with
    AP READY, CPU2 online mask `0x3`, RPTUN connected and Manager idle.

## Safety and current state

- Failed intermediate attempts never committed CP sector 0; BL2 continued to
  select confirmed A. Watchdog recovery also returned to A.
- No whole-chip erase, OTP/eFuse/lifecycle/debug-lock, root rotation,
  calibration or persistent-data write occurred.
- Two byte-identical 8 MiB recovery backups remain outside the repository.
- Final hardware state is healthy confirmed active B with recoverable inactive
  A. The Range server is stopped.
- Windows denied deletion of the two exact temporary firewall rules without
  administrator privilege; both remain for explicit elevated cleanup.
