# Current Progress

Last updated: 2026-08-04T15:45:33+08:00
Updated by: Codex (`maintain-project-memory` checkpoint)

## Snapshot

- Branch: `feat/bk7258-n15-ota`.
- N15 OTA implementation commit:
  `6fef975b75d7773f55a00b8deff6ad3968cf7dfb`; this documentation checkpoint
  accompanies it on `fork/feat/bk7258-n15-ota`.
- Upstream baseline: `origin/dev-ai-contest-2026` at
  `8738f07aef3756880c460f7434f8a9aa18fa40d3`.
- No board write or physical OTA campaign was performed during implementation
  or publication.
- Sole active SDK: official Beken v3.1.1.9 at
  `/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9`. Older SDKs remain
  preserved and unused.
- The workspace build tree is restored to the normal
  `cp_nsh_psram + ap_smp_psram` profile. All OTA selection/write gates are zero.
- Last known deployed board state remains the earlier N15 validation sparse
  image with erased metadata and A mapping. No format-2 campaign generation has
  been written to the board.

## Implemented

- ADR-004 contiguous CP/AP A/B Flash layout generated from the project-owned
  partition CSV and consumed through SDK wrappers. Official SDK, NuttX and apps
  source trees are not permanent integration points.
- Deterministic paired CP/AP package generation with bounds, generation,
  version, vector, CRC and SHA-256 validation.
- CP-only bounded inactive-slot staging under the shared Flash guard.
- ADR-006 format-2 symmetric OTA with two metadata banks:
  - bank 0: `0x4fb000..0x4fc000`;
  - bank 1: `0x50a000..0x50b000`;
  - `usr_config`: preserved at `0x4fc000..0x50a000`.
- Slot-neutral A-to-B and B-to-A selection, one-trial boot, health confirmation
  and rollback. Candidate data and both live pairs are verified before the
  inactive metadata bank is published.
- A separate `cp_nsh_ota + ap_smp_psram` validation profile. Boot mutation is
  compile-time gated; CP mutation starts disabled and additionally requires an
  exact generation-bound authorization token.
- Validation-only bounded PSRAM transfer and one-shot fault-injection hooks.
  The loader is dry-run-first and contains no Flash or reset command.
- Symmetric campaign packaging/verifying supports 16 ordered identities,
  including a terminal return from confirmed B to confirmed A.

## Verification checkpoint

- All format-2 core host matrices pass: rotation, selection/recovery, trial,
  publication, control, health and target fault handling.
- A one-time host-only qualification set for generations 300..315 passed all
  16 pack, independent verify and loader dry-run checks. It was not loaded onto
  hardware and will not be regenerated as routine validation.
- The final normal integration build passed with official SDK v3.1.1.9:
  - Boot ELF SHA-256:
    `b4e199d4fcb9135a307171301ccd79b3dfeb23c2f8e3da9274d98e9f94ab0531`;
  - CP ELF SHA-256:
    `2d562c02b39362c26231662531fe6b0aca1af02a6161aa01b74b75891f59d933`;
  - AP ELF SHA-256:
    `6b8e102870e82d971a028cc560f18e67fc9a10d429fd33a555485fbb9086e5cc`.
- Normal-profile facts: validation disabled, all Boot OTA gates zero, both CP
  runtime gates initially false, board-write authorization false, and no
  `bkota_main` or `bk7258_ota_fault` symbol in the produced ELF files.
- The obsolete board-validation SOP was deleted, and build/source verification
  no longer depends on prose documentation.

Primary evidence:

- [N15 format-2 symmetric host closure](verification/2026-08-04-n15-format2-symmetric-host.md)
- [N15-F validation foundation](verification/2026-08-04-n15-f-host-validation.md)
- [N15-M board migration](verification/2026-08-03-n15-migration-board-verification.md)

## Active work

N15 is host/source/ELF verified, but it is not board-verified. Under fresh,
explicit board-write authority:

## Next actions

1. Run one minimal physical A-to-B lifecycle and one B-to-A lifecycle.
2. Verify retained N14 services after confirmation in each slot.
3. Record a real complete power-removal test separately; COM7 RTS or J-Link RST
   is reset evidence, not proof of VDD removal.
4. Restore and record the normal gates-zero A state.

The exhaustive historical campaign and generic hardware-debug automation are
not part of the active OTA closure unless separately authorized.

## Risks and blockers

- Physical inactive-A writes, metadata-bank-1 mutation, remap, Flash wear and
  power-loss recovery have not yet been observed on hardware.
- Package hashes provide integrity, not publisher authentication or
  anti-rollback; key provisioning remains out of scope.
- The one-time factory migration authority is consumed. Chip erase,
  calibration-tail writes and any new board-write range remain forbidden
  without explicit approval.
