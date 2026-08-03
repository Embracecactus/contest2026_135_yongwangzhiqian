# Current Progress

Last updated: 2026-08-03T18:16:17+08:00
Updated by: Codex (`maintain-project-memory` checkpoint)

## Snapshot

- Branch: `feat/bk7258-n14-psram`
- N14 feature commit: `36fc6a282efe787dff18630bdc77b245cb5d2514` (`feat(bk7258): add board-verified PSRAM and timer wrappers`).
- Checkpoint commit: the metadata-only commit containing this file; resolve it with `git log -1 -- progress/CURRENT.md`.
- Remote state: `origin/dev-ai-contest-2026` remains at `c6afd6f9b73dcf862f17bd31f5b2dc90820b9bb0`; the N14 feature and memory checkpoint commits are local and unpushed.
- Deployed version: N14 factory artifact SHA-256 `3b34edc5d86343dcb0a3f479d71eb1271c49157eb93c51b5bb6da13fafcef253`; post-calibration physical cold boot passed.
- Environment: physical Tuya T5-AI, CP `cp_nsh_psram`, AP `ap_smp_psram`, Beken SDK bundle v3.1.1.9.

## Active work

- Objective: preserve the completed N14 checkpoint and await explicit push or next-stage authorization.
- Scope: team-owned board/app/build wrappers, N14 configs/verifier, documentation, logs, and project-memory checkpoint.
- Status: N14 is `board-verified` and committed locally; no next MAIN Stage is approved.

## Recently completed

- N14 16 MiB PSRAM and SDK software-timer wrapper completed on 2026-08-03; see [milestone](milestones/2026-08-03-n14-psram-board-verified.md).
- Project-memory protocol initialized without replacing legacy local memory notes.
- N14 plan, source verification, evidence index, project README, porting report, and stage handoff now point to the N14 baseline.

## Verification

- Tests: AP PSRAM CPU0/CPU1 `16/16`; CP heap 256; timer 256; AP cycle 10; RPMsg six scenarios ×100; Bluetooth info; physical cold and factory gates passed.
- Type/static checks: N14 source/ELF verifier PASS; 216 local documentation links PASS; source and documentation whitespace checks PASS. Raw Windows capture logs retain their original CRLF bytes.
- Build: final clean paired dual build completed; PSRAM/RPTUN/BLE/dual-image gates PASS; CP/AP SDK bundle checks PASS.
- Deployment health: final sparse flash, final cold, factory first-calibration, and post-calibration cold reached `PASS_NSH`; AP/RPTUN/supervisor/SMP/PSRAM health passed.
- Evidence: [verification record](verification/2026-08-03-n14-psram-board-verification.md) and [canonical raw evidence index](../docs/bk7258-t5ai/nuttx-port/n14-evidence-index.md).

## Next actions

1. Push the two local commits only after explicit user authorization, then update this file with the remote state.
2. Discuss and approve the next MAIN Stage before implementing OTA, Wi-Fi, security, Bluetooth warm restart, or upper-8 PSRAM runtime use.
3. Keep unrelated untracked directories outside this branch unless they receive a separate scope decision.

## Risks and blockers

- The worktree contains unrelated untracked `board/bk7258_qemu/`, `board/bk7258_t5ai/tests/`, `docs/assets/`, and `qemu-bk7258/`; preserve them and do not include them by inference.
- N14 upper 8 MiB is boot-tested/reserved, not a runtime allocator.
- Factory flashing erases data/calibration regions; normal updates must remain sparse.
- Physical power-cut, cacheable/DMA PSRAM, temperature/voltage stress, and product performance SLA remain unverified.

## Relevant context

- Decisions: [wrapper-only boundary](../memory/decisions/ADR-001-wrapper-only-official-source-boundary.md), [N14 PSRAM ownership/layout](../memory/decisions/ADR-002-n14-psram-ownership-and-layout.md).
- Tasks: none active; next MAIN Stage requires owner approval.
- Verification records: [N14 verification](verification/2026-08-03-n14-psram-board-verification.md).
- Rollback point: commit `c6afd6f9b73dcf862f17bd31f5b2dc90820b9bb0` with N13 `cp_nsh_ble_gatt + ap_smp_ble_gatt` profiles.
