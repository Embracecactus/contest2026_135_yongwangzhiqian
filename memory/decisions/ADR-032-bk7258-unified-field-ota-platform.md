# ADR-032: Unified BK7258 field OTA platform

- Status: Accepted
- Date: 2026-08-21
- Decision owner: Project owner

## Context

The verified paired MCUboot boot chain owns safe CP/AP launch and rollback, but
the product still needs one field-update platform spanning Wi-Fi, BLE, TF,
fixed NAND, UART/USB, offline packages, cached resume, resources and delta.
Historical N15/N17 OTA selectors, journals and RBL adapters are not design
inputs for this work.

## Decision

- Keep BL1 plus the accepted paired MCUboot BL2 as the sole persistent boot,
  signature, counter, trial, revert and confirmation authority.
- Put policy, signed-catalog ingestion, source selection, cache/resume and
  product progress in one AP OTA Manager.  Wi-Fi, BLE, TF, NAND and UART/USB
  are mutually selectable source backends, not separate OTA state machines.
- Use one versioned RPMsg protocol for AP object reads and CP staging.  CP is
  the sole on-chip Flash writer and owns the Pair Installer.
- The Pair Installer validates the generated layout identity, exact physical
  sizes and streamed CP/AP SHA-256 values; it writes AP, then CP sectors 1..N,
  and commits prefetched CP sector zero last.
- Resource/model/config installers are separate from MCUboot and use verified
  temporary content plus atomic activation.  Delta is restored to complete
  physical images on TF/NAND before normal pair staging; active on-chip Flash
  is never patched in place.
- Retire the CP-local `bkota stage <cp> <ap>` seam.  `bkota status|confirm`
  remains only as a bounded maintenance surface until automatic health
  confirmation replaces the manual confirm action.
- The team manifest remains the SDK identity source:
  `Embracecactus/bk_avdk_smp@cb080de1655d579c7593ecf504c440997c4c137b`.
  [ADR-033](ADR-033-openvela-native-beken-layout-and-gcc10.md) owns the later
  content-locked GCC10 decision.

## Consequences

- OpenVela OTA packaging, verification, delta and UI concepts may be adapted,
  but KVDB `bootctl` and generated direct partition-write scripts do not own
  BK7258 slot state or CP/AP writes.
- The manifest-pinned SDK HTTP/HTTPS, SD destination and BLE sequence handling
  are current source references only; its single-image RBL/AB flags are not
  reused.
- Every implementation phase is accepted through the normal CP/AP/BL2/BL1
  build/package path and real-board download/runtime evidence.  No historical
  OTA compatibility layer or new `tests/` work is required by this decision.

## Reversal signals

- Product requirements split CP and AP into independently launchable ABI
  domains.
- A supported board cannot provide the AP-to-CP RPMsg path while staging.
- MCUboot or the selected Flash layout can no longer retain one recoverable
  complete pair during an interrupted update.
