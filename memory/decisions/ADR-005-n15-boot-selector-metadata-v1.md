# ADR-005: Freeze the N15 boot-selector metadata v1 contract

- Status: Accepted
- Date: 2026-08-04
- Decision owner: Project owner and implementation maintainer

## Context

ADR-004 deployed equal-length contiguous primary CP/AP and secondary
`s_app` spans so the exact Beken v3.1.1.9 one-offset remap mechanism can
select a pair atomically. N15-A defined a deterministic paired RBL bundle;
N15-B defined a bounded CP-only staging descriptor and mutation core. The
remaining boot decision needed a repository-owned metadata ABI, fail-closed
pair validation, and an explicit boundary between candidate validation and
one-trial permission.

Official NuttX, apps, SDK source and SDK static libraries remain immutable.
The deployed board must remain A-only until later hardware gates are
separately authorized.

## Decision

Use the ADR-004 metadata sector `0x4fb000..0x4fc000` as eight append-only,
512-byte little-endian records. Metadata format 1 has magic `BKOTA15C` and
stores:

- state, uint64 sequence and uint64 pair generation;
- uint32 timestamp and the actual CRC-expanded primary CP/AP lengths;
- NUL-padded candidate/base versions;
- SHA-256 of the complete primary pair, including erased slot padding;
- the canonical 384-byte N15-B descriptor for the secondary pair;
- CRC32 over record bytes 0 through 507.

The first record must be `PENDING_B`. Every later record must retain the
exact first-record identity, increment sequence by one, and follow only:

`PENDING_B -> TRIAL_STARTED -> CONFIRMED_B | ROLLBACK_A`

Dirty gaps, torn records, identity drift, sequence overflow, invalid state
transitions or non-erased trailing bytes invalidate the metadata.

Before a trusted decision, the boot selector validates the primary CP/AP
pair. With trusted metadata it verifies all encoded CRC16 packets, declared
lengths, erased padding, vectors, CP magic and whole-pair SHA-256. A pending
or confirmed B also undergoes the complete N15-B descriptor, RBL, vector,
padding and digest validation.

`PENDING_B` means only “validated candidate”; it does not permit N15-C to
remap. N15-D must append and read back `TRIAL_STARTED` before granting B for
that one current boot. A later reset that observes `TRIAL_STARTED` falls back
to A. Only `CONFIRMED_B` is a stable B selection; `ROLLBACK_A` selects A.

The clean-room remap contract is one contiguous mapping:

- begin `0x02010000`;
- end `0x02260000`;
- offset `0x02250000`;
- controller registers `0x44030058`, `0x4403005c`, `0x44030060`, and enable
  at `0x44030064`.

Selection and remap each have separate immutable compile and runtime gates.
All four remain zero through N15-C. The N15-C adapter performs raw reads only;
it has no Flash erase/program path.

## Consequences

- CP and AP remain one generation and one remap decision.
- The boot decision is bounded, deterministic, host-testable and independent
  of official source modification.
- A corrupt candidate falls back to a fully verified A when trusted metadata
  exists. A corrupt trusted primary is fatal rather than silently booted.
- An erased or untrusted metadata sector receives only bounded A header
  checks; existing hardware CRC-on-fetch remains part of the A baseline.
- Each candidate lifecycle consumes at most three records. N15-E permits
  bounded reclamation only for consumed trial/rollback or structurally invalid
  metadata under the CP Flash guard and a strictly newer generation.
- The 12 KiB selector workspace temporarily uses
  `0x2800d000..0x28010000` before CP/AP startup and is cleared before CP
  handoff.
- CRC32 and SHA-256 are integrity checks, not publisher authentication or
  anti-rollback.

## Validation

N15-C passed 5 positive and 28 negative host cases, four SHA-256 vectors,
`-Werror`, GCC `-fanalyzer`, exact v3.1.1.9 source/binary contract checks,
final boot ELF symbol/gate checks, and a full exact-v3.1.1.9
`cp_nsh_psram + ap_smp_psram` build. No board write occurred. See the
[N15-C verification record](../../progress/verification/2026-08-04-n15-c-host-boot-selection.md).

N15-D later froze the append/read-back API and current-boot permission in the
portable transition core. Its 4 positive/113 negative matrix, 48 reset
boundaries, SRAM writer and final Boot/CP ELF closure passed while every
mutation/remap gate remained closed. See the
[N15-D verification record](../../progress/verification/2026-08-04-n15-d-host-trial.md).

N15-E then froze pending publication and bounded sector reclamation. Its 5
positive/142 negative cases, 8 erase and 112 program/reset boundaries, static
analysis and final ELF builds passed without board access. N15-F froze the
target-side 5000 ms supervisor health policy with 250 ms polling and a
separate validation/transport profile. The host model's 1000 ms window is an
accelerated test fixture, not the target policy.
See [N15-E](../../progress/verification/2026-08-04-n15-e-host-publication.md)
and [N15-F](../../progress/verification/2026-08-04-n15-f-host-validation.md).

## N15-V interruption model clarification

For the N15 software-recovery claim, a power-loss boundary means: stop at a
deterministic fail-before callback, let the command return quiescent without a
later Flash mutation, then completely remove and restore board power. The host
models cover the append/program reset boundaries and prove that staging never
publishes trust before full read-back; the physical campaign samples every
erase/program/read mutation family using fixed ordinals. Randomly timed power
removal and analog brownout inside a Flash erase/program pulse require separate
electrical qualification and are not implied by N15-V.

## Open issues

- N15-V board gates must cover cold/warm reset, corrupt metadata/candidate,
  failed confirmation, explicit rollback and the format-2 deterministic
  controlled-power-cycle matrix across all mutation families.
- `CONFIRMED_B` remains deliberately non-reclaimable. Symmetric staging into
  inactive A and repeated confirmed A/B generation rotation require a later
  decision/ABI extension.
- Publisher signature/key provisioning and anti-rollback policy require a
  separate security ADR before production claims.
