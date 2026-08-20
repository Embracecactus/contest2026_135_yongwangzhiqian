# ADR-029: BK7258 storage and boot topologies

Status: Accepted

Date: 2026-08-20

## Context

BK7258 uses one project boot chain across physical boards, while persistent
media differs: on-chip Flash, removable block media and soldered block media.
An application may require persistence but must not select partitions,
filesystem drivers, board pins or cross-core storage transport.

Manifest A/B and BL2 A/B were designed for BL2 update and fallback. They are
not compatibility debris and must survive the tooling rewrite. The verified
CP/AP A/B model likewise keeps primary CP/AP and the contiguous secondary
pair.

## Decision

- Keep one project BL1, Manifest A/B, project BL2 A/B and MCUboot CP/AP A/B.
- Use the previously verified geometry as the initial CSV values. All sizes
  and offsets remain compile-time adjustable through the selected CSV.
- Make BL2 B explicit in CSV; no source may infer it from an unused gap.
- Support three application-independent storage topologies:
  `onchip-persistent`, `removable-block` and `fixed-block`.
- A physical-board binding exposes only electrical capability. A system
  configuration selects a topology; applications consume a mounted storage
  service or explicit data directory.
- Ordinary build, package and boot never format or clear persistent media.
  Provisioning is an explicitly named operation outside normal firmware
  delivery.
- Pin the OpenVela ARM prebuilt in the team manifest. OpenVela, SDK rebuild,
  project BL1 and project BL2 share it without a PATH fallback.
- The official Beken bootloader remains reference-only. Project BL1 and BL2
  are the executable boot inputs; upstream MCUboot supplies the standard
  CP/AP signature format and verifier.

## Consequences

- Changing a CSV before compilation regenerates all C/LD/SDK adapters and a
  new layout identity. It is not an in-place runtime partition resize.
- A deployed layout-identity change requires an explicit migration or full
  reflash; it is not an ordinary OTA update.
- T5AI-Core currently maps to on-chip persistence, T5-Board to removable
  media and AIDK AI Toy to fixed SD NAND, but those board names do not appear
  in the storage algorithm.
- Vela-Claw and other applications do not appear in layout, image, package,
  trust or boot code.
