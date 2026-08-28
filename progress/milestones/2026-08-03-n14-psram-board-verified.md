# Milestone: N14 PSRAM and SDK timer wrapper board-verified

- Status: Completed
- Period: 2026-08-03
- Final commit or artifact: feature commit `36fc6a282efe787dff18630bdc77b245cb5d2514`; factory artifact SHA-256 `3b34edc5d86343dcb0a3f479d71eb1271c49157eb93c51b5bb6da13fafcef253`
- Deployment: physical T5-AI; final sparse, cold, factory first-calibration, and post-calibration cold gates passed

## Outcome

N14 established a board-verified 16 MiB PSRAM hardware gate, disjoint CP/AP
NuttX heaps, AP dual-CPU-safe allocation, and task-context SDK software timers
without changing official NuttX/apps/SDK source or static libraries.

## Delivered scope

- CP official PSRAM PM ownership, device identification, anti-alias check, and one-shot full-capacity boot test.
- Official lower-8-MiB ABI: CP 128 KiB heap, AP 640 KiB heap, AP 256 KiB reserved section; upper 8 MiB boot-tested/reserved.
- Internal-SRAM heap controls and allocator spinlock; bounded allocate-copy-free realloc.
- Per-core non-cacheable MPU setup and AP CPU0/CPU1 concurrent startup gate.
- Deferred `bk-sdk-timer` callback service and queued self-delete final-free fix.
- Paired N14 profiles, source/ELF verifier, NSH diagnostics, documentation, and raw evidence index.

## Decisions

- [Wrapper-only official-source boundary](../../memory/decisions/ADR-001-wrapper-only-official-source-boundary.md).
- [PSRAM ownership and layout](../../memory/decisions/ADR-002-n14-psram-ownership-and-layout.md).

## Verification

See [N14 verification record](../verification/2026-08-03-n14-psram-board-verification.md) and
[canonical evidence index](../../docs/platforms/bk7258/nuttx-port/n14-evidence-index.md).

## Operational state and rollback

The board last passed the post-calibration physical cold gate with N14. N13
`cp_nsh_ble_gatt + ap_smp_ble_gatt` and commit
`c6afd6f9b73dcf862f17bd31f5b2dc90820b9bb0` are the no-PSRAM rollback baseline.
Normal updates should use sparse flashing; factory flashing erases data/calibration.

## Remaining work

- Complete owner PR review/merge, then record the merge commit in the current checkpoint.
- Select no next MAIN Stage by inference; owner discussion is required.
- Upper-8 runtime allocation, cache/DMA, power-cut, and product stress remain outside N14.
