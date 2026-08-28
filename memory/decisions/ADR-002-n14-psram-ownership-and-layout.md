# ADR-002: CP owns PSRAM hardware; retain the lower 8 MiB ABI

- Status: Accepted
- Date: 2026-08-03
- Owners: Project owner

## Context

The T5-AI board detects 16 MiB PSRAM, while the official `projects/app` layout
defines a stable lower-8-MiB CP/AP ABI. AP is an SMP NuttX cluster, and the
BK7258 PSRAM bus cannot complete Arm exclusive stores used by allocator control
metadata.

## Drivers

- Follow official CP-only hardware initialization and PM ownership.
- Avoid changing established N13 memory consumers while bringing up PSRAM.
- Make AP dual-CPU allocation deadlock-free without modifying NuttX.
- Prove the physical 16 MiB without prematurely exposing unowned memory.

## Options considered

1. Expose all 16 MiB immediately through one shared allocator.
2. Give CP and AP dynamically sized regions from a new layout.
3. Retain the official lower-8-MiB ABI, use disjoint role-local heaps, and reserve the upper 8 MiB after a boot test.

## Decision

Use option 3. CP takes the official PSRAM PM vote, detects/tests capacity, and
only then releases AP. CP receives a 128 KiB heap and AP a 640 KiB heap; the
256 KiB AP section and upper 8 MiB stay reserved. Heap control and the outer
allocator spinlock remain in internal SRAM. AP realloc uses bounded
allocate-copy-free. All cores use the non-cacheable MPU contract.

## Consequences

- Positive: N14 preserves the official app ABI and verified N13 services.
- Positive: full physical capacity is tested while runtime ownership stays explicit.
- Positive: AP CPU0/CPU1 allocation is serialized without an official NuttX patch.
- Negative: most physical PSRAM is intentionally unavailable to applications.
- Negative: non-cacheable memory favors correctness over peak performance.

## Evidence and validation

- AP CPU0/CPU1 each completed 16 allocator iterations with zero errors and stable free space.
- CP completed 256 heap iterations; warm cycle, cold reset, factory calibration, RPMsg, and Bluetooth regressions passed.
- Canonical evidence: [N14 evidence index](../../docs/platforms/bk7258/nuttx-port/n14-evidence-index.md).

## Reversal signals

- An accepted stage defines owners, cache/DMA semantics, and board gates for the upper 8 MiB.
- A verified upstream allocator or hardware rule changes the exclusive-store/spinlock constraint.
- A product workload demonstrates a measured need for a different static partition.

## Open questions

- Which future workload, if any, should own the upper 8 MiB?
