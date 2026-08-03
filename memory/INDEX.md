# Project Memory Index

Last reviewed: 2026-08-03

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
  Current work starts at N15-A; B is not selectable and runtime OTA writes are
  disabled.

## Memory rules

- Record verified, durable facts; label assumptions and unknowns.
- Link to canonical sources instead of duplicating details.
- Keep secrets and personal or production data out of this directory.
- When a fact becomes obsolete, update it and preserve material history in a decision or milestone.
- Legacy `memory/*.md` files outside this managed index remain local compatibility notes and are not authoritative unless explicitly linked here.
