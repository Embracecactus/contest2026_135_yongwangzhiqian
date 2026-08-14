# Operations

Last reviewed: 2026-08-14

Do not place credentials, tokens, private keys, or sensitive production data in this file.

## Environments

- Workspace root contains official `nuttx/` and `apps/` siblings plus this contest repository.
- Active physical target: T5-Board/BK7258.  The owner-confirmed current route
  uses COM3/UART1 at 115200 8N1 for download and console.  COM4 is disabled by
  its DIP switch and conflicts with the active debug arrangement; do not open
  it.  Preserve the P0/P1 SWD route and RTT support.
- Owner-confirmed hardware topology: the J-Link pin-15 RST signal is physically
  wired to board RST.  This clone probe's old firmware cannot reliably access
  the top shared-SRAM BL2 release word while caches are enabled; use bounded
  register/memory inspection and the documented final-hold continuation, never
  skip a BL2 validation or boot-policy branch.
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
- Build paired CP/AP profiles with `board/bk7258/scripts/build_dual_image.sh`.
  The default direct pair is `t5ai_core_cp_base + t5ai_core_ap_base`; the
  current profile catalog and compatible pairs are maintained in
  [`board/bk7258/configs/README.md`](../board/bk7258/configs/README.md).
- Every maintained CP/AP profile carries `profile.conf`.  The wrapper rejects
  board, role, boot-mode, feature and compatibility mismatches before build.
  CI-only profiles additionally require `BK7258_ALLOW_CI_PROFILE=YES`.
- Physical dual-image builds hold `/tmp/openvela-bk7258-build-$UID.lock`
  because openvela mutates the shared `nuttx/` and `apps/` trees.  Do not
  bypass that wrapper or run a second direct configure/build concurrently.
  `BK7258_PROFILE_CHECK_ONLY=YES` remains lock-free and performs no build.
- Follow [the build/flash/debug SOP](../docs/bk7258-t5ai/nuttx-port/bk7258-build-flash-debug-sop.md) rather than reconstructing commands from memory.
- The build wrapper rejects mismatched CP/AP feature-profile pairs and runs post-link verification.
- For the MCUboot host-reference pipeline, leave `MCUBOOT_OFFICIAL_PIPELINE=YES`
  and omit `SECUREBOOT_AES_TOOL`/`SECUREBOOT_AES_KEY_FILE` for the no-AES
  branch.  Supplying both external paths opts into the SDK v3.1.1.9 AES step;
  no key is stored in this repository and the resulting stream remains
  host-reference-only until the BK7258 BootROM consumer is proven.
- A signed build emits `bk7258-trust-chain.json`.  Packaging must re-resolve
  the BL1/BL2 symbols, bind that public contract to `bootloader.bin` and
  `bl2.bin`, and revalidate after staging into a clean output directory; no
  private-key path belongs in the package.  Do not hand-edit the contract to
  make a target pass.
- No active N15/N17 field-update candidate, validation profile, PSRAM loader,
  board SOP or aggregate fault campaign exists. Their historical records are
  evidence only and must not be reconstructed or treated as build gates.
- Commit and push only when explicitly authorized. After either, update `progress/CURRENT.md` with exact commit and remote state.

## Deployment

- Normal sparse flashing must use CP raw `0x011000..0x165000`, AP raw
  `0x165000..0x286000`, and preserve LittleFS at `0x600000..0x700000`.
- Every MCUboot flash mode in `bk7258_auto_debug.sh` performs the non-halting
  BL1/BL2 target-fingerprint preflight before starting `bk_loader`.  Treat
  J-Link failure or an identity with no matching permitted address as a
  package/target pairing failure; do not add a bypass.  Use
  `--flash --sparse-flash --apps-only --no-console` for routine
  CP/AP updates so BL1, BL2, Manifest, secondary and data regions remain
  untouched.  Root rotation is a separately designed and authorized recovery
  workflow, not a downloader option.
- The current board still matches the reviewed legacy probe addresses; the
  linker-reserved fixed blocks read erased until a future explicitly authorized
  BL1/BL2 write.  Probe both sets read-only, accept one exact match per identity,
  and never treat erased fixed blocks alone as a failure while compatibility is
  active.
- The one-time migration is complete. Reusing its factory path or performing
  any other destructive Flash action requires fresh owner authority. Chip
  erase and calibration-tail writes remain forbidden.
- New tooling must carry a layout ID and reject pre-migration segment offsets.
- All `s_app`, BL1 Manifest and BL2 writes require fresh, exact-range owner
  authority. Source/dry-run verification does not grant it.
- A flash PASS is not sufficient: require a new serial capture, `PASS_NSH`, and the stage-specific health command.
- MIC lower-half acceptance is a stage-specific exception with stronger
  direct evidence: exercise at least 10 complete public-audio-API cycles,
  including pause/resume and close/reopen, and prove every fitted channel is
  non-silent.  A stereo board must additionally show L/R are not mirrored.
  Remove temporary register/ISR telemetry and disable the lifecycle validator
  in the final runnable profile, then rebuild, apps-only flash, and confirm AP
  `READY`, RPTUN `CONNECTED`, zero errors and advancing CP/AP heartbeats.
- ADR-003 staging/journal/scratch addresses are retired and remain forbidden.
- For critical-region BKFIL read-back, use 115200 and require two
  byte-identical captures. High-speed 6 Mbps reads can insert isolated
  128-byte zero blocks and are forensic-only.

## Rollback and recovery

- The former N13 `cp_nsh_ble_gatt + ap_smp_ble_gatt` names are historical
  evidence, not maintained rollback profiles.  Do not reconstruct them from
  memory; select a current pair from the canonical profile catalog.
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
