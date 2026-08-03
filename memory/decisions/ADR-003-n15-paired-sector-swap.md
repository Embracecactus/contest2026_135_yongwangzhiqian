# ADR-003: Preserve the active layout with a paired physical-sector swap

- Status: Superseded by ADR-004
- Date: 2026-08-03
- Owners: Project owner

This proposal was never accepted or enabled on hardware. On 2026-08-03 the
owner approved a one-time full layout migration and allowed the existing
LittleFS contents to be discarded. That reversal signal selected the
official-style contiguous A/B layout recorded in
[ADR-004](ADR-004-n15-official-contiguous-ab-layout.md).

## Context

N15 is approved as the next MAIN Stage for paired CP/AP OTA and failover. The
current physical layout keeps LittleFS between CP A and AP A. The official
Beken v3.1.1.9 AB bootloader instead maps contiguous primary CP/AP images to an
equal-sized contiguous `s_app` through one Flash-controller offset.

The existing layout and calibration tail must remain recoverable. Permanent
changes may only use team-owned code and wrappers, with official NuttX/apps/SDK
and SDK archives left unchanged.

R2 found that the original team packer mixed address domains: it treated the
logical data offset `0x100000` as CRC-expanded physical `0x110000`, while the
v3.1.1.9 `bk_flash_*` APIs and the board MTD use raw physical `0x100000`.
Consequently, the old declared CP envelope overlapped the actual LittleFS
range by 64 KiB. The existing images are much smaller than that boundary, but
the OTA design must use the corrected safe envelope.

## Drivers

- Treat CP and AP as one generation and never boot a mixed pair.
- Preserve LittleFS and `easyflash/easyflash_ap/sys_rf/sys_net`.
- Retain current CP/AP XIP addresses and N14 rollback profiles.
- Resume safely after a reset at any committed swap phase.
- Use only the pinned official v3.1.1.9 baseline during N15.

## Options considered

1. Repartition CP/AP contiguously and use the official single-offset AB remap.
2. Keep the current active layout and swap the CP/AP physical sectors as one journaled transaction.
3. Copy staging over A without retaining an old pair or automatic rollback.

## Proposed decision

Use option 2. The R2 technical evidence is complete, but this decision remains
conditional on explicit owner acceptance. The corrected active
CP slot is `0x011000..0x100000` (`0xef000` physical bytes), whose largest
complete CRC blob is `0xeeff0` and largest raw logical image is `0xe0f00`.
Reserve bounded-safe CP/AP staging at `0x440000..0x74f000`, then four journal
copies and one 4 KiB scratch sector:

- forward copy 0: `0x74f000..0x762000`;
- forward copy 1: `0x762000..0x775000`;
- reverse copy 0: `0x775000..0x788000`;
- reverse copy 1: `0x788000..0x79b000`;
- scratch: `0x79b000..0x79c000`;
- unallocated reserve: `0x79c000..0x7fa000` (`0x5e000`).

Each journal copy is `0x13000` (19 sectors). The pair has 783 sectors and
three phases per sector, so one direction needs 2349 unique 32-byte marker
slots plus `0x200` bytes of immutable header/control data (`0x127a0` before
erase-sector alignment). The 32-byte slot follows the v3.1.1.9 SDK write-chunk
behavior; it is not a claim about the BK7258 integrated Flash's physical
program unit. This does not rely on repeated partial writes within one SDK
chunk. Swap only the CP and AP physical sector sets; LittleFS,
the `0x200000..0x220000` CRC-address gap, and the official tail never enter the
transaction. A new pair receives one trial boot and is made permanent only
after the CP/AP health gate confirms it. An unconfirmed reset swaps the old
pair back.

The exact v1 ABI uses a `0x100` immutable header and one `0x20` record per
control/phase marker. The C and Python definitions agree, and their corruption
and torn-prefix tests pass. The directional/mirrored journal host model uses
those exact records and passes 32,915 reset/torn-write
cases. A phase is committed only when both mirror markers are exact; a trial
is consumed when either trial-start marker is exact, while confirmation
requires both exact markers. Generation is uint64 monotonic and wrap is
rejected.

The team Tier-1 also links a source-verified raw-controller closure at SRAM VMA
`0x28000000`: it is `0x680` bytes, has no external call or XIP literal, and
uses at most 176 bytes of static entry stack. It is unreachable from the
normal boot path and its source and linked mutation gates are both zero.

This ADR was never accepted. No production Flash write may depend on this
layout, journal ABI, scratch sector, or SRAM swap closure. The write gate
remains zero and the artifacts are retained only as R2 research evidence.

## Consequences

- Positive: preserves current executable addresses, LittleFS, calibration, and rollback artifacts.
- Positive: the 8 MiB device has enough capacity for a bounded-safe old/new CP/AP pair.
- Positive: rollback is pair-atomic rather than independently selecting CP and AP.
- Negative: CP's historical logical linker window is larger than the safe
  physical envelope; post-link/packer gates are mandatory unless a team-owned
  linker override is introduced.
- Negative: boot-time swap is slower and causes more erase/program cycles than hardware remap.
- Negative: journal and recovery logic are substantially more complex than official remap.
- Negative: CRC32/FNV/SHA without a signature does not authenticate the publisher.

## Evidence completed before supersession

- Exact v3.1.1.9 provenance and layout verifier PASS.
- Corrected CP post-build/packer/Flash-SOP boundary gates PASS.
- Raw physical Flash and SRAM-execution constraints source-verified.
- Journal model passes reset injection at every state/sector phase: PASS, 32,915 cases.
- Exact metadata ABI and byte-level implementation tests: PASS.
- Team-owned SRAM Flash closure/link/stack/reachability/write-gate verification: PASS, read-only.
- Owner acceptance: not granted; the owner selected ADR-004 instead.

## Reversal signals

- The owner accepts a destructive migration to an official-style contiguous layout.
- Hardware evidence shows independent CP/AP remap windows not present in current source/binary analysis.
- A larger external staging device or signed boot architecture changes the recovery design.
