# ADR-011: Arm N17 with a fail-closed format-2 migration

- Status: Accepted architecture; migration implementation and board run pending
- Date: 2026-08-06
- Decision owner: Project owner
- Approval evidence: owner accepted the three coupled N17 layout, journal and
  migration decisions together

## Context

A dual-format bootloader is needed during migration, but erasing both
format-3 banks must never restore unsigned format-2 or header-only slot-A
fallback after authentication has been enabled. Arming must occur only after
two independent signed recovery paths exist.

## Decision

Install the future dual-format Tier-1 bootloader through a controlled wired
prerequisite. While the policy sector is erased, format 3 is preferred but a
valid format-2 baseline remains eligible if no format-3 bank exists.

Migration must seed both executable slots with the same verified signed CP/AP
pair and both dedicated sectors with the same slot-neutral signed Manifest.
It then creates verified format-3 baselines in the two metadata banks at old
generation plus one and plus two. Only after either bank can independently
select an authenticated slot may it program the canonical 32-byte `BKOTA17A`
marker in the policy sector.

Boot classifies the entire sector: all `0xff` is unarmed; any other byte is
armed. A canonical marker is healthy armed state and any other non-erased
content is degraded armed state. Both armed states allow only format 3 plus a
valid signed Manifest and payload. They forbid format 2 and header-only A
fallback. The marker is programmed last and normal firmware has no erase/write
path to it.

## Consequences

- A torn arming write cannot lower policy.
- Power loss before arming retains a durable format-2 or verified format-3
  baseline; power loss during/after arming relies on the already verified two
  format-3 paths.
- The software marker does not resist external raw-Flash erasure or bootloader
  replacement. Hardware-root enforcement remains the separate N17-H gate.
- The current development board is not armed. This ADR grants no firmware
  build, Flash mutation, board action, OTP/eFuse operation or JTAG change.

## Validation and remaining gate

The host model now covers every row of the accepted migration reset/power-loss
matrix, including torn first/second baselines, torn marker, canonical marker
and armed loss of both format-3 banks. Only the wholly erased policy state can
select format 2. A separately reviewed, range-specific board plan and fresh
owner authorization remain mandatory before any physical migration.

Detailed ordering and marker bytes: [N17 layout/journal/migration design](../../docs/bk7258-t5ai/nuttx-port/n17-layout-journal-migration.md).

## Reversal signals

- Migration cannot establish two independently boot-eligible signed baselines
  before the marker write.
- Product recovery requirements mandate booting a lower signed counter after
  confirmation; that requires an explicit anti-rollback policy redesign.
