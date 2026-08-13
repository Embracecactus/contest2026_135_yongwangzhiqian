# ADR-023: Separate the BK7258 platform from physical-board variants

- Status: Accepted
- Date: 2026-08-09
- Decision owner: Project owner

## Context

The original `board/bk7258_t5ai` directory grew from one T5AI-Core bring-up
into the shared home for BK7258 chip support, CP/AP firmware, BL1/BL2/MCUboot,
SDK wrappers, partitions and peripheral drivers.  The project now has two
physical PCBs: T5AI-Core V1.0.1 and T5-Board V1.0.2.  Their BK7258 platform
code is common, while user GPIOs and fitted peripherals differ.

Treating `bk7258_t5ai` as both a chip platform and one physical board makes
new board mappings ambiguous.  Duplicating the whole directory would also
fork the boot chain and dual-core implementation.

## Decision

1. Rename the shared source root to `board/bk7258`.
2. Keep chip, CP/AP, bootloader, SDK wrapper, partition and build code in that
   shared root.
3. Put physical wiring under `board/bk7258/boards/t5ai_core` and
   `board/bk7258/boards/t5_board`.
4. Select exactly one board with
   `CONFIG_BK7258_BOARD_T5AI_CORE` or
   `CONFIG_BK7258_BOARD_T5_BOARD`.
5. Keep T5AI-Core as the default so existing defconfigs preserve their
   board-verified behavior.
6. Keep PCB revisions as metadata.  Add a revision selector only when a later
   revision changes a software-visible electrical contract.
7. Do not treat Tuya's `TUYA_T5AI_EVB` mapping as T5-Board evidence; the two
   schematics have different pin assignments.
8. Let each physical-board variant own every fixed electrical fact: fitted
   capability, pin route, polarity, pull/drive policy, bus attachment,
   address/chip-select, fixed-device frequency limits, panel timing and route
   conflicts.  Keep generic controller mechanics and runtime I2C/SPI
   transaction parameters in the shared chip wrapper and NuttX upper half.
9. Register attached devices through selected-board early/device hooks.  Keep
   mandatory platform lifetime initialization and application filesystem/MTD
   bringup in separate shared layers instead of one board-name-aware function.

## Consequences

- Existing custom-board paths, manifest links, scripts and maintained
  documentation move from `board/bk7258_t5ai` to `board/bk7258` in one
  migration.
- One driver implementation can consume the selected board's LED, key and
  other wiring without duplicating chip code.
- New physical boards provide a variant header and hook implementation rather
  than adding board-name tests or connector pin literals to `chip/`.
- Product defconfigs select a board and feature set; they do not become the
  canonical store for fixed board wiring or shared-bus transaction policy.
- T5AI-Core retains the current hardware evidence.  T5-Board remains
  schematic-verified until separately tested.
- A genuinely different future BK7258 module or board can be added as another
  physical variant without renaming the platform again.

## Rejected alternatives

1. Keep `bk7258_t5ai` as the outer name.  Rejected because it continues to
   conflate a shared BK7258 platform with the first carrier board.
2. Create two complete top-level BSP copies.  Rejected because CP/AP,
   bootloader and wrappers would immediately diverge.
3. Name T5-Board as `t5ai_evb`.  Rejected because it would falsely imply the
   Tuya EVB pinout.
