# RV1126B Review Example — P2-A Four-Dimensional Review

Real-world example of the four-dimensional review gate applied to RV1126B P2-A RPMsg/RPTun code.

## Context

- Target: `board/contest_board/chip/` (mailbox driver + RPTUN adapter)
- Files: `rv1126b_mailbox.c`, `rv1126b_mailbox.h`, `hardware/rv1126b_mailbox.h`, `rv1126b_rptun.c`
- Also: `ld.script`, `Kconfig`, `Make.defs`, `defconfig`, `bringup.c`, `evb.h`

## Review Process

### Phase 1: Initial Review (4 parallel Agents)

Each Agent reviewed all changed files through its dimension lens:
- Concurrency Agent: traced all shared state access paths
- Register Agent: verified MMIO semantics against SDK HAL/Linux
- Startup Agent: checked init sequence and rollback
- Build Agent: verified Make/Kconfig/linker consistency

### Phase 2: Adversarial Verification

For each initial finding, a second Agent tried to **refute** it:
- If the finding used `.manual` (backup) evidence → discarded
- If the finding contradicted actual runtime behavior → discarded
- If the finding was confirmed by current repo-managed code → kept

### Phase 3: Arbitration

Disputed findings were arbitrated by tracing the exact code path:
- `gfeatures=0x9` vs `gfeatures=0` → traced through rpmsg_virtio_probe, confirmed VALID AS-IS
- `DRIVER_OK` skipping ACK/DRIVER → traced through rproc_virtio_wait_remote_ready, confirmed VALID AS-IS
- CMake SRCS missing → confirmed FATAL_ERROR guard, downgraded to Info

## Final Report (After Arbitration)

| # | Severity | File:Line | Summary |
|---|----------|-----------|---------|
| 1 | Low | rv1126b_rptun.c:498-526 | Check-then-act TOCTOU in init (UP safe, SMP risk) |
| 2 | Low | rv1126b_rptun.c:253-256 | gfeatures=0x9 local preset, no wire-level impact |
| 3 | Low | ld.script + rv1126b_rptun.c | Three independent hardcoded constants, no cross-verify |

## Key Lessons

1. **Evidence hierarchy**: repo-managed code > SDK reference > backup/manual files
2. **Adversarial verification** prevents plausible-but-wrong findings
3. **Severity calibration**: "architectural concern" ≠ "code defect"
4. **Runtime boundary**: static review can prove absence of some bugs, but presence requires hardware
