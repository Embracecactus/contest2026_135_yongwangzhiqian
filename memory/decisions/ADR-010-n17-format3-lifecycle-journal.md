# ADR-010: Use a 256-byte format-3 lifecycle journal

- Status: Accepted architecture; portable host model verified, firmware
  implementation pending
- Date: 2026-08-06
- Decision owner: Project owner
- Approval evidence: owner accepted the three coupled N17 layout, journal and
  migration decisions together

## Context

N15 format 2 has no signed-Manifest digest or accepted downgrade floor and
cannot be safely reinterpreted. Signing mutable pending/trial state would also
mix publisher authorization with device-local lifecycle state.

## Decision

Reuse the existing 4 KiB metadata banks at `0x4fb000` and `0x50a000` as
append-only format-3 banks. Each bank contains sixteen 256-byte records with
magic `BKOTA17J`. A record carries phase, stable/target slots, sequence,
generation, accepted security-counter floor, both Manifest signed-region
digests and a previous-record digest.

The final 32-byte Flash program unit contains commit marker `CMT3` and CRC32
and is programmed last. The CRC detects torn mutable records; it is not an
authentication primitive. Executable authorization always comes from the
referenced signed Manifest and the actual verified CP/AP payload.

`STABLE/CONFIRMED` changes the stable slot and accepted counter floor in the
same journal commit. Pending/trial/rollback retain the stable Manifest's floor.
A target counter must be strictly greater than the current floor. Persisted
`TRIAL` grants no second target boot.

The exact byte layout, state combinations, selection rules and boot checks in
the linked design are normative.

## Consequences

- Format 3 has a distinct magic/version and no ambiguous format-2 decoding.
- A generation holds immutable Manifest identities while lifecycle transitions
  append records; inactive Manifest replacement starts a new generation.
- The selected old bank remains durable until the first record in the new bank
  is fully read back and valid.
- The software counter floor is power-loss coherent with confirmation, but it
  is not hardware-backed against complete-Flash replacement.

## Validation and remaining gate

The portable standard-library host model now proves canonical parsing, all
accepted transitions, every record bit mutation, every byte-level torn record
and append, dual-bank selection, stable-lineage preservation and counter-floor
invariants. This is executable architecture evidence, not Bootloader code.
The future C parser must reproduce these decisions before integration. No
firmware or board-write gate is open.

Detailed design: [N17 layout/journal/migration design](../../docs/platforms/bk7258/nuttx-port/n17-layout-journal-migration.md).

## Reversal signals

- The verified 32-byte Flash programming primitive cannot provide the assumed
  final-unit commit behavior.
- Bootloader Flash, SRAM, stack or watchdog budgets cannot safely scan and
  authenticate the selected records and Manifests.
