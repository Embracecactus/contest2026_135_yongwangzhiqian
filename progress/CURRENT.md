# Current Progress

Last updated: 2026-08-14
Updated by: Codex

## Current task

Publish the completed BK7258 on-die temperature stage from branch
`feat/bk7258-soc-temperature-v2` for a web PR against
`open-vela/contest2026_135_yongwangzhiqian:dev-ai-contest-2026`.

## Completed

- CP owns the immutable SDK temperature conversion; AP exposes it through a
  bounded, replay-safe RPMsg wrapper.
- Endpoint teardown, reply matching, initialization publication and PM
  admission races found during review are fixed.
- The v3.1.1.9 bundle contract is pinned to raw `11..1364`, 46 codes per 10 C,
  with the conversion direction confirmed from the SDK binary.
- Exact CP/AP profile preflight and signed build `18.6.62` / security counter
  `116` passed.
- Trust-preflighted apps-only download wrote only the CP/AP application
  segments.  Non-halting J-Link diagnostics passed 8/8 samples at raw 569.
- Absolute Celsius remains fail-closed unless a trusted per-device 25 C raw
  reference is explicitly configured.

Canonical evidence:

- [On-die temperature verification](verification/2026-08-14-bk7258-soc-temperature.md)
- [Pre-flash trust-chain verification](verification/2026-08-14-bk7258-preflash-trust-chain.md)

## Blockers

None.

## Next action

The owner creates and merges the prepared Chinese PR from
`fork/feat/bk7258-soc-temperature-v2` in the web UI.

## Fixed constraints

- Do not modify official NuttX/apps or Beken SDK source trees.
- Preserve P0/P1 SWD/RTT and COM3; never open COM4.
- Preserve unrelated untracked artifacts.
- Do not rotate trust roots or write boot-chain, OTP/eFuse, lifecycle or data
  regions without explicit owner authority.
- GPT-5.6-Luna delegation remains disabled.
