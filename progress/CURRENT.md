# Current Progress

Last updated: 2026-08-03T23:08:16+08:00
Updated by: Codex (`maintain-project-memory` checkpoint)

## Snapshot

- Branch: `feat/bk7258-n15-ota`.
- Base: `origin/dev-ai-contest-2026` at
  `6de4962147e5ee180def704d219ace9ae11f6e4e`.
- N15-M implementation commit:
  `39732de94af6f715ec50536a2521ea811795c5b4`, pushed to
  `fork/feat/bk7258-n15-ota`.
- Sole active SDK: official Beken v3.1.1.9. Legacy bundles are preserved but
  unused until N15 is complete and the owner opens separate validation.
- Latest deployed board state: **N15-M contiguous-layout migration
  `board-verified`** with `cp_nsh_psram + ap_smp_psram`.
- Full N15 OTA is still `in-progress`: B is seeded but not selectable and all
  runtime OTA mutations remain disabled.

## Active work

N15 remains active after the completed N15-M migration. The immediate scope is
N15-A host-only RBL/pair-bundle work; all runtime OTA and board-write gates
remain closed.

## Recently completed

- Accepted [ADR-004](../memory/decisions/ADR-004-n15-official-contiguous-ab-layout.md)
  and retired ADR-003 before any sector-swap code reached hardware.
- Replaced the N14 flash geometry with the exact v3.1.1.9-style contiguous
  primary CP/AP plus `s_app` layout in team-owned linker, boot, MTD, packer,
  debug and verifier code. Official NuttX/apps/SDK source and SDK libraries
  remain unchanged.
- Moved AP XIP to `0x02150000` and LittleFS to raw
  `0x600000..0x700000`.
- Added a canonical layout model plus independent source/layout and byte-exact
  factory verifiers. The factory path rejects old layout IDs and uses two
  bounded loader ranges instead of one dense image.
- Per owner authorization, migrated the board once, discarded old LittleFS,
  and completed the retained N14 regression plus three physical resets.

## Verification

- Factory prefix: `0x4fc000`, SHA-256
  `4722e2a81504e5e321f67850c518b0b919b79e796214481d1e0dd01bf9cf8e4b`.
- LittleFS clear image: `0x100000`, SHA-256
  `f5fb04aa5b882706b9309e885f19477261336ef76a150c3b4d3489dfac3953ec`.
- Loader write set: `0x000000..0x4fc000` and
  `0x600000..0x700000`; no chip erase and no `usr_config`, reserved or tail
  range in the command.
- Board gates PASS: NSH; AP READY; CPU2 scheduler-online; RPTUN CONNECTED;
  supervisor HEALTHY; relocated LittleFS; PSRAM; SDK timer; RPMsg; RPMsgFS;
  Bluetooth; physical reset 3/3.
- Final host gates PASS: exact SDK CP/AP checksums; bootloader rebuild/verify;
  RBL self-test; layout/factory, RPTUN, BLE-GATT and PSRAM verifiers; two
  factory negative fixtures; Python/shell/format checks.
- Canonical evidence:
  [N15-M board verification](verification/2026-08-03-n15-migration-board-verification.md).

## Read-back rule

The pre-migration 8 MiB BKFIL read at 6 Mbps contains occasional inserted
128-byte zero blocks and is forensic evidence only, not a recovery image.
Critical Flash acceptance reads must use 115200 and two byte-identical
captures. Post-migration `usr_config` and official-tail repeats meet that
rule; the loader's range list independently proves neither was targeted.

## Next actions

Start N15-A without changing the deployed layout:

1. reproduce the exact v3.1.1.9 `s_app` RBL container and 96-byte parser;
2. define a deterministic CP/AP pair manifest with layout, generation,
   version, length and digest checks;
3. add positive and corruption/address/size/layout/version negative tests;
4. keep B writer, remap, trial metadata mutation and board writes disabled;
5. separately decide publisher signature/key provisioning and anti-rollback
   policy before any security claim.

## Risks and blockers

- A-only Tier-1 boot remains the deployed behavior. The B seed has no RBL
  header and is explicitly `boot_selectable=false`.
- N15 has not verified candidate staging, trial, confirm, rollback,
  mixed-generation rejection or power-loss recovery.
- Pre- and post-migration sparse artifacts are incompatible. Never use the
  old CP/AP offsets on the migrated board.
- Never chip erase or write `0x7fa000..0x800000`; factory and OTA commands
  must also exclude `usr_config` and unallocated ranges.
- CRC32/FNV/SHA provide integrity, not publisher authenticity or
  anti-rollback.
- Preserve unrelated untracked `board/bk7258_qemu/`,
  `board/bk7258_t5ai/tests/`, `docs/assets/`, and `qemu-bk7258/`.

## Resume pointers

- Active worklog: [N15 stage](../docs/bk7258-t5ai/nuttx-port/prompts/15-n15-tier2-ota.md).
- Task packet: [N15 task](tasks/2026-08-03-n15-tier2-ota.md).
- Current architecture: [ADR-004](../memory/decisions/ADR-004-n15-official-contiguous-ab-layout.md).
- Historical rejected option: [ADR-003](../memory/decisions/ADR-003-n15-paired-sector-swap.md).
- Previous completed stage: [N14 milestone](milestones/2026-08-03-n14-psram-board-verified.md).
- Source rollback point: merged N14 commit
  `6de4962147e5ee180def704d219ace9ae11f6e4e`.
