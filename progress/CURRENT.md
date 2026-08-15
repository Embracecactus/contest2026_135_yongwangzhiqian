# Current Progress

Last updated: 2026-08-15
Updated by: Codex

## Snapshot

- Canonical development baseline: main contest repository branch
  `dev-ai-contest-2026` at `860ac761b64727a6d9be06730f66a2dabb4cc9cc`.
- The merged tree is byte-for-byte identical to the reviewed publication tree
  at `28a35366e82c087cc3e16d689f89642b765373b0`; the merge rewrote commit IDs but
  did not change the final tree.
- P0-P9a are merged.  The owner accepted the new rule that later work starts
  from the latest main branch rather than a merged fork branch or auxiliary
  worktree.
- No P9b legacy cutover or AIDK hardware operation is authorized by this
  checkpoint.

## Merged foundation

- P0-P4 provide the frozen 27-profile baseline, strict composition and SDK
  metadata, role-isolated build plans, and the official
  Architecture -> Chip/SoC -> Board ownership/resource contracts.
- P5-P7 provide the opt-in validation runner, metadata-only package planning,
  and dry-run cross-platform transport with no fixed COM or `/dev` identity.
- P8 provides the schematic-only `aidk_ai_toy` Board binding with UART0,
  disabled SWD/boot hold/RTT/flow control, and all unknown BOM devices off.
- P9a maps all 27 frozen profiles through the shadow ledger.  Every row remains
  `MIGRATION_PENDING`; no parity or retirement result is fabricated.
- The obsolete local `board/bk7258_t5ai` dependency is retired.  Its unique
  SDK bundles were moved into the ignored canonical `board/bk7258/bk_idk`
  store as real directories; active tooling rejects the retired path and SDK
  bundle symlinks.
- The bounded framework check passed 11 metadata/planning checks.  AIDK still
  has no production build, signing, Flash, boot, or hardware evidence.

Evidence: [P9a verification](verification/2026-08-15-bk7258-platform-v2-p9a.md)
and [release checklist](../docs/bk7258-t5ai/release-checklist.md).

## Development workflow

- Start every new task from the main repository's latest
  `origin/dev-ai-contest-2026` and keep the canonical checkout synchronized.
- Do not continue coding on an already merged feature/fork branch or auxiliary
  worktree.  Use the fork only as PR publication transport when required.
- Public NuttX/common changes remain a separate NuttX fork/PR targeted to
  `dev-ai-contest-2026`; proprietary SDK bytes remain outside Git.

## Exact next action

Select the next driver/board task from this merged baseline.  P9b profile
cutover and AIDK download/boot verification remain separate owner-gated tasks.

## Boundaries

- Preserve the four remaining untracked local artifact groups; do not stage,
  delete, or infer release evidence from them.
- Do not inspect N17 or another historical trust domain, add SDK bytes, or
  mutate a private SDK mirror.
- Do not delete or modify the 27 frozen profiles until P9b is explicitly
  authorized and profile-specific parity evidence exists.
- Any later AI Coding log export must be selected and scrubbed before it is
  added under the existing `logs/` tree.
