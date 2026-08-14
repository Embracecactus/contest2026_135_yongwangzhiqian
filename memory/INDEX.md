# Project Memory Index

Last reviewed: 2026-08-14

Read `../progress/CURRENT.md` after this file, then load only the documents relevant to the active task.

## Durable context

- [Project](PROJECT.md): purpose, users, scope, and success criteria.
- [Architecture](ARCHITECTURE.md): system boundaries, components, data flows, and constraints.
- [Rules](RULES.md): accepted domain rules, invariants, security, and UX conventions.
- [Operations](OPERATIONS.md): environments, verification, deployment, rollback, and recovery.
- [Decisions](decisions/): accepted architecture and product decisions with consequences.

## Accepted decisions

- [ADR-001](decisions/ADR-001-wrapper-only-official-source-boundary.md): official NuttX/apps/SDK remain read-only; permanent adaptations use repository-owned wrappers.
- [ADR-002](decisions/ADR-002-n14-psram-ownership-and-layout.md): CP owns PSRAM hardware; lower 8 MiB ABI is retained and the upper 8 MiB remains reserved.
- [ADR-004](decisions/ADR-004-n15-official-contiguous-ab-layout.md): migrate once to the official-style contiguous CP/AP A/B layout, relocate/clear LittleFS, and preserve the calibration tail.
- [ADR-005](decisions/ADR-005-n15-boot-selector-metadata-v1.md): freeze the append-only metadata v1 ABI, fail-closed A/B validation, and the boundary between a pending candidate and one-trial permission.
- [ADR-006](decisions/ADR-006-n15-symmetric-dual-bank-ota.md): use two metadata banks and slot-neutral states for safe inactive-slot A/B rotation; the approved minimal board gate now passes, while future writes remain separately authorized.
- [ADR-007](decisions/ADR-007-n16-cp-radio-ap-nuttx-network.md): keep official Wi-Fi RF/MAC/WPA control on CP and connect the AP proxy to the native NuttX network stack through a repository-owned netdev adapter.
- [ADR-008](decisions/ADR-008-n17-phased-ota-authentication.md): use a hardware-root-compatible signed CP/AP release format now, while deferring every irreversible OTP/eFuse and secure-boot activation to a separately authorized hardware gate.
- [ADR-009](decisions/ADR-009-n17-dedicated-manifest-policy-sectors.md): reserve separate per-slot Manifest sectors and a normal-write-forbidden one-way authentication-policy sector.
- [ADR-010](decisions/ADR-010-n17-format3-lifecycle-journal.md): freeze the 256-byte append-only format-3 lifecycle record and atomic confirmation/counter-floor transition.
- [ADR-011](decisions/ADR-011-n17-fail-closed-format2-migration.md): migrate through two authenticated format-3 baselines and then permanently forbid format-2/header-only fallback.
- [ADR-014](decisions/ADR-014-bk7259-v4-retirement-and-bk7236-secureboot-reference.md): retire BK7259/v4 completely; retain v3.1.1.9 for runtime and BK7236 `bk_idk release/v2.0.1` only for secureboot reference.
- [ADR-015](decisions/ADR-015-mcuboot-packer-provenance.md): reuse official v3.1.1.9 CRC/partition tools where present; keep the board packer as a thin standard-MCUboot adapter until the missing secureboot tool is obtained.
- [ADR-016](decisions/ADR-016-self-owned-bl1-manifest.md): separate the public bootloader reverse result from the self-owned, recoverable BL1 Manifest that authorizes BL2 only.
- [ADR-017](decisions/ADR-017-bk7258-official-secureboot-source-crosswalk.md): classify the newly cloned official `bk_idk` checkout into BK7258 documentation/tool evidence versus BK7236-only buildable secureboot examples.
- [ADR-018](decisions/ADR-018-bk7258-board-bl2-primary-secondary-fallback.md): keep the official primary BL2 envelope and add a board-owned secondary candidate with Manifest-verified fail-closed fallback.
- [ADR-019](decisions/ADR-019-mcuboot-cp-ap-same-slot-gate.md): expose only one physical CP/AP slot per MCUboot attempt and reject cross-slot-only combinations before handoff.
- [ADR-020](decisions/ADR-020-mcuboot-cp-ap-same-generation-binding.md): bind CP/AP image versions and protected security counters within one launchable generation.
- [ADR-021](decisions/ADR-021-mcuboot-version-aware-cp-ap-selection.md): let upstream MCUboot order both slots first, then enforce the board-owned same-slot CP/AP launch gate and isolate-slot fallback.
- [ADR-022](decisions/ADR-022-bk7258-secureboot-bk7236-semantic-port.md): use BK7236 Secure Boot as a read-only semantic reference for the active BK7258 BL1/BL2 port, while keeping BK7258-specific ABI unknowns and OTP/eFuse operations gated.
- [ADR-023](decisions/ADR-023-bk7258-platform-board-variants.md): keep one generic BK7258 platform root and select T5AI-Core or T5-Board wiring as physical-board variants, with Core as the compatibility default.
- [ADR-024](decisions/ADR-024-bk7258-physical-board-build-profiles.md): model each reusable firmware configuration as a metadata-declared profile for one physical board and role; pair CP/AP by explicit compatibility rather than filename conventions.

