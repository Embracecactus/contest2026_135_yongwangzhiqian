# Operations

Last reviewed: 2026-08-15

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
- SDK workspace ownership is recorded as follows:
  - The active read-only BK7258 SDK source is synchronized by the team
    manifest at `vendor/beken/bk_avdk_smp` and pinned to the commit/tree in
    ADR-027. `bk7258.py sdk rebuild --source` is mandatory and the supplied
    clean checkout must equal that manifest revision.
  - Imported runtime bundles live only under the ignored canonical directory
    `board/bk7258/bk_idk/armino_as_lib/versions/<version>/<profile>` and must
    be real directories matching the tree hash in the selected profile.
  - The OpenVela ARM prebuilt at
    `prebuilts/gcc/linux-x86_64/arm-none-eabi` is pinned by the team manifest.
    OpenVela, SDK rebuild and project BL1/BL2 share it; `/usr/bin` and PATH are
    not compiler fallbacks.
- The active compatible SDK bundle remains v3.1.1.9, with one AP-only SDIO4
  variant derived from the same source/profile family. Matching SDK source is
  the manifest project pinned by ADR-027; no older bundle fallback is
  supported. BK7259 and v4 are retired and cannot replace it.
- `tools/bk7258/bk7258.py sdk list|verify|install|rebuild` owns profile
  discovery, tree verification and the locked rebuild/replace transaction.
  There are no standalone manifests or provenance files.
- `tools/bk7258/bk7258.py package create|extract` and
  `bk7258.py verify layout|image|package|trust` are the complete host package
  and verification surface.

## Required verification

- Run `tools/bk7258/bk7258.py sdk verify --profile NAME` for each selected
  bundle and the direct `verify layout|image|package` gates.
- Require `git diff --check`; confirm the build introduced no new tracked
  changes in official `nuttx/` or `apps/` beyond their recorded baseline.
- For a completed hardware stage, retain raw UART/J-Link logs, artifact hashes, physical reset evidence, and regression tests proportional to the change.
- Canonical N14 matrix: [N14 evidence index](../docs/bk7258-t5ai/nuttx-port/n14-evidence-index.md).
- For BL1/BL2/MCUboot changes, run the affected source/host gate and one full
  signed CP/AP integration build. Hardware fallback or destructive mutation
  remains separate, range-specific validation work.
- Deployed artifacts must match the package layout identity. Persistent and
  immutable ranges come from that layout; never infer them from an older
  board capture.

## Build and release

- A compatible SDK update changes the team manifest pin, rebuilds each
  explicit profile from that clean source, records the new tree hash comment,
  and passes a real OpenVela link/build before acceptance.
- Build requires every semantic input explicitly; there is no default pair:

  ```text
  bk7258.py build --cp-config PATH --ap-config PATH \
    --boot direct|mcuboot --partition CSV --jobs N
  ```
- Every maintained CP/AP profile carries `profile.conf`.  The wrapper rejects
  board, role, feature and compatibility mismatches before build. Boot mode is
  an explicit command input; MCUboot defconfig overlays are build-local.
- Do not run another OpenVela configure/build concurrently with
  `bk7258.py build`; official classic clean still touches shared NuttX state,
  while CMake outputs remain under `out/bk7258/<config>/cmake`.
- Follow [the build/flash/debug SOP](../docs/bk7258-t5ai/nuttx-port/bk7258-build-flash-debug-sop.md) rather than reconstructing commands from memory.
- The build wrapper rejects mismatched CP/AP pairs and a storage topology that
  differs from the selected CSV.
- Signed package creation must match explicit keys to the public-anchor
  sections in the actual BL1/BL2 ELFs before invoking official signers. Public
  fingerprints and artifact hashes are embedded in `.bkpack`; no standalone
  trust file or private-key path is retained.
- No active N15/N17 field-update candidate, validation profile, PSRAM loader,
  board SOP or aggregate fault campaign exists. Their historical records are
  evidence only and must not be reconstructed or treated as build gates.
- Commit and push only when explicitly authorized. After either, update `progress/CURRENT.md` with exact commit and remote state.

## Deployment

- Normal flashing consumes only a verified package Flash contract. It must
  preserve every CSV `preserve`/`immutable` range and any explicitly omitted
  external artifact.
- Every MCUboot hardware workflow in `tools/windows-hardware-debug` must
  perform the non-halting BL1/BL2 target-fingerprint preflight before starting
  `bk_loader`. Treat
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
  evidence, not maintained rollback profiles. Do not reconstruct them from
  memory; pass two current explicit config directories to `bk7258.py build`.
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
