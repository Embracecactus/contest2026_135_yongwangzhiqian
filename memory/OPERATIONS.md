# Operations

Last reviewed: 2026-08-04

Do not place credentials, tokens, private keys, or sensitive production data in this file.

## Environments

- Workspace root contains official `nuttx/` and `apps/` siblings plus this contest repository.
- Active physical target: Tuya T5-AI/BK7258; current workstation mapping uses
  COM7 for download/reset and COM11 at 460800 8N1 for UART. Both ports come
  from the USB-to-UART device, so the USB connection must remain present to
  preserve their power and availability. Rediscover ports before each session.
- Owner-confirmed hardware topology: the J-Link RST signal is physically wired
  to the board RST signal. This wiring fact does not by itself prove a specific
  J-Link Commander reset command or reset type. On the current setup, a 150 ms
  COM7 RTS pulse is the proven reset method because its resulting `BClk` cold
  boot was captured on COM11; use it for the automated reset-recovery matrix
  until the J-Link RST command path is separately demonstrated.
- USB and J-Link can power the target simultaneously. Removing only J-Link
  target power while USB remains connected does not remove BK7258 VDD and must
  not be recorded as a complete power cycle.
- Sole active SDK bundle: `board/bk7258_t5ai/bk_idk/armino_as_lib/versions/v3.1.1.9` (ignored, checksum-pinned). Do not run legacy-version checks until N15 completes on v3.1.1.9 and the owner opens a separate task.
- Matching SDK source is external and read-only; supply it through `BK7258_SDK_SOURCE` for source verification.

## Required verification

- Run the stage-specific source/ELF verifier and existing RPTUN/BLE/packaging gates.
- Run CP and AP SDK bundle `--check` for the selected version.
- Require `git diff --check`; confirm official `nuttx/` and `apps/` tracked diffs are zero.
- For a completed hardware stage, retain raw UART/J-Link logs, artifact hashes, physical reset evidence, and regression tests proportional to the change.
- Canonical N14 matrix: [N14 evidence index](../docs/bk7258-t5ai/nuttx-port/n14-evidence-index.md).
- For N15 implementation changes, run the affected portable format-2 tests,
  one A-to-B and one B-to-A package check, and one normal integration build.
  Exhaustive campaigns and hardware procedures are separate, explicitly
  authorized validation work rather than routine implementation gates.
- The deployed board uses CP `0x011000`, AP `0x165000`, and raw LittleFS
  `0x600000..0x700000`. Never mix old-layout images or offsets with the
  migrated board.

## Build and release

- Build paired CP/AP profiles with `board/bk7258_t5ai/scripts/build_dual_image.sh`; N14 uses `cp_nsh_psram + ap_smp_psram` and v3.1.1.9.
- Follow [the build/flash/debug SOP](../docs/bk7258-t5ai/nuttx-port/bk7258-build-flash-debug-sop.md) rather than reconstructing commands from memory.
- The build wrapper rejects mismatched CP/AP feature-profile pairs and runs post-link verification.
- A host OTA candidate is generated only when `N15_OTA_GENERATION`,
  `N15_OTA_VERSION`, `N15_OTA_BASE_VERSION`, and `N15_OTA_TIMESTAMP` are all
  explicitly supplied. Normal host-only output lives under
  `bk7258-dual/n15-ota-host-candidate/`; the gates-on profile uses the isolated
  `bk7258-dual-ota-validation/n15-ota-host-candidate/`. Neither is implicitly
  added to factory loader ranges.
- No active N15 board SOP is maintained. Define and review a bounded physical
  plan only when the owner explicitly opens board validation. The PSRAM loader
  is dry-run by default; real execution requires target-side
  `bkota prepare-transfer`, `--watchdog-stopped`, `--execute`, and fresh board
  authority. A host-only campaign may be generated with
  `pack_bk7258_ota_campaign.py`, then require
  `verify_bk7258_ota_campaign.py`; this does not authorize board execution.
- The retired reset-campaign/SOP work is historical evidence only. It is not
  part of the current OTA implementation or build acceptance path.
- Commit and push only when explicitly authorized. After either, update `progress/CURRENT.md` with exact commit and remote state.

## Deployment

- Normal sparse flashing must use CP raw `0x011000..0x165000`, AP raw
  `0x165000..0x286000`, and preserve LittleFS at `0x600000..0x700000`.
- The one-time migration is complete. Reusing its factory path or performing
  any other destructive Flash action requires fresh owner authority. Chip
  erase and calibration-tail writes remain forbidden.
- New tooling must carry a layout ID and reject pre-migration segment offsets.
- The N15 validation profile and all `s_app`/metadata writes require fresh,
  exact-range owner authority. Source/dry-run verification does not grant it.
- A flash PASS is not sufficient: require a new serial capture, `PASS_NSH`, and the stage-specific health command.
- ADR-003 staging/journal/scratch addresses are retired and remain forbidden.
- For critical-region BKFIL read-back, use 115200 and require two
  byte-identical captures. High-speed 6 Mbps reads can insert isolated
  128-byte zero blocks and are forensic-only.

## Rollback and recovery

- N13 `cp_nsh_ble_gatt + ap_smp_ble_gatt` is the no-PSRAM BLE rollback pair.
- The immutable pre-N14 source rollback point is commit `c6afd6f9b73dcf862f17bd31f5b2dc90820b9bb0`.
- Recover a nonbooting board with the known Tier-1/minimal bootloader and documented sparse segments; do not erase broad ranges by inference.
- Keep the N14 source/commit as a historical recovery input, but repack any
  recovery image for ADR-004 before use. Never recover the migrated board with
  old sparse offsets.
- The pre-migration 8 MiB read at 6 Mbps is not a bit-exact backup and must not
  be reflashed. Rebuild from the pinned source/bundle or use a separately
  verified 115200 read-back instead.

## Observability and support

- CP NSH commands include `apctl`, `bkrpmsgtest`, `bkrpmsgfstest`, `bkbttest`, `bkpsramtest`, and `bktimertest` under matching profiles.
- Use raw UART logs as evidence; use J-Link only for bounded register/memory inspection and avoid leaving diagnostic telemetry in the final image.
- Store summarized, non-sensitive evidence routing in `progress/verification/`; keep full raw logs in the canonical stage log tree.