## Superseded decisions

- [ADR-003](decisions/ADR-003-n15-paired-sector-swap.md): the journaled physical-sector-swap proposal was never accepted; ADR-004 replaced it before any board write.
- [ADR-012](decisions/ADR-012-n17-defer-mcuboot.md): the v3.1.1.9/NuttX-port-only MCUboot deferral is superseded by ADR-013; no migration or board action is implied.
- [ADR-013](decisions/ADR-013-sdk-v401-mcuboot-bl2-reassessment.md): v4.0.1 active-baseline decision is superseded by ADR-014.

## Current verified baseline

- Read [Current Progress](../progress/CURRENT.md) for the branch, publication/merge state, next action, and rollback point.
- The 66/100 review's actionable runtime-contract findings, the exact-once PM
  retry proof, WDT/timer failure-recovery proof, final signed build and
  T5-Board camera/RPTUN/supervisor/PM state are recorded in the
  [2026-08-14 runtime-contract verification](../progress/verification/2026-08-14-bk7258-runtime-contracts.md).
- The current bringup/physical-board layering and its Classic/CMake build
  evidence are recorded in the
  [2026-08-13 board-layer verification](../progress/verification/2026-08-13-bk7258-bringup-board-layers.md).
- The canonical 18-profile catalog, metadata pairing gates, shared-build lock
  and cross-board build evidence are recorded in the
  [2026-08-13 profile-consolidation verification](../progress/verification/2026-08-13-bk7258-config-profile-consolidation.md).
- The complete three-core coordinated low-voltage PM implementation, the
  official-SDK/Tuya wrapper audit, the final signed build matrix and the
  non-halting COM3/P0-P1 board proof are recorded in the
  [2026-08-13 verification record](../progress/verification/2026-08-13-bk7258-coordinated-pm-sdk-audit.md).
- New contributors should start with the
  [BK7258/T5-AI beginner porting guide](../docs/bk7258-t5ai/beginner-porting-guide/README.md),
  then return here for durable decisions and to Current Progress for live state.
- N14 completion is archived in [the N14 milestone](../progress/milestones/2026-08-03-n14-psram-board-verified.md).
- Detailed N14 test output and hashes are canonical in [the N14 evidence index](../docs/bk7258-t5ai/nuttx-port/n14-evidence-index.md).
- N16 STA association, native NuttX sockets, active-Wi-Fi retained-service
  coexistence and bounded lifecycle closure are recorded in the
  [N16 board verification](../progress/verification/2026-08-06-n16-wifi-sta-coexistence.md).
- The BL1/BL2 SRAM-boundary hypothesis was disproved by a complete CRC-expanded
  128 KiB BL2 copy; see the [CRC-boundary verification](../progress/verification/2026-08-07-bl1-bl2-128k-crc-boundary.md).
- The self-owned BL1 Manifest has an independent board reject/recovery proof:
  a valid signature reaches BL2/NSH, while a one-bit signature mutation is
  rejected before BL2 and the good boot is recoverable; see the
  [Manifest board validation](../progress/verification/2026-08-07-self-owned-bl1-manifest-board.md).
  The separate two-image NuttX MCUboot BL2 proof shows invalid A selecting B
  through the observed hardware remap and valid A restored as default; see the
  [A/B fallback board validation](../progress/verification/2026-08-07-bl1-bl2-mcuboot-ab-fallback-board.md).
  Both are software-rooted recovery evidence, not Beken Secure Boot or N17
  policy activation.
- The subsequent v2 Manifest/BL2 handoff correction reached `B2HANDOFF` and
  NuttShell on the board after restoring the CP image's `MSPLIM=0x28010000`;
  see [the v2 handoff board record](../progress/verification/2026-08-07-bl1-v2-manifest-bl2-handoff-board.md).
- N17's Manifest ABI, dedicated sector layout, format-3 lifecycle journal and
  fail-closed migration are architecture-frozen. The partition generator,
  public-only vector and
  [portable format-3 model](../progress/verification/2026-08-06-n17-format3-host-model.md)
  pass on host. The staged secureboot path also has a
  [v3.1.1.9 Non-Secure SDK build record](../progress/verification/2026-08-06-n17-v3119-nspe-sdk-build.md): CP passes, AP needs official resolution.
  Earlier MCUboot proof-of-life is recorded in
  [the BL1/BL2 CRC-boundary verification](../progress/verification/2026-08-07-bl1-bl2-128k-crc-boundary.md);
  the subsequent CP/AP multi-image fallback result is linked above. N17 policy
  gates remain closed. Read [Current Progress](../progress/CURRENT.md) before
  implementation.
