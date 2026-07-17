# AI collaboration worklog

This directory preserves the development chronology and recovery prompts for the RV1126B HPMCU OpenVela/NuttX port. It is not the final contest AI log; exported JSONL session logs belong under `logs/<github-login>/` according to `logs/README.md`.

## Current reading order

1. [Canonical RV1126B NSH port guide](../adaptation/nsh-port.md) — current implementation, build/package procedure, and limits.
2. [Immutable 2026-07-14 NSH baseline evidence](../verification/2026-07-14-rv1126b-nsh-baseline.md) — formal board-test record, separate from this worklog.
3. [Phase 05 verified-baseline follow-up](prompts/phase-05-verified-baseline-follow-up.md) — current handoff and remaining work.
4. [Porting stage record](2026-07-12-rv1126b-openvela-porting.md) — historical development chronology with final-status correction.

## Phase index

| Phase | Scope | Status | Record |
| --- | --- | --- | --- |
| 01 | Minimal build repair | Historical, complete | [Prompt](prompts/phase-01-build-fix.md) |
| 02 | Image packaging and first board boot | Historical, complete | [Prompt](prompts/phase-02-boot-verify.md) |
| 03 | DCache/startup corrections and NSH TX | Historical, complete | [Prompt](prompts/phase-03-dcache-fix-verify.md) |
| 04 | UART RX and interactive NSH restoration | Historical, complete and board-verified | [Prompt](prompts/phase-04-nsh-uart-rx-restore.md) |
| 05 | Preserve baseline, synchronize documentation, make a candidate, and collect missing evidence | **Current** | [Prompt](prompts/phase-05-verified-baseline-follow-up.md) |

The formal baseline evidence is deliberately outside the phase sequence: [2026-07-14 RV1126B NSH baseline](../verification/2026-07-14-rv1126b-nsh-baseline.md). It verifies boot, prompt, RX, `help`, and prompt return, but not `uname -a`, board revision, the exact flash command, or a timestamped capture.

## Directory map

```text
docs/ai-worklog/
├── README.md
├── 2026-07-12-rv1126b-openvela-porting.md
└── prompts/
    ├── phase-01-build-fix.md
    ├── phase-02-boot-verify.md
    ├── phase-03-dcache-fix-verify.md
    ├── phase-04-nsh-uart-rx-restore.md
    └── phase-05-verified-baseline-follow-up.md
```

## Scope rule

Historical prompts document the route taken to reach the baseline. They are not permission to revive stale UART4, placeholder, or idle-loop polling approaches. Current work must remain in the Team 135 overlay and follow the canonical guide plus Phase 05.
