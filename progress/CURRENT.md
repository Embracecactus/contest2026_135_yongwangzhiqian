# Current Progress

Last updated: 2026-08-15
Updated by: Codex (GPT-5.6-Luna MAX delegated implementation)

## Current task

P0 of the BK7258 platform-v2 migration is implemented pending primary-agent
review on `refactor/bk7258-platform-v2`.  The sole local P0 commit is the only
commit after the approved base; the exact amended HEAD is reported in the
handoff rather than embedded here to avoid a recursive self-hash.

## Verified baseline

- Approved base: `origin/dev-ai-contest-2026` at
  `2eb0353ee6989e6654629aa0b67cac8c7c1ee810`; independent remote read-only
  verification returned the same exact SHA.
- The frozen `board/bk7258/configs` tree is 27 profile directories, 55 regular
  files and 82 descendants.  Its Git tree is
  `3fb581ad1a0d0c439f12a7c5f18f8989c8a448df`.
- Merged JPEG/SARADC behavior remains present.  SARADC physical endpoint
  remains PARTIAL: released ADC14/controller evidence exists, but a real SW5
  released/pressed/released run is pending.

## P0 implementation

- Git-object-derived freeze manifest binds the exact approved commit/tree,
  current configs bytes, modes, metadata, defconfig symbols, SDK manifests,
  pair graph and mandatory ledger digest.
- The 27-row ledger is explicit `consolidation-review`/proposal state.  It
  separates product/resource mode from validation suite and creates no product,
  board or configuration seed.
- The exact-base consumer snapshot has 104 recomputed consumers plus neutral
  reviewed-path records for board/platform documentation.  It makes no
  unreliable active/inactive claim and records historical-only false positives.
- Strict duplicate-key JSON loading, schema/path/hash/type checks, root
  lstat/mode checks, Git-object availability and all adversarial negative tests
  are implemented.
- ADR-026 is proposed for owner review; ADR-024 remains byte-identical and is
  identified as legacy/as-is in `memory/ARCHITECTURE.md`.

Detailed evidence: [P0 freeze verification](verification/2026-08-15-bk7258-platform-v2-p0.md).

## Exact next action

Primary agent independently reviews the amended P0 commit and evidence.  Do
not begin P1 until review accepts this checkpoint.

## Remaining boundaries

- No legacy profile was deleted or modified; no equivalent product resolver
  exists yet.
- No product manifest, generated seed, SDK registry, production build,
  signing, package, Flash, hardware operation or remote mutation occurred.
- Cross-platform stable-device transport discovery is a P7 requirement:
  default auto discovery, unique USB identity matching and explicit
  Windows/Linux/WSL backend capability; no COM number or `/dev` path is a
  product/board default.

## Fixed constraints

- Official NuttX/apps/Beken SDK and proprietary SDK bytes remain untouched.
- Preserve validated T5/SARADC/JPEG/Audio/TF/Wi-Fi behavior and unrelated user
  files; do not inspect N17 or another historical trust domain.
- No new legacy config directory, driver/validator profile, product/board
  seed, upstream rebase, P1 code, build/sign/package/flash/hardware action or
  push is authorized in P0.
- The future mirror URL is owner-designated only and grants no redistribution
  right; licensing/authorization/notices/SBOM must precede any P2 action.
