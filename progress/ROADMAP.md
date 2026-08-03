# Roadmap

Last reviewed: 2026-08-03

## Now

- N15 Tier-2 paired CP/AP OTA is the accepted active MAIN Stage.
- N15-R1 is source-verified/read-only: exact v3.1.1.9 RBL/AB behavior and remap incompatibility are documented and tool-checked.
- N15-R2 completed the rejected sector-swap feasibility work. The owner then
  approved a one-time destructive LittleFS migration and accepted ADR-004:
  align with the official contiguous primary CP/AP + `s_app` layout instead.
- N15-M is `board-verified`: the new linker/packer/boot/MTD layout was built,
  migrated with two bounded loader segments, and passed LittleFS plus the
  retained N14 board matrix and physical reset 3/3.
- Current gate: N15-A deterministic pair manifest and exact v3.1.1.9 RBL
  container/parser, with host-only positive and negative tests.
- Keep `progress/CURRENT.md` aligned with each material result, blocker, architecture decision, commit, push, or deployment.

## Next

- N15-A/B/C: deterministic pair bundle, CP-only `s_app` staging, and
  fail-closed trial metadata/parser after the migrated baseline is verified.
- N15-D/E/F/V: hardware-remap trial/confirm/revert, fault injection,
  performance baseline, and final board regression.

## Later

- Only after N15 is complete and board-verified on v3.1.1.9 may a separately approved task validate preserved legacy SDK versions.
- Wi-Fi data plane, signed/authenticated update policy, Bluetooth warm restart, and explicit upper-8 PSRAM runtime ownership remain separate candidates.
- Product hardening may include uncontrolled physical power cuts, temperature/voltage stress, long-duration wear, and cache/DMA PSRAM design.

## Explicitly deferred

- The ADR-003 physical-sector swap, scratch/journal ABI, and SRAM copy engine
  are superseded and must not be enabled.
- Repeating the N15-M factory migration without fresh owner authority is out
  of scope; normal development uses bounded sparse updates.
- Network OTA transport, bootloader self-update, key provisioning, signatures, and anti-rollback are outside N15's first implementation boundary.
- CPU0 direct 480 MHz, upper-8 PSRAM allocator exposure, and QEMU work remain separate scope.

Priorities beyond the active stage are proposals until the project owner accepts them. Move completed phase detail to `milestones/`.
