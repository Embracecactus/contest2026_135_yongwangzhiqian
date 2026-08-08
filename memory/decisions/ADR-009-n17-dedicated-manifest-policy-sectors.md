# ADR-009: Reserve dedicated N17 Manifest and policy sectors

- Status: Accepted
- Date: 2026-08-06
- Decision owner: Project owner
- Approval evidence: owner accepted the three coupled N17 layout, journal and
  migration decisions together

## Context

The signed CP/AP release Manifest must outlive mutable lifecycle records and
must not be embedded in the executable pair it authenticates. N15 leaves a
reserved raw-Flash span after metadata bank 1. Addresses must come from the
canonical partition CSV rather than bootloader-only constants.

## Decision

Reserve three adjacent 4 KiB sectors:

- Manifest A: `0x50b000..0x50c000`;
- Manifest B: `0x50c000..0x50d000`;
- one-way authentication policy: `0x50d000..0x50e000`.

The remaining unallocated span is `0x50e000..0x600000` (`0xf2000` bytes).
LittleFS remains physically at `0x600000..0x700000`; only its project SDK
partition ID changes from 12 to 15 because the new roles occupy IDs 12..14.
The accepted generated identity is
`bk7258-v3119-ab-32d3519eada0a7f7`, SHA-256
`32d3519eada0a7f77a284998e785fdb1daa55c691b3bfaf1a92b4097ce398203`.

The generic SDK partition wrapper must reject write/erase access to both
Manifest sectors and the policy sector. Future dedicated lifecycle code may
write an inactive slot's Manifest only under its separately verified protocol;
normal code must never write or erase the policy sector.

## Consequences

- Immutable release authorization and mutable journal reclamation have
  independent erase lifetimes.
- The partition CSV, generated header/model and signed layout identity change,
  while executable, vendor, LittleFS and calibration physical ranges do not.
- An old public Manifest vector carrying the prior layout hash is obsolete and
  has been reissued.
- This layout change alone does not implement format 3 or authorize a build,
  Flash write, board action, or security provisioning.

## Validation and remaining gate

The official v3.1.1.9 partition parser/generator accepts the project CSV. Host
partition, OTA-layout, rotation and SDK-wrapper checks pass with 16 table
entries and LittleFS role ID 15. Final firmware source/ELF checks for literal
ID 12 remain an implementation gate.

Detailed ABI and protocol: [N17 layout/journal/migration design](../../docs/bk7258-t5ai/nuttx-port/n17-layout-journal-migration.md).

## Reversal signals

- Exact official v3.1.1.9 evidence shows an undiscovered owner in the new
  sector span.
- An immutable binary consumer requires project LittleFS numeric ID 12.

