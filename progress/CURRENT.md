# Current Progress

Last updated: 2026-08-20
Updated by: Codex

## Objective

The implementation is nearing completion.  The active phase is a
maintainer-first architecture cleanup: remove obsolete compatibility paths,
reduce duplicated configuration and host tooling, and leave one obvious way
to build and maintain each supported BK7258 product without changing verified
runtime behavior prematurely.

## Repository state

- Repository: `contest2026_135_yongwangzhiqian`
- Branch: `feat/bk7258-ai-toy-vela-claw`
- Implementation checkpoint: `36cea32` (`refactor(bk7258): begin maintenance
  architecture cleanup`)
- Pre-checkpoint product HEAD: `ef9e23f`
- Auxiliary `.worktrees`, `.eq-workspace` and `.jpeg-workspace` checkouts were
  removed; active development is restricted to the primary checkout.
- Existing unrelated untracked logs, boot artifacts and personal helper
  scripts remain owner work and must not be staged, rewritten or deleted by
  the architecture cleanup.

## Material work complete

- Existing product work through `ef9e23f` remains intact: missing peripheral
  drivers, AIDK AI Toy support, Vela-Claw, UART ownership and T5-Board screen
  UI changes are present in the current history.
- The first host-tool redundancy was removed without changing the flash
  format: `bk7258_crc_expand.py` was folded into the canonical
  `bk7258_crc16.py` codec/CLI, and BL2, postbuild, isolated delivery, path
  allowlists and tests now consume the single implementation.
- Project rules now require primary-checkout-only development and use one
  agent by default, with bounded Terra/Luna delegation for independent work.
- CodeGraph was synchronized after deleting the auxiliary workspaces, so old
  worktree copies no longer act as architecture evidence.

## Verification

- `python3 tools/bk7258/tests/test_bk7258_crc16.py`: 5/5 PASS, including
  vendor-byte-exact encoding, CLI vector/size/magic checks, padding and JSON
  manifest output.
- `python3 tools/bk7258/tests/test_bk7258_paths.py`: 27/27 PASS.
- `python3 tools/bk7258/tests/test_bk7258_aidk.py`: 12/12 PASS, including real
  host postbuild and private partition-contract paths.
- `python3 tools/bk7258/tests/test_bk7258_isolated_executor.py`: 28 tests PASS
  with one expected skip when stale local BL1/BL2 ELF artifacts are hidden.
  The ambient 2026-08-19 `bl.elf` lacks current handoff symbols and causes the
  artifact-dependent test to fail if left visible; this predates `36cea32`
  and is not a CRC regression.
- `bash -n board/bk7258/scripts/postbuild.sh`: PASS.
- `git diff --check`: PASS.
- No firmware build, signing, package delivery, hardware or flash operation
  was performed in this cleanup checkpoint.

## Accepted working direction

- Keep one logical `board/bk7258` port with three physical variants:
  `t5ai_core`, `t5_board` and `aidk_ai_toy`.
- Expose one product entry per physical board; internally each product retains
  separate CP and AP role defconfigs.  BL1/MCUboot BL2 and the partition/trust
  contract should be shared when the hardware contract permits it.
- Keep chip mechanics, immutable board electrical facts, role configuration,
  product/application selection and validation configuration as separate
  ownership layers.
- Converge on one human-facing build/verify/package entry while preserving
  algorithm modules and fail-closed trust checks internally.

## Risks and unresolved decisions

- `board/bk7258/configs/README.md` describes three retained seeds, but the
  tracked tree now contains product, drivercheck and two full `*.config`
  pairs.  The source of truth is not yet reconciled.
- The tracked `personal/*.config` pairs cannot be removed yet: owner-created
  untracked `build_package.sh` and the document-stress fragment consume them.
- `build_dual_image.sh` and the framework/isolated executor remain parallel
  orchestration surfaces.  No removal is safe until one accepted product path
  reproduces the required BL1/BL2/CP/AP and package evidence.
- The target common MCUboot boot chain is an architecture direction, not yet a
  claim that all three current board profiles build or boot identically.

## Exact next action

Derive compact CP/AP defconfigs for the three supported board products from
the current verified inputs, migrate the owner helper consumers to those
profiles, and remove full `personal/*.config` files only after consumer and
build verification reach zero ambiguity.  Then select the sole orchestration
entry and retire its compatibility peer in a separate reviewed change.

## Boundaries

- Do not alter official NuttX/apps/SDK sources or SDK static libraries.
- Do not use auxiliary worktrees or historical N17 trust domains.
- Do not flash hardware, read private keys, sign production images, commit,
  push or create a PR without the corresponding current-turn authorization.
- Preserve all unrelated untracked files and logs.
