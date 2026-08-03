# Project

Last reviewed: 2026-08-03

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

- Team-owned code under `board/bk7258_t5ai/`, `app/`, `tools/`, `docs/`, and exported evidence under `logs/`.
- Bootloader, BSP, flash/LittleFS, AP SMP, RPTUN/RPMsg, supervision, RPMsgFS, Bluetooth, and PSRAM wrappers already approved by completed stages.
- Documentation, verification scripts, and durable project-memory checkpoints.

## Out of scope

- Permanent edits to official NuttX, openvela apps, Beken SDK source, or Beken SDK static libraries.
- Redistribution of the ignored proprietary SDK bundle.
- Unapproved future scope such as OTA, Wi-Fi data plane, security, cacheable PSRAM, or opening the N14 upper 8 MiB to allocators.
- Claims beyond recorded board gates, such as product lifetime, temperature/voltage, power-loss, or performance SLA.

## External constraints

- The project owner requires wrapper-based integration; temporary official-tree debug edits are allowed only if removed before the deliverable checkpoint.
- The latest supported bundle is checksum-pinned v3.1.1.9; legacy inputs remain rollback references.
- Hardware mutation, commit, push, PR, and destructive factory flash require their normal explicit authority.
- Canonical stage state is routed through `docs/bk7258-t5ai/next-stage-prompt.md`; no next MAIN Stage is currently approved.
