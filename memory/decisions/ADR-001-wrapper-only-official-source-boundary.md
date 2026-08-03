# ADR-001: Keep official NuttX/apps/SDK read-only and integrate through wrappers

- Status: Accepted
- Date: 2026-08-03
- Owners: Project owner

## Context

The BK7258 port needs vendor functionality and occasional deep debugging, but
the project owner explicitly forbids permanent NuttX source changes and expects
the official SDK wrapper approach. SDK archives are proprietary, checksum-pinned
inputs rather than team deliverables.

## Drivers

- Preserve upstream/official updateability and make contest ownership clear.
- Keep SDK source and archive provenance auditable.
- Allow temporary diagnosis without shipping hidden official-tree patches.
- Make all permanent behavior reviewable in the contest repository.

## Options considered

1. Patch official NuttX or SDK source directly.
2. Rebuild or replace vendor static libraries.
3. Keep official inputs read-only and implement minimal board/app/build wrappers.

## Decision

Use option 3. Permanent adaptations live in the contest repository. Temporary
official-tree debug edits are allowed only during diagnosis and must be removed
before a build/board-verified checkpoint. Any necessary upstream fix requires
separate explicit authority and its own upstream PR workflow.

## Consequences

- Positive: official checkouts remain clean; upgrades and rollback are measurable.
- Positive: wrapper ABI, symbol ownership, and archive version can be verified post-link.
- Negative: some compatibility behavior must be guarded against upstream source changes.
- Negative: wrapper code may be more verbose than a direct patch.

## Evidence and validation

- N14 source/ELF verifier and both v3.1.1.9 bundle checksum checks passed.
- Official NuttX/apps tracked diffs were zero at the N14 checkpoint.
- Canonical evidence: [N14 source verification](../../docs/bk7258-t5ai/nuttx-port/n14-psram-source-verification.md).

## Reversal signals

- The project owner explicitly authorizes an upstream contribution and the fix is accepted in the official baseline.
- A vendor ABI cannot be safely wrapped and the owner approves a new integration boundary.

## Open questions

- None for the current N14 baseline.
