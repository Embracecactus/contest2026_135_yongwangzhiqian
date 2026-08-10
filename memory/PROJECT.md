# Project

Last reviewed: 2026-08-10

## Purpose

Port openvela/NuttX to the Beken BK7258 Tuya T5-AI board with a reproducible,
auditable boot and dual-image runtime. The repository is the team-owned overlay
for the 2026 openvela hardware-porting contest.

## Users and primary scenarios

- Project maintainers who build, flash, debug, and extend the BK7258 port.
- Contest reviewers who need reproducible source, architecture, and board evidence.
- Coding agents resuming work without loading raw conversation history.

## Goals and success measures

- Boot through the source Tier-1 bootloader into interactive NuttX/NSH.
- Run CP NuttX plus AP native SMP NuttX with bounded lifecycle and RPMsg services.
- Integrate Beken capabilities through minimal board wrappers while preserving official trees.
- Attach source, build, and physical-board evidence to every completed MAIN Stage.
- Keep rollback profiles and deterministic SDK bundle checks available.

## In scope

- Team-owned code under `board/bk7258/`, `app/`, `tools/`, `docs/`, and exported evidence under `logs/`.
- Bootloader, BSP, flash/LittleFS, AP SMP, RPTUN/RPMsg, supervision, RPMsgFS, Bluetooth, and PSRAM wrappers approved by completed stages.
- A recoverable board-owned BL1, pinned NuttX MCUboot BL2 and signed same-slot
  CP/AP boot chain. The contiguous A/B geometry remains, while the former
  custom N15/N17 update lifecycle is retired from active source.
- Documentation, verification scripts, and durable project-memory checkpoints.

## Out of scope

- Permanent edits to official NuttX, openvela apps, Beken SDK source, or Beken SDK static libraries.
- Redistribution of the ignored proprietary SDK bundle.
- Unapproved future scope such as network OTA transport, field-update
  lifecycle, hardware-root key provisioning, cacheable PSRAM, or opening the
  N14 upper 8 MiB to allocators.
- Claims beyond recorded board gates, such as product lifetime, temperature/voltage, power-loss, or performance SLA.

## External constraints

- The project owner requires wrapper-based integration; temporary official-tree debug edits are allowed only if removed before the deliverable checkpoint.
- Official v3.1.1.9 is the sole active SDK baseline; legacy inputs remain preserved references and are not validated until the active stage is complete and the owner separately approves that work.
- Hardware mutation, commit, push, PR, and destructive factory flash require their normal explicit authority.
- Canonical current state is `progress/CURRENT.md`; the roadmap records phase
  history without making retired N15/N17 code part of the active firmware.
