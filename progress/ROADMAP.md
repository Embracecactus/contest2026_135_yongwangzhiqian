# Roadmap

Last reviewed: 2026-08-04

## Now

- N15 Tier-2 paired CP/AP OTA is the accepted active MAIN Stage.
- N15-R1 is source-verified/read-only: exact v3.1.1.9 RBL/AB behavior and remap incompatibility are documented and tool-checked.
- N15-R2 completed the rejected sector-swap feasibility work. The owner then
  approved a one-time destructive LittleFS migration and accepted ADR-004:
  align with the official contiguous primary CP/AP + `s_app` layout instead.
- N15-M is `board-verified`: the new linker/packer/boot/MTD layout was built,
  migrated with two bounded loader segments, and passed LittleFS plus the
  retained N14 board matrix and physical reset 3/3.
- N15-A is `host-verified`: deterministic pair manifest, exact v3.1.1.9 RBL
  container/parser, official golden vectors, 2 positive/13 negative cases and
  a real clean-build bundle all passed without board writes.
- N15-B is `host/source/ELF-verified`: full-candidate preflight, CP-only Flash
  guard/staging wrapper, exact-bound sector transactions, final digest, 2
  positive/21 negative cases and the full dual build passed without board writes.
- N15-C is `host/source/ELF-verified`: fixed append-only metadata ABI, full
  A/B pair validation, exact clean-room one-offset remap path, 5 positive/28
  negative cases, final boot ELF closure and the full dual build passed
  without board writes.
- N15-D is `host/source/ELF-verified`: one-trial append/read-back,
  confirm/rollback, 4 positive/113 negative cases, 48 reset boundaries and
  final Boot/CP ELF closure passed with all mutation gates closed.
- N15-E is `host/source/ELF-verified`: pending publication, bounded metadata
  reclamation, 5 positive/142 negative cases, 8 erase and 112 program/reset
  boundaries passed without board access.
- N15-F validation foundation is `host/source/ELF-verified`: the target-side
  5000 ms AP-supervisor health window with 250 ms polling (the host model uses
  a 1000 ms fixture), separate gates-on profile, fixed volatile upper-PSRAM
  transfer ABI, exact-token `bkota` path and dry-run-first WSL2 loader passed.
  Normal firmware was rebuilt with gates zero and no `bkota`.
- N15 format-2 symmetric host closure is `host/source/ELF/dry-run-verified`:
  dual metadata banks and inactive-slot A/B rotation pass their core matrices;
  16 unique A-to-B-to-A packages pass the independent verifier and loader
  dry-runs.
- Current gate: N15-V physical staging/remap/confirm/rollback, controlled
  interruption matrix and retained regression require fresh owner authority.
- Keep `progress/CURRENT.md` aligned with each material result, blocker, architecture decision, commit, push, or deployment.

## Next

- N15-V: after fresh, range-specific owner authority, run one minimal physical
  A-to-B lifecycle and one B-to-A lifecycle, retain raw evidence, verify the
  retained N14 services and restore the normal gates-zero A profile.
- ADR-006 symmetric inactive-slot rotation is host/source/ELF verified; the
  remaining question is physical validation, not another OTA architecture.

## Later

- Only after N15 is complete and board-verified on v3.1.1.9 may a separately approved task validate preserved legacy SDK versions.
- Wi-Fi data plane, signed/authenticated update policy, Bluetooth warm restart,
  and general upper-8 PSRAM allocator ownership remain separate candidates.
- Product hardening may include uncontrolled physical power cuts, temperature/voltage stress, long-duration wear, and cache/DMA PSRAM design.

## Explicitly deferred

- The ADR-003 physical-sector swap, scratch/journal ABI, and SRAM copy engine
  are superseded and must not be enabled.
- Repeating the N15-M factory migration without fresh owner authority is out
  of scope; normal development uses bounded sparse updates.
- Network OTA transport, bootloader self-update, key provisioning, signatures, and anti-rollback are outside N15's first implementation boundary.
- CPU0 direct 480 MHz, upper-8 PSRAM allocator exposure, and QEMU work remain separate scope.

Priorities beyond the active stage are proposals until the project owner accepts them. Move completed phase detail to `milestones/`.
