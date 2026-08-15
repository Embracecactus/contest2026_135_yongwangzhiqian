# Current Progress

Last updated: 2026-08-15
Updated by: Codex (GPT-5.6-Luna MAX delegated implementation)

## Current task

P5 validation-runner metadata and `bkvalidate` skeleton are now implemented on
`refactor/bk7258-platform-v2`; P0 remains the accepted foundation and legacy
profiles/production build behavior remain usable.

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
- The CMake adapter keeps the existing board/chip source tree
  read-only, creates a role-local build/artifact view, forbids a shared
  `.config`, and emits a machine-readable view identity; the Classic
  adapter-isolation feasibility report is explicitly unproven and does not
  alter the legacy builder.
- Catalog inputs live in the existing `board/bk7258/scripts` path.  No new
  config profile, product directory, SDK bytes or production source was added.

Detailed evidence: [P1 resolver verification](verification/2026-08-15-bk7258-platform-v2-p1.md),
with the [P0 freeze verification](verification/2026-08-15-bk7258-platform-v2-p0.md)
as its immutable baseline.

## P2 implementation

- Versioned SDK registry/set/lock metadata is content-addressed by SHA-256 and
  records official, derived and sealed-binary provenance without storing SDK
  bytes.  The lock encodes CP/AP manifest/provenance identities and explicitly
  binds BL2 to no runtime SDK.
- `bk7258_framework.py sdk-verify` validates duplicate-free metadata, tracked
  manifest/provenance paths, source-reproducibility claims, private-mirror
  authorization policy and optional external bundle trees.  Bundle checks
  reject symlinks, special files, missing files and extra files.
- `sdk-import` is verification-only and emits a non-overwriting receipt; there
  is no `--replace`, upload, network access or registry mutation.  AP entries
  with `source_archive=not-provided` remain non-source-reproducible.

Detailed evidence: [P2 SDK registry verification](verification/2026-08-15-bk7258-platform-v2-p2.md).

## P3 implementation

- `config` emits a deterministic role config document and optional defconfig
  text from the resolved product/mode/board/role IR; it never writes the
  frozen `board/bk7258/configs` profiles.
- `build-plan` emits a content-identified, machine-readable plan for separate
  BL1, BL2, CP and AP source views, build roots, artifact roots and `.config`
  paths.  CP/AP SDK rows bind to the P2 lock; BL1 and BL2 use `sdk=null`.
- BL2 is explicitly `minimal-make-inputs` with the existing
  `bootloader/bl2/Makefile` adapter and `fake_nuttx_seed=false`; no fake NuttX
  BL2 seed or real build/sign/flash operation is performed.  The legacy
  `build_dual_image.sh` path remains a compatibility-only, uninvoked adapter.

Detailed evidence: [P3 build-plan verification](verification/2026-08-15-bk7258-platform-v2-p3.md).

## P4 implementation

- Strict ownership and dependency metadata now follows only Architecture ->
  Chip/SoC -> Board; Architecture is upstream NuttX and is not patched here.
  `vendor_common_glue`, `build_adapter` and `migration_pending` are internal
  tags, while the compatibility ledger records files/symbols before any
  per-item migration and creates no layer directory.
- BK7258-intrinsic AP topology/lifecycle, mailbox/IPI/shared memory, RPTUN,
  clock/power and cross-core mechanisms remain Chip-owned.  Board owns pins,
  bindings, external devices, linker/configuration and bring-up; production
  validation auto-start is marked migration_pending toward Board.
- The resource graph resolves paired BL1/BL2/CP/AP roles over download, boot,
  hold, runtime, suspend and restart.  It checks pin/function, devpath/minor,
  IRQ/DMA/clock/power, SDK singleton, mailbox, memory/PSRAM and BOM claims,
  plus temporal handoff preconditions.
- Exactly-one board selection and forbidden fallback are enforced.  SDK
  singleton controllers remain max-one with explicit owner; the graph binds
  SDK IDs to the existing P2 lock and emits metadata only.
- `bk7258_framework.py` now exposes host-only `layer-check`,
  `migration-check`, `resource-check`, and `resource-resolve` compatibility
  commands.  P1-P3 tools remain CMake/Classic adapters over existing build
  semantics; standard artifacts are `libarch.a`, `libboards.a`, and
  `vela_*.bin`, while `.bkpack` is only a later additive vendor extension.
  No public NuttX/common repository change was needed; any future upstream API
  requirement must be recorded as external-upstream-needed.

Detailed evidence: [P4 ownership/resource-graph verification](verification/2026-08-15-bk7258-platform-v2-p4.md).

## P5 implementation

- `bk7258_validation_descriptors.json` defines versioned validation descriptors
  with typed requirements, auto/interactive/fixture/destructive-fault
  categories, lifecycle commands, timeout/status and globally serialized
  resource claims.  The checker enforces `app/hello_app` entrypoints and no
  vendor/chip/board-private calls in the runner core.
- `bkvalidate list`, `run <id>` and `all-compatible` are opt-in through
  `CONFIG_BK7258_BKVALIDATE` and wired through the existing hello-app Kconfig,
  Classic Make and CMake files.  Outcomes are stable JSON; incompatible,
  interactive, fixture, destructive-fault and not-ready descriptors are
  explicit `SKIP` results.
- The checker proves all 27 frozen legacy profiles map to family/resource
  mode/validation suite.  Existing validation and production auto-start remain
  untouched; the new-path migration policy is explicitly `migration_pending`.

Detailed evidence: [P5 validation-runner verification](verification/2026-08-15-bk7258-platform-v2-p5.md).

## Exact next action

Continue with owner review of P5 descriptor coverage and later device-backed
validation; no private SDK mirror mutation or automatic hardware execution is
permitted.

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
