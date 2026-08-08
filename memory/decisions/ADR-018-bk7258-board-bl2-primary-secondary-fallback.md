# ADR-018: Board-owned primary/secondary BL2 fallback

## Status

Accepted for the current MCUboot reverse-engineering track. This is a
repository-owned recovery contract, not a claim about the unpublished BK7258
BootROM Secure Boot ABI.

## Decision

Keep the official v3.1.1.9 CSV `bl2` row as the primary BL2 envelope:

- raw flash `0x51d000`, logical XIP `0x024d0000`;
- 128 KiB logical capacity and 136 KiB 32+2-CRC physical capacity.

Use the immediately following reserved span as a same-sized secondary BL2:

- raw flash `0x53f000`, logical XIP `0x024f0000`;
- end `0x561000`, before the existing LittleFS boundary.

Store two board-owned candidate Manifests in the reserved boot tail, with the
secondary record at `0x0200fe00` and the primary record at `0x0200ff00`.
BL1 verifies the slot-specific Manifest, validates the copied vector table,
and tries primary then secondary. If both candidates fail, it remains in the
existing watchdog-backed fail-closed loop.

## Evidence and boundary

The exact package passed host checks and the board matrix. Normal boot reached
`B1PRIMARY -> BL2RAM -> B2HANDOFF -> NuttShell`. After a CRC-valid mutation of
the primary BL2 contents, BL1 reported Manifest error `0x00000002`, entered
`B1SECONDARY`, and reached NuttShell. The known-good primary was then restored
and a COM7 RTS reset regression passed.

This fallback is deliberately board-owned and recoverable. It does not write
OTP/eFuse, enable a secure lifecycle, or assert that the self-owned Manifest
matches the missing vendor BootROM format. The official CSV and SDK sources
remain read-only.

## Consequences

- A corrupted primary BL2 can be recovered without changing the official CSV
  partition row or depending on a second BootROM slot selector.
- The secondary span consumes reserved flash before LittleFS and must remain
  in every package/download layout.
- The two BL2 copies are currently built from the same image; independent
  update policy and CP/AP same-slot atomicity remain later work.
