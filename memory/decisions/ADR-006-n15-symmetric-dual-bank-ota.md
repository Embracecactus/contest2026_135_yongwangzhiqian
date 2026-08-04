# ADR-006: Add symmetric inactive-slot OTA with dual metadata banks

- Status: Accepted; host/source/ELF and approved minimal board scope verified
- Date: 2026-08-04
- Decision owner: Project owner

## Context

ADR-005 proves a bounded A-to-B lifecycle, but metadata format 1 makes
`CONFIRMED_B` terminal.  That is not a complete field-update design: a later
release cannot be staged into inactive A while B is running.

Reclaiming the only metadata sector is unsafe when B is the stable slot.  If
power is lost after that sector is erased, the bootloader loses the durable
fact that B was confirmed and falls back to A.  A may already contain the new
candidate, so this can bypass the intended one-trial policy.

The exact official v3.1.1.9 `app_ab` partition table ends its assigned
`ota_fina_executive` and `usr_config` regions at raw offset `0x50a000`.
ADR-004 leaves `0x50a000..0x600000` unallocated.  Official NuttX, apps, SDK
source and SDK libraries remain immutable.

## Decision

Implement a repository-owned metadata format 2 and alternate two physical
metadata banks:

| Bank | Raw range | Origin |
|---|---:|---|
| bank 0 | `0x4fb000..0x4fc000` | official `ota_fina_executive` envelope |
| bank 1 | `0x50a000..0x50b000` | first 4 KiB of ADR-004 unallocated space |

The vendor `usr_config` range `0x4fc000..0x50a000`, LittleFS and calibration
tail remain untouched.  The remaining first reserved span becomes
`0x50b000..0x600000`.

Each 512-byte format-2 record keeps the existing compact layout.  Its
semantics become slot-neutral:

- the state encodes both lifecycle phase and candidate slot;
- the existing primary lengths and SHA-256 fields describe the stable base
  slot;
- the embedded 384-byte descriptor describes the candidate slot;
- generation, versions, identity, sequence and CRC remain mandatory.

State values preserve the format-1 B-family numeric values and add an
A-family:

```text
PENDING_B(1) -> TRIAL_B(2) -> CONFIRMED_B(3) | ROLLBACK_A(4)
PENDING_A(5) -> TRIAL_A(6) -> CONFIRMED_A(7) | ROLLBACK_B(8)
```

For a new generation, stage and completely verify the inactive executable
slot first.  Then erase/write/read-back only the metadata bank that does not
contain the current durable lifecycle.  The previous bank remains valid
until the new `PENDING_*` record is fully committed.  Boot selects the valid
bank with the greatest generation; equal-generation identities in different
banks are ambiguous and fail closed.

An erased or torn newer bank never displaces an older trusted bank.  A
persisted `TRIAL_*` boots its base slot after reset.  `CONFIRMED_*` boots its
candidate slot, and `ROLLBACK_*` boots its base slot.  Slot A means remap
disabled; slot B means the exact official one-offset remap enabled.

The production-facing OTA adapter will be transport-neutral and
repository-owned.  It will accept a descriptor/pending record plus bounded
candidate chunks, write only the inactive raw slot, verify the complete pair,
publish metadata last, and expose activate/status/confirm/rollback operations.
Validation-only J-Link/PSRAM commands and fault injection stay outside that
normal API.

## Safety consequences

- No metadata garbage collection can erase the only durable active-slot
  record.
- A reset during download or inactive-slot staging keeps the current bank and
  current slot authoritative.
- A reset during new-bank erase/program uses the older trusted bank.
- A complete new pending record is still only one-trial permission; it is not
  confirmation.
- Both banks corrupt or an untrusted equal-generation conflict still fail to
  the bounded A recovery baseline; this is not a publisher-authentication
  claim.

## Compatibility and rollout

- Format 1 remains the historical N15-A-to-B evidence format. The board moved
  directly from the erased migration baseline to format 2; no live format-1
  conversion was required.
- The format-2 core, Boot/CP adapters, packers, host fault matrices and final
  validation ELFs pass. Under exact owner authority, generation 314 used bank
  0 for A-to-confirmed-B and generation 315 used bank 1 for
  B-to-confirmed-A; both inactive executable pairs passed full read-back/SHA.
- The confirmed-A state survived RTS and complete removal of both USB and
  J-Link power. The board was then restored with a bounded normal gates-zero
  sparse image that excludes both banks and slot B.
- The ordered format-2 campaign now contains 16 identities: generations
  42..56 retain the interruption/A-to-B coverage and generation 57 stages the
  inactive A pair from confirmed B, confirms A and proves the symmetric
  lifecycle. Host qualification used an isolated 300..315 campaign so no
  board generation was consumed.

## Acceptance gates

1. **PASS (host):** portable dual-bank parser/selector/transition tests cover A-to-B, B-to-A,
   repeated generations, torn inactive-bank writes and ambiguous identities.
2. **PASS (host/source):** inactive-slot staging rejects the currently mapped executable pair at both
   the wrapper and Flash-permission layers.
3. **PASS (source/ELF):** Boot and CP use the same bank-selection and state-transition core.
4. **PASS (host/source/ELF):** a transport-neutral normal wrapper passes tests without a
   validation command or fault-injection symbol.
5. **PASS (board):** one minimal A-to-B and one B-to-A lifecycle, retained
   services, RTS recovery and post-confirm complete-power-removal recovery pass
   on hardware.

The 16-package host campaign and independent verifier both pass. This accepts
the architecture and implementation baseline; the approved minimal N15 board
scope is `board-verified`. It does not authorize future Flash writes or claim
physical rollback/analog mid-pulse brownout.

## Reversal signals

- Exact v3.1.1.9 source or board evidence shows `0x50a000..0x50b000` is used
  by a vendor component despite its absence from the official partition map.
- Raw writes to inactive A while B is mapped cannot be isolated from active
  instruction fetches through the repository-owned Flash guard.
- Boot SRAM or boot-slot size cannot accommodate the sequential two-bank
  selection path without weakening existing bounds.
