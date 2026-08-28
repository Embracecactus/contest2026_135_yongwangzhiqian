# Operations

Last reviewed: 2026-08-28

Do not place credentials, tokens, private keys, or sensitive production data in this file.

## Environments

- Workspace root contains official `nuttx/` and `apps/` siblings plus this contest repository.
- Active physical target: T5-Board/BK7258.  The owner-confirmed current route
  reuses COM3 for both BKFIL download and the CP UART0 console at 115200 8N1;
  AP syslog is forwarded over RPMsg to CP and therefore appears on the same
  COM3 stream.  COM4/UART1 stays disabled because it conflicts with the P0/P1
  SWD route; do not open or enable it.
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
    `chips/bk7258/bk_idk/armino_as_lib/versions/<version>/<profile>` and must
    be real directories matching the tree hash in the selected profile.
  - The OpenVela ARM prebuilt at
    `prebuilts/gcc/linux-x86_64/arm-none-eabi` is pinned by the team manifest.
    OpenVela, SDK rebuild and project BL1/BL2 share it; `/usr/bin` and PATH are
    not compiler fallbacks.
- The active compatible SDK bundles remain v3.1.1.9 CP and AP. The tracked
  `ap-sdio4` profile is an optional four-line hardware-capability variant and
  may be listed while its proprietary bundle is not installed; it is not a
  build input unless a board seed selects it. Matching SDK source is the
  manifest project pinned by ADR-027. BK7259 and v4 are retired and cannot
  replace it.
- `tools/bk7258/bk7258.py sdk list|verify|install|rebuild` owns profile
  discovery, tree verification and the locked rebuild/replace transaction.
  There are no standalone manifests or provenance files.
- `tools/bk7258/bk7258.py release full|ota` is the complete signed publication
  surface. It accepts only a verified MCUboot build manifest. `package create`
  is unsigned direct-boot diagnostics only; package extraction, flash-contract,
  materialization and all `verify` commands are inspection or compatibility
  operations. The build manifest, package manifest, signed update catalog and
  release summary carry one validated physical-board target; sharing a layout
  never makes two physical boards interchangeable update targets.
- `release full` cryptographically verifies the signed full package and
  combines it with an exact accepted-board base into the one dense BKFIL input
  in one atomic output directory. It refuses a wrong base digest, a size other
  than the immutable-tail boundary, a changed `preserve` partition,
  overlapping writes, a direct-build manifest or an existing output.

## Required verification

- Run `tools/bk7258/bk7258.py sdk verify --profile NAME` for each selected
  bundle and the applicable `verify layout|image|build-manifest|package|trust`
  gates.
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
- Normal development names one physical board; its checked `openvela.conf`
  supplies the maintained CP/AP pair and partition from one source of truth:

  ```text
  bk7258.py build --board NAME --boot direct|mcuboot
  ```
- xTS, performance and driver-check work may instead use the explicit
  `--cp-config PATH --ap-config PATH --partition CSV` form. It cannot be mixed
  with `--board`.
- `direct` is the unsigned `BootROM -> BL1 -> CP` bring-up/diagnostic chain and
  cannot be released. `mcuboot` is the only signed release chain. This
  software-rooted chain does not claim the OP-TEE, HUK provisioning or
  hardware-immutable Secure Boot architecture in official document 1594.
- Every maintained CP/AP profile carries `profile.conf`.  The wrapper rejects
  board, role, feature and compatibility mismatches before build. Boot mode is
  an explicit command input; MCUboot defconfig overlays are build-local.
- Do not run another OpenVela configure/build concurrently with
  `bk7258.py build`; official classic clean still touches shared NuttX state,
  while CMake outputs remain under `out/bk7258/<config>/cmake`.
- Follow [the build/flash/debug SOP](../docs/bk7258-t5ai/nuttx-port/bk7258-build-flash-debug-sop.md) rather than reconstructing commands from memory.
- The build wrapper rejects mismatched CP/AP pairs and a storage topology that
  differs from the selected CSV.
- A successful build atomically writes one
  `out/bk7258/.../releases/<boot>/build-manifest.json`. Signed release accepts
  only the MCUboot form and re-hashes its raw images, ELFs, resolved configs,
  SDK bundles, locked toolchain, layout and public anchors. It then matches the
  private keys to the public-anchor sections in the actual BL1/BL2 ELFs before
  invoking official signers. Public fingerprints and artifact hashes are
  retained; no private-key path is retained.
- Unsigned diagnostic packaging accepts only a verified direct build manifest:
  `bk7258.py package create --build-manifest PATH --unsigned --output PATH`.
  It derives the board, layout, finalized images, SDK evidence and preserved
  partitions from that single handoff and accepts no duplicate manual inputs.
- No active N15/N17 field-update candidate, validation profile, PSRAM loader,
  board SOP or aggregate fault campaign exists. Their historical records are
  evidence only and must not be reconstructed or treated as build gates.
- Commit and push only when explicitly authorized. After either, update `progress/CURRENT.md` with exact commit and remote state.

## Deployment

- The current owner-directed hardware loop always creates fresh, distinct BL1
  and MCUboot P-256 roots, performs a clean MCUboot build, then publishes one
  board-bound full release from the printed build manifest:

  ```text
  bk7258.py release full --build-manifest MANIFEST \
    --bl1-key BL1_PRIVATE --mcuboot-key MCUBOOT_PRIVATE \
    --version MAJOR.MINOR.REVISION+GENERATION \
    --base ACCEPTED_FULL.bin --base-sha256 SHA256 \
    --openssl OPENSSL --output-dir RELEASE_DIR
  ```

  The resulting operator file is exactly `0x7fa000` bytes for the accepted
  Agent layout.
  It contains all nine authorized writes, restores `usr_config` and every hole
  from the accepted base, and excludes the immutable tail.
- Give BKFIL that one file in one operation; do not extract it into a
  `--mainBin-multi` list:

  ```text
  bk_loader.exe download -p 3 -b 6000000 -s 0 -i RELEASE-full.bin \
    --uart-type OTHER --reboot 0 --fast-link 1
  ```

  Acceptance requires loader log index `[0]`, file length `0x7fa000`, one
  `EraseFlash ->pass`, one `WriteFlash ->pass`, `Writing Flash OK`, and
  `{All Finished Successfully}`.  Open COM3 at UART0 115200 only after BKFIL
  exits, then capture a fresh reset boot.
- The dense image is board-bound because it embeds retained bytes.  Reuse the
  latest accepted full image as the next base only for the same board/layout.
  If the base is uncertain or comes from a target read, read every retained
  interval twice at 115200, require byte identity and bind its SHA-256; never
  fill `usr_config` or holes with guessed erase bytes.
- Every MCUboot hardware workflow in `tools/windows-hardware-debug` must
  perform the non-halting BL1/BL2 target-fingerprint preflight before starting
  `bk_loader`. Treat
  J-Link failure or an identity with no matching permitted address as a
  package/target pairing failure; do not add a bypass.  The current hardware
  loop uses the full single-file flow above, not apps-only or sparse Flash.
  Root rotation remains a separately designed and authorized recovery
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
- The standing accepted-layout full-image authority covers byte-compatible
  `s_app`, BL1 Manifest and BL2 refreshes only after target trust identities
  match.  A layout change, trust-root change or recovery write still requires
  fresh exact-range authority; source/dry-run verification does not grant it.
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
