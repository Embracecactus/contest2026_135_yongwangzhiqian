# Operations

Last reviewed: 2026-08-10

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
- 2026-08-07 host access check: Windows exposes the J-Link as USB `8-4`
  (S/N `20790067`, VTref about `3.30 V`) and WSL can invoke the Windows
  Commander directly; no usbipd attach is needed and the Windows UART path is
  preserved. SWD read-only discovery reached STAR r1p0 (`CPUID=0x631F1320`).
  Commander warns that this J-Link V9 firmware does not reliably handle an
  enabled I/D-cache. Treat breakpoint/single-step observations with cache
  enabled as diagnostic-only; use bounded register/memory reads and UART for
  acceptance evidence.
- USB and J-Link can power the target simultaneously. Removing only J-Link
  target power while USB remains connected does not remove BK7258 VDD and must
  not be recorded as a complete power cycle.
- SDK workspace locations are recorded as follows:
  - `/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9` is the active,
    read-only BK7258 SDK source snapshot.
  - `/tmp/bk-idk-v201` is a disposable read-only checkout of Beken
    `bk_idk release/v2.0.1` (`650e754e12fe1e43c37ce2316a973668b033fd48`) for
    BK7236 secureboot source review only.
  - `/home/lijian/project/armino/vendor_beken` is a third-party Git mirror,
    not an official SDK implementation input.
- The active compatible SDK bundle remains v3.1.1.9. Matching SDK source is
  external and read-only; supply it through `BK7258_SDK_SOURCE` for source
  verification. BK7259 and v4 are retired and cannot replace it.

## Required verification

- Run the stage-specific source/ELF verifier and existing RPTUN/BLE/packaging gates.
- Run CP and AP SDK bundle `--check` for the selected version.
- Require `git diff --check`; confirm official `nuttx/` and `apps/` tracked diffs are zero.
- For a completed hardware stage, retain raw UART/J-Link logs, artifact hashes, physical reset evidence, and regression tests proportional to the change.
- Canonical N14 matrix: [N14 evidence index](../docs/bk7258-t5ai/nuttx-port/n14-evidence-index.md).
- For BL1/BL2/MCUboot changes, run the affected source/host gate and one full
  signed CP/AP integration build. Hardware fallback or destructive mutation
  remains separate, range-specific validation work.
- The deployed board uses CP `0x011000`, AP `0x165000`, and raw LittleFS
  `0x600000..0x700000`. Never mix old-layout images or offsets with the
  migrated board.

## Build and release

- A future compatible SDK update is a fresh export, never a rename or reuse
  of v3 archives: resolve the official tag to a commit, confirm BK7258 CP/AP
  profiles, build clean role outputs, import them into a new versioned bundle,
  record manifests/provenance, then run the bounded ABI/link review before
  changing the default selector.
- Build paired CP/AP profiles with `board/bk7258/scripts/build_dual_image.sh`; the historical N14 profile uses `cp_nsh_psram + ap_smp_psram` and v3.1.1.9.
- Follow [the build/flash/debug SOP](../docs/bk7258-t5ai/nuttx-port/bk7258-build-flash-debug-sop.md) rather than reconstructing commands from memory.
- The build wrapper rejects mismatched CP/AP feature-profile pairs and runs post-link verification.
- For the MCUboot host-reference pipeline, leave `MCUBOOT_OFFICIAL_PIPELINE=YES`
  and omit `SECUREBOOT_AES_TOOL`/`SECUREBOOT_AES_KEY_FILE` for the no-AES
  branch.  Supplying both external paths opts into the SDK v3.1.1.9 AES step;
  no key is stored in this repository and the resulting stream remains
  host-reference-only until the BK7258 BootROM consumer is proven.
- No active N15/N17 field-update candidate, validation profile, PSRAM loader,
  board SOP or aggregate fault campaign exists. Their historical records are
  evidence only and must not be reconstructed or treated as build gates.
- Commit and push only when explicitly authorized. After either, update `progress/CURRENT.md` with exact commit and remote state.

## Deployment

- Normal sparse flashing must use CP raw `0x011000..0x165000`, AP raw
  `0x165000..0x286000`, and preserve LittleFS at `0x600000..0x700000`.
- The one-time migration is complete. Reusing its factory path or performing
  any other destructive Flash action requires fresh owner authority. Chip
  erase and calibration-tail writes remain forbidden.
- New tooling must carry a layout ID and reject pre-migration segment offsets.
- All `s_app`, BL1 Manifest and BL2 writes require fresh, exact-range owner
  authority. Source/dry-run verification does not grant it.
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
