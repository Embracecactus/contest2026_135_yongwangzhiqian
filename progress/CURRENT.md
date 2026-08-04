# Current Progress

Last updated: 2026-08-04T19:56:05+08:00
Updated by: Codex (`maintain-project-memory` checkpoint)

## Snapshot

- Branch: `feat/bk7258-n15-ota`; published HEAD before this uncommitted
  checkpoint: `68b943615838eea6a79b25431183c154fee73727`.
- Sole active SDK: official Beken v3.1.1.9. Its CP/AP wrapper bundles passed
  their checksum manifests; older SDKs remain preserved and unused.
- N15 symmetric OTA is physically verified for one complete A-to-B-to-A
  lifecycle, including both metadata banks, both inactive-pair Flash ranges,
  trial boots, service regressions, confirmation, COM7 RTS recovery and one
  post-confirm removal of both USB and J-Link power.
- The last direct OTA metadata read, after complete power removal, was
  generation 315, bank 1, confirmed/active A, secondary 0 and runtime gates 0.
- The board and shared host build tree now both use normal
  `cp_nsh_psram + ap_smp_psram`. A bounded sparse restore wrote only Boot, CP A
  and AP A; the ranges exclude B, both metadata banks, `usr_config`, LittleFS,
  reserved space and the calibration tail. Normal firmware has every OTA gate
  0 and no `bkota` command.
- Post-restore AP, logical CPU2, RPTUN, the existing LittleFS probe and PSRAM
  info all passed. Metadata generation 315 is preserved by the non-overlapping
  write-set contract; normal firmware intentionally cannot query it by CLI.
- The final durability gate passed in a capture-only COM11 session after the
  owner removed both USB and J-Link power. The same generation-315 record and
  all AP/CPU2/RPTUN health predicates matched; no reset, J-Link Commander or
  Flash command ran before the acceptance read.

## Implemented and verified

- ADR-004 contiguous CP/AP A/B layout generated from the project-owned CSV;
  `usr_config`, relocated LittleFS and the official calibration tail remain
  outside normal sparse update writes.
- ADR-006 slot-neutral format-2 rotation with two metadata banks, inactive-pair
  staging, fail-closed publication, one-trial selection, confirmation and
  rollback.
- Separate dry-run-first validation profile with generation-bound operator
  tokens, initially closed CP runtime gates, bounded upper-PSRAM transfer and
  deterministic fault hooks.
- Physical generation 314 A-to-B:
  full candidate read-back/SHA pass, bank-0 publication, trial B, retained N14
  service matrix, and confirmed B.
- Physical generation 315 B-to-A:
  full candidate read-back/SHA pass, bank-1 publication, trial A, retained N14
  service matrix, confirmed A, and confirmed-A RTS recovery.
- Physical defects closed in project-owned code:
  both Tier-1 watchdogs are fed; J-Link uses independently verified 64 KiB
  no-reset chunks; publication timeout is 180 seconds; the validation NSH
  argument limit is 10. Official NuttX/apps/SDK sources were not changed.
- Full validation and normal builds pass with official v3.1.1.9. Current
  normal ELF SHA-256 values:
  - Boot: `04e193c0db43f8c8ee5d361f3e91c8036aa5d5f4f78871eff2bd611e2d43a793`;
  - CP: `76f17d1a68f5ffb3b89c249c2a8a2a16232c3c9399cb80cd0c6fc78cbbc4c272`;
  - AP: `6b8e102870e82d971a028cc560f18e67fc9a10d429fd33a555485fbb9086e5cc`.
- The normal profile is deployed on the board. Its encoded sparse segment
  SHA-256 values are Boot `3042fb32...173fb`, CP `ac40ea2e...5dc57`, and AP
  `da640e33...8573e`; all three loader erase/write operations passed.

Primary evidence:

- [N15 physical symmetric lifecycle](verification/2026-08-04-n15-physical-symmetric-lifecycle.md)
- [N15 format-2 symmetric host closure](verification/2026-08-04-n15-format2-symmetric-host.md)
- [N15-M board migration](verification/2026-08-03-n15-migration-board-verification.md)

Onboarding:

- [BK7258/T5-AI beginner porting guide](../docs/bk7258-t5ai/beginner-porting-guide/README.md)

## Next actions

1. Review the bounded N15 changes/evidence, then commit and push only the
   intended project files.
2. Select the next MAIN Stage separately; legacy SDK validation remains
   deferred until explicitly requested.

## Open boundaries

- RTS reset and complete VDD removal remain separately labelled evidence; both
  now pass for the generation-315 confirmed-A state.
- Hashes, CRC and read-back prove integrity; publisher authentication, key
  provisioning and anti-rollback are not implemented.
- No chip erase occurred. The authorized normal restore did not write
  `usr_config`, LittleFS, B, either metadata bank, reserved spans or the
  calibration tail.
