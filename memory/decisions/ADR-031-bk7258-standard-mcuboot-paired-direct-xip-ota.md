# ADR-031: Standard MCUboot trailers with a BK7258 pair gate

- Status: Accepted
- Date: 2026-08-21
- Decision owner: Project owner

## Context

BK7258 launches CP and AP as one physical pair, while upstream MCUboot models
them as image 0 and image 1 and may select or mutate each image independently.
The retired N15/N17 private selector and journal must not return as a second
update authority. Direct-XIP revert still needs standard per-image trailer
magic, `copy_done` and `image_ok` state.

## Decision

- MCUboot remains the only signature and persistent trailer authority. CP is
  image 0 and AP is image 1; there is no private OTA journal.
- BL1 selects and authenticates only Manifest/BL2 A/B. It does not hand a
  fixed application-slot choice to BL2.
- BL2 read-only preselects complete same-slot CP/AP pairs, requires matching
  version, security counter and trailer flags, and exposes exactly one
  physical pair to each upstream `boot_go()` attempt. The other complete pair
  is the fallback.
- Enable upstream `MCUBOOT_DIRECT_XIP_REVERT`. BL2 may write only the exact
  visible image's standard `copy_done` byte or erase the selected failed image;
  a partial image makes its whole pair ineligible.
- A full provisioned release uses imgtool `--confirm`. A field OTA package
  uses imgtool `--pad`, is explicitly targeted at the runtime inactive pair,
  and carries physical 32+2 CRC-encoded CP/AP members.
- CP derives the active pair from the retained Flash-remap registers. Staging
  erases inactive CP first, completes AP, writes CP except sector zero, and
  commits CP sector zero last. Health confirmation writes AP `image_ok` then
  CP `image_ok`; any reset between them leaves a trailer mismatch that BL2
  rejects in favor of the old pair.
- Flash mutation is task- and range-scoped. It cannot write BL1, Manifest,
  BL2, persistent data or the immutable calibration tail.
- `bkota` is a trusted maintenance/operator seam, not a device-side package
  parser or an untrusted field transport. Its stage inputs must come from a
  separately verified `--ota-apps` package; its confirm action means the
  operator accepts externally observed CP/AP health. Product transport,
  package ingestion and automatic health gates remain higher-layer owners.

## Consequences

- Positive: signatures, pending/confirmed state and revert semantics remain
  standard MCUboot concepts; the board adapter owns only CP/AP pair atomicity.
- Positive: every interrupted stage or confirmation has a deterministic
  fallback without a second persistent journal.
- Negative: imgtool output must reserve a board-specific 8 KiB logical tail so
  a 4 KiB raw-sector CRC RMW never contains signed executable content.
- Negative: boot-stage mutation currently fails closed for the T5Board-proven
  C86517 Flash command set; other official JEDEC variants need separate real
  hardware qualification before enablement.

## Evidence and validation

- [T5Board paired OTA adaptation checkpoint](../../progress/verification/2026-08-21-bk7258-t5-board-paired-ota-adaptation.md)
- Pinned MCUboot direct-XIP-revert source in the manifest workspace.
- Official Beken v3.1.1.9 Flash status/protection table and command path.

## Reversal signals

- The pinned MCUboot trailer offsets or direct-XIP-revert mutation order change.
- Real power-interruption or Flash-status evidence contradicts the fallback
  reasoning.
- A supported BK7258 board cannot provide one recoverable old CP/AP pair while
  staging or confirming the new pair.
