# Current Progress

Last updated: 2026-08-15
Updated by: Codex (GPT-5.6-Luna MAX delegated implementation)

## Current task

P1 composition framework is implemented on `refactor/bk7258-platform-v2`; P2
SDK registry work follows immediately.  P0 remains the accepted foundation;
legacy profiles and production build behavior remain usable.

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

## P1 implementation

- Strict duplicate-key schemas validate board, product, mode, role, fragment
  and resolved IR documents with safe identifiers, repository-relative source
  views and a content identity hash; the schema contract is versioned in the
  existing scripts path.
- Board/product/mode/role composition is deterministic and fail-closed: one
  board is selected by the product, conflicting symbols and missing/cyclic
  fragment dependencies fail, and no T5 fallback exists.
- The canonical CMake adapter keeps the existing board/chip source tree
  read-only, creates a role-local build/artifact view, forbids a shared
  `.config`, and emits a machine-readable view identity; the Classic
  adapter-isolation feasibility report is explicitly unproven and does not
  alter the legacy builder.
- Catalog inputs live in the existing `board/bk7258/scripts` path.  No new
  config profile, product directory, SDK bytes or production source was added.

Detailed evidence: [P1 resolver verification](verification/2026-08-15-bk7258-platform-v2-p1.md),
with the [P0 freeze verification](verification/2026-08-15-bk7258-platform-v2-p0.md)
as its immutable baseline.

## Exact next action

Continue with P2 immutable SDK bundle/set/lock metadata and verification
framework.  SDK archives remain metadata-only and no private mirror mutation
is permitted.

## Remaining boundaries

- No legacy profile was deleted or modified; no equivalent product resolver
  exists yet.
- No generated build seed, SDK registry bytes, production build, signing,
  package, Flash, hardware operation or remote mutation occurred.
- Cross-platform stable-device transport discovery remains a P7 requirement:
  default auto discovery, unique USB identity matching and explicit
  Windows/Linux/WSL backend capability; no COM number or `/dev` path is a
  product/board default.

## Fixed constraints

- Official NuttX/apps/Beken SDK and proprietary SDK bytes remain untouched.
- Preserve validated T5/SARADC/JPEG/Audio/TF/Wi-Fi behavior and unrelated user
  files; do not inspect N17 or another historical trust domain.
- No new legacy config directory or driver/validator profile was added;
  upstream rebase, build/sign/package/flash/hardware action and push remain
  forbidden.
- The future mirror URL is owner-designated only and grants no redistribution
  right; licensing/authorization/notices/SBOM must precede any P2 action.
