# N17 format-3 portable host-model verification

- Date: 2026-08-06
- Result: PASS (host-only architecture implementation gate)
- Hardware/firmware mutation: none
- SDK/NuttX/apps source changes: none

## Scope

This gate turns the accepted 256-byte format-3 journal, dual-bank selection,
counter-floor, phase-specific boot and one-way policy rules into an executable
standard-library Python model. It does not implement the Tier-1 Bootloader or
signature verification and performs no firmware or board write.

Canonical sources:

- [`bk7258_n17_journal.py`](../../board/bk7258/scripts/bk7258_n17_journal.py)
- [`verify_bk7258_n17_journal.py`](../../board/bk7258/scripts/verify_bk7258_n17_journal.py)
- [Accepted N17 layout/journal/migration design](../../docs/bk7258-t5ai/nuttx-port/n17-layout-journal-migration.md)

## Command and result

```sh
python3 board/bk7258/scripts/verify_bk7258_n17_journal.py
```

The verifier passed:

- 13 canonical record round trips;
- 20 CRC-repaired structural field negatives;
- all 2,048 possible single-bit changes in one complete record;
- all 256 byte-level torn-record cuts;
- all 256 byte-level torn-append cuts while preserving the previous record;
- 8 invalid-transition cases;
- 11 dual-bank/generation/lineage selection cases;
- 11 counter and generation-wrap cases;
- 8 phase-specific signed-slot boot decisions;
- 35 policy-sector state cases and 6 format-selection cases;
- all 8 accepted format-2-to-format-3 migration reset rows.

The canonical marker remains:

```text
424b4f5441313741010020000000000032d3519eada0a7f77a2849981a6fa31b
```

## Verified conclusion

A torn or non-canonical append leaves the longest valid record prefix
authoritative. An invalid higher-generation bank cannot displace a stable
older bank or lower its accepted counter floor. Equal generations remain
ambiguous and fail closed. Only an entirely erased policy sector permits a
format-2 fallback; any programmed policy bit requires format 3.

## Boundaries

The `SlotEvidence` inputs represent the result of future Manifest signature
and payload verification; this model does not replace that cryptographic C
parser. It also does not prove final Bootloader Flash/RAM/stack/watchdog
budgets, Flash-driver behavior, physical power-cut behavior, migration on the
development board, OTP/eFuse state or hardware-backed anti-rollback.
