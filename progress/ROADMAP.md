# Roadmap

Last reviewed: 2026-08-03

## Now

- N14 is complete, board-verified, and published to `fork/feat/bk7258-n14-psram`; owner PR review and merge remain pending.
- Keep `progress/CURRENT.md` aligned with any commit, push, rollback, or new stage approval.

## Next

- No accepted next MAIN Stage. Candidate discussion topics are Tier-2 OTA, Wi-Fi data plane, security, Bluetooth warm restart, or an explicit upper-8 PSRAM policy.
- Before implementation, create one bounded stage plan with owners, resource map, failure behavior, rollback, and board gates.

## Later

- Product hardening may include physical power-cut, temperature/voltage memory stress, cache/DMA PSRAM design, and AP automatic recovery.
- Contest presentation/submission packaging remains a separate owner-prioritized activity.

## Explicitly deferred

- CPU0 direct 480 MHz work is deferred; the verified product path uses the SDK-aligned 320 tier.
- Upper 8 MiB allocator exposure, cacheable PSRAM, and dynamic CP/AP partitioning are deferred beyond N14.
- QEMU work is outside the N14 checkpoint and must not be mixed into its commit without separate scope approval.

Priorities are proposals until the project owner accepts them. Move completed phase detail to `milestones/`.
