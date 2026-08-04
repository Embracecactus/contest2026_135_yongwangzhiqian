# Project Memory Index

Last reviewed: 2026-08-04

Read `../progress/CURRENT.md` after this file, then load only the documents relevant to the active task.

## Durable context

- [Project](PROJECT.md): purpose, users, scope, and success criteria.
- [Architecture](ARCHITECTURE.md): system boundaries, components, data flows, and constraints.
- [Rules](RULES.md): accepted domain rules, invariants, security, and UX conventions.
- [Operations](OPERATIONS.md): environments, verification, deployment, rollback, and recovery.
- [Decisions](decisions/): accepted architecture and product decisions with consequences.

## Accepted decisions

- [ADR-001](decisions/ADR-001-wrapper-only-official-source-boundary.md): official NuttX/apps/SDK remain read-only; permanent adaptations use repository-owned wrappers.
- [ADR-002](decisions/ADR-002-n14-psram-ownership-and-layout.md): CP owns PSRAM hardware; lower 8 MiB ABI is retained and the upper 8 MiB remains reserved.
- [ADR-004](decisions/ADR-004-n15-official-contiguous-ab-layout.md): migrate once to the official-style contiguous CP/AP A/B layout, relocate/clear LittleFS, and preserve the calibration tail.
- [ADR-005](decisions/ADR-005-n15-boot-selector-metadata-v1.md): freeze the append-only metadata v1 ABI, fail-closed A/B validation, and the boundary between a pending candidate and one-trial permission.
- [ADR-006](decisions/ADR-006-n15-symmetric-dual-bank-ota.md): use two metadata banks and slot-neutral states for safe inactive-slot A/B rotation; board validation remains a separate gate.

## Superseded decisions

- [ADR-003](decisions/ADR-003-n15-paired-sector-swap.md): the journaled physical-sector-swap proposal was never accepted; ADR-004 replaced it before any board write.

## Current verified baseline

- Read [Current Progress](../progress/CURRENT.md) for the branch, publication/merge state, next action, and rollback point.
- N14 completion is archived in [the N14 milestone](../progress/milestones/2026-08-03-n14-psram-board-verified.md).
- Detailed N14 test output and hashes are canonical in [the N14 evidence index](../docs/bk7258-t5ai/nuttx-port/n14-evidence-index.md).
- N15 is active. N15-M migrated the board to the ADR-004 contiguous layout and
  re-verified the N14 runtime matrix. Read the
  [N15-M verification record](../progress/verification/2026-08-03-n15-migration-board-verification.md)
  and [Current Progress](../progress/CURRENT.md) before any Flash action.
- R1/R2 sector-swap material is historical rejected-option evidence only.
  [N15-A](../progress/verification/2026-08-03-n15-a-host-pair-bundle.md)
  is host-verified and
  [N15-B](../progress/verification/2026-08-04-n15-b-host-staging.md),
  [N15-C](../progress/verification/2026-08-04-n15-c-host-boot-selection.md),
  [N15-D](../progress/verification/2026-08-04-n15-d-host-trial.md),
  [N15-E](../progress/verification/2026-08-04-n15-e-host-publication.md)
  and [N15-F](../progress/verification/2026-08-04-n15-f-host-validation.md)
  are host/source/ELF-verified. The
  [historical N15-V fault matrix](../progress/verification/2026-08-04-n15-v-host-fault-injection.md)
  is additionally host/source/ELF/dry-run evidence for the original 15-case
  format-2 workflow. The
  [symmetric host closure](../progress/verification/2026-08-04-n15-format2-symmetric-host.md)
  adds independently checked A-to-B-to-A packaging with 16 unique identities.
  Current work is the N15-V
  board gate under fresh authority. The
  deployed B seed remains unselectable; the gates-on validation artifact has
  not been flashed.

## Memory rules

- Record verified, durable facts; label assumptions and unknowns.
- Link to canonical sources instead of duplicating details.
- Keep secrets and personal or production data out of this directory.
- When a fact becomes obsolete, update it and preserve material history in a decision or milestone.
- Legacy `memory/*.md` files outside this managed index remain local compatibility notes and are not authoritative unless explicitly linked here.