- The exact official v3.1.1.9 BK7258 A/B bootloader has now been statically
  rechecked in Ghidra. Read the [A/B reverse-engineering evidence](../progress/verification/2026-08-07-ghidra-bk7258-ab-bootloader.md)
  for the RBL/FNV/remap/trial state machine and the boundary from official A/B
  OTA to the separate MCUboot/Secure Boot work.
- The [BL1/BL2/MCUboot evidence matrix](../progress/verification/2026-08-07-bk7258-boot-chain-evidence-matrix.md)
  is the canonical boundary between public binary facts, generic SDK templates
  and still-missing BK7258 Secure-Boot artifacts.
- The valid newer-B MCUboot selection and restoration are now board-proven;
  see the [version-aware selection evidence](../progress/verification/2026-08-07-mcuboot-version-aware-selection-board.md).
- The [official BK7258 Secure Boot source crosswalk](../progress/verification/2026-08-07-bk7258-official-secureboot-repo.md)
  records the newly cloned `bk_idk` documentation/tool evidence, recovered
  generic Manifest bytes, official MCUboot 1.9.0 reference, and the remaining
  absence of a buildable BK7258 secureboot project.
- The active route is the [N17-SB Secure Boot port](../progress/ROADMAP.md):
  SB0-SB6 complete the recoverable BK7258 BL1/BL2/MCUboot chain before any
  network OTA work; SB-H is the separately authorized hardware-root phase.
- The 2026-08-08 remaining-gates record consolidates the anchored Manifest
  key rejection, read-only MCUboot security-counter floor, host-only official
  AES/CRC/merge/sign order, and the recoverable A/B reject/fallback matrix:
  [remaining-gates verification](../progress/verification/2026-08-08-bk7258-secureboot-remaining-gates.md).
- The current full BL1/BL2/MCUboot package was sparsely deployed and reached
  `B2HANDOFF -> NuttShell` after an older board image was observed looping at
  `B1PAGE`; see [current-build board boot](../progress/verification/2026-08-08-bk7258-current-build-board-boot.md).
- The [BK7236 v2.0.1 security-document audit](../progress/verification/2026-08-07-bk7236-security-docs-audit.md)
  records the complete same-Armino-architecture reference, including every
  text page and security diagram, while keeping single-core BK7236 details
  separate from unproven BK7258 facts.
- The [normal bootloader Ghidra evidence](../progress/verification/2026-08-07-ghidra-bk7258-normal-bootloader.md)
  records only direct raw-binary facts and keeps its unrecovered selection
  state machine explicitly unknown.
- N15-M migrated the board to the ADR-004 contiguous layout and re-verified
  the N14 runtime matrix. Read the
  [N15-M verification record](../progress/verification/2026-08-03-n15-migration-board-verification.md).
- R1/R2 sector-swap material is historical rejected-option evidence only.
  [N15-A](../progress/verification/2026-08-03-n15-a-host-pair-bundle.md)
  is host-verified and
  [N15-B](../progress/verification/2026-08-04-n15-b-host-staging.md),
  [N15-C](../progress/verification/2026-08-04-n15-c-host-boot-selection.md),
  [N15-D](../progress/verification/2026-08-04-n15-d-host-trial.md),
  [N15-E](../progress/verification/2026-08-04-n15-e-host-publication.md)
  and [N15-F](../progress/verification/2026-08-04-n15-f-host-validation.md)
  are host/source/ELF-verified. The
  [historical N15-V fault matrix](../progress/verification/2026-08-04-n15-v-host-fault-injection.md)
  is additionally host/source/ELF/dry-run evidence for the original 15-case
  format-2 workflow. The
  [symmetric host closure](../progress/verification/2026-08-04-n15-format2-symmetric-host.md)
  adds independently checked A-to-B-to-A packaging with 16 unique identities.
  The [physical symmetric lifecycle](../progress/verification/2026-08-04-n15-physical-symmetric-lifecycle.md)
  verifies generation 314 A-to-B through confirmed B and generation 315
  B-to-A through confirmed A, including both metadata banks and retained N14
  services. A subsequent complete post-confirm removal of both USB and J-Link
  power recovered the same generation-315 confirmed-A state with AP, CPU2 and
  RPTUN healthy. Read Current Progress before any target action.

## Memory rules

- Record verified, durable facts; label assumptions and unknowns.
- Link to canonical sources instead of duplicating details.
- Keep secrets and personal or production data out of this directory.
- When a fact becomes obsolete, update it and preserve material history in a decision or milestone.
- Legacy `memory/*.md` files outside this managed index remain local compatibility notes and are not authoritative unless explicitly linked here.
