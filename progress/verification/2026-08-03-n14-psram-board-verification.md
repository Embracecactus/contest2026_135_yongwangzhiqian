# Verification: N14 PSRAM and SDK timer wrapper

- Date and time zone: 2026-08-03T18:16:17+08:00
- Verifier: Codex plus physical T5-AI automation
- Commit or artifact: feature commit `36fc6a282efe787dff18630bdc77b245cb5d2514`; factory SHA-256 `3b34edc5d86343dcb0a3f479d71eb1271c49157eb93c51b5bb6da13fafcef253`
- Environment: T5-AI, `cp_nsh_psram + ap_smp_psram`, Beken SDK v3.1.1.9

## Scope

Verify N14 source/ABI ownership, paired build, PSRAM capacity/layout/allocator,
SDK timer lifecycle, AP warm/cold lifecycle, factory first-calibration path,
and regressions for existing RPMsg/Bluetooth services.

## Commands or methods

- Final clean paired dual build with `BK7258_SDK_SOURCE` source verification.
- `verify_bk7258_psram.py` against final CP/AP ELF and map files.
- `setup_bk7258_sdk.sh --check --version v3.1.1.9 --role cp|ap`.
- `bkpsramtest info`, `bkpsramtest all 256`, `bktimertest 256`.
- `apctl cycle 10 60000`, generation-12 restart and PSRAM recheck.
- `bkrpmsgtest all 100 60000`, `bkbttest info 10000`.
- Three independent physical resets, final clean cold, factory first-calibration, and post-calibration cold.
- Official NuttX/apps tracked-diff checks, `git diff --check`, Python syntax, and local documentation-link validation.

## Results

- Source/ELF verifier PASS: CP-only owner, v3.1.1.9 bundle, lower-8-MiB ABI, upper-8 reserved, AP CPU0+CPU1 gate.
- SDK CP/AP bundle checksum checks PASS; official NuttX/apps tracked diffs are zero.
- Board detected `id=0x8d08`, `config=0x8d1a`, capacity 16 MiB; destructive raw gate `1/1`.
- AP allocator completed `16/16` on CPU0 and `16/16` on CPU1, errors `0/0`, free `655344→655344`.
- CP heap completed 256 iterations with free `131056→131056`.
- Timer completed 256 callbacks and the 20 ms queued self-delete case.
- AP cycle 10, generation-12 restart, RPMsg six-scenario matrix, and Bluetooth info passed.
- Physical/factory gates reached `PASS_NSH`; final AP/RPTUN/supervisor/SMP/PSRAM health passed.
- 216 local Markdown links and source/document whitespace checks passed; raw Windows capture logs retain their original CRLF bytes.

## Failures and investigation

Historical N14 candidates exposed three resolved faults: CPU1 release without
the official PM vote, allocator control metadata in PSRAM, and AP dual-CPU
contention in the private-heap/realloc path. The final image uses the official
CPU1 vote, SRAM-resident controls plus an outer spinlock, and bounded
allocate-copy-free realloc. No unresolved functional failure remains in N14.

## Residual risks

- Upper 8 MiB runtime ownership and cache/DMA semantics are undefined by design.
- Physical power-cut, temperature/voltage memory stress, and product performance SLA were not tested.
- The implementation was unpushed when verification completed and was subsequently published to `fork/feat/bk7258-n14-psram`; see the current checkpoint for merge state.

## Evidence locations

- [N14 evidence index](../../docs/platforms/bk7258/nuttx-port/n14-evidence-index.md).
- Raw logs: `logs/bk7258-n14/`.
- [N14 source verification](../../docs/platforms/bk7258/nuttx-port/n14-psram-source-verification.md).
