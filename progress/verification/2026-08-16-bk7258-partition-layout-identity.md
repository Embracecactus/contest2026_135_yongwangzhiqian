# BK7258 partition identity and product-framework shadow verification

> **Superseded/current-state note:** This is a phase-local record from
> 2026-08-16. Subsequent work completed the 27-to-3 profile cutover, the
> four-role compile contract, and the postbuild command alias. In particular,
> the residual statements below about no isolated executor, incomplete cutover
> or pending work describe this phase only; they are not current status. See
> the authoritative [current progress](../CURRENT.md).

- Date and time zone: 2026-08-16, Asia/Shanghai
- Verifier: Codex plus independent read-only subreviews
- Base commit: `54ff505912baf4c23e2515ffa60e6c8df18933b5`
- Branch: `feat/bk7258-partition-layout-identity`
- Environment: host-only WSL2 workspace; no hardware access

## Scope

This phase binds each reviewed product to a partition source, canonical layout
ID and canonical layout SHA-256; propagates that tuple through build, link,
boot, package and Flash planning; and moves SDK partition/MTD composition from
`chip/cp` into the existing logical-board layer. It also removes product image
and partition policy from chip startup, AP lifecycle and radio persistence by
using linker symbols and typed board-provided descriptors/callbacks. The
framework shadow now covers T5AI-Core, T5-Board and AIDK product/SDK/resource
metadata, and JPEG/temperature validation is explicitly command-triggered.

It does not introduce a new source layer, modify frozen configs, flash a board,
or claim an isolated canonical executor, independent multi-layout builds,
complete validation migration or P9b cutover.

## Implemented contract

- All three current products select:

  ```text
  source: board/bk7258/partitions/bk7258/auto_partitions.csv
  id:     bk7258-v3119-ab-124ebfab37ca1fcd
  sha256: 124ebfab37ca1fcd9971c5aba7b9f214f0500df74cdc394c88ec602020732d8a
  ```

- Resolved IR, role config, build plan and package identity include the layout
  ID/SHA. Unsafe, alternate, absolute, traversal and symlink source aliases are
  rejected at the product/package boundary.
- The dual builder resolves the tuple once and passes it to the generator,
  verifiers, BL1, BL2, postbuild, MCUboot pair, dual/factory pack and package
  metadata. Consumers re-read the source and compare the expected tuple. BL1
  Manifest generation also derives and checks the primary/secondary BL2 XIP
  and usable capacity from that same in-memory layout object.
- BL1, BL2, CP and AP receive separate generated contracts under a
  product/layout/role-private build root. Classic Make, CMake, linker
  preprocessing and postbuild derive their include path from that root;
  incomplete tuples and attempts to override the private header path fail.
- BL1/BL2 clean and build run as separate Make invocations. Their object DAGs
  wait for the partition check, and BL2 objects normally depend on the
  generated partition header; an incremental invocation cannot silently keep
  layout-stale objects.
- `firmware.bkpack` embeds the source/ID/SHA and exact plan member hashes and
  sizes. Auto-debug derives no ranges from a repository default; its
  `flash-contract` verifies the container and materialized source tree first.
- `bk7258_sdk_partition.c`, `bk7258_flash_mtd.[ch]` and
  `bk7258_flash_guard.[ch]`, plus the five SDK partition `--wrap` policies, are
  owned by the logical board. BK7258 raw Flash mechanics remain chip-owned.
- CP/AP reset code uses linker `_vectors`. CP AP-lifecycle consumes a validated
  board image descriptor, AP validates the CP-published slot, and radio
  lifecycle consumes board MAC-storage operations. No chip source directly
  includes a board image/partition header.
- Cross-backend artifact metadata requires role-specific `libarch.a` and
  selected vendor `libboard.a`. Classic Make's additional generic
  `libboards.a` is recorded as backend-internal; CMake folds those objects
  into `libboard.a`.
- T5AI-Core, T5-Board and AIDK have strict product catalogs, SDK set/lock
  records and resource graphs. The framework `execute` command is host-only,
  rejects `--build`, and labels the real shell path as a shared compatibility
  adapter with semantic parity unproven.
- JPEG and temperature validators are invoked through `bkvalidate`, not chip
  peripheral auto-start. The descriptor policy is `mixed-legacy` because MIC,
  AUD, SARADC, TF and other frozen validation paths have not all migrated.

## Commands and results

The earlier concentrated partition/package suite passed 54 tests. After the
product/validation shadow additions, the latest focused acceptance passed 33
tests:

```text
python3 -m unittest -v \
  board/bk7258/tests/test_bk7258_framework.py \
  board/bk7258/tests/test_bk7258_aidk.py \
  board/bk7258/tests/test_bk7258_t5_board_product.py \
  board/bk7258/tests/test_bk7258_validation.py
```

The combined evidence includes negative BL1 Manifest checks proving that the active layout
source is re-read and rejected when its expected ID/SHA tuple is incomplete or
mismatched at Manifest generation time. It also checks the active primary and
secondary contracts `(0x024d0000, 0x20000)` / `(0x024f0000, 0x20000)` and the
host-reference staging contracts `(0x02004c00, 0x1ff40)` /
`(0x02024c00, 0x1ff40)`; the staging packer rejects an identity mismatch before
opening image or key inputs.
It also executes the real Classic selector/contract harness and verifies that
command-line attempts to substitute the tracked partition header are ignored.
The latest acceptance additionally rejects framework `execute --build`, runs
all three products through `BK7258_PROFILE_CHECK_ONLY=YES`, and includes all
three products in the 11-step `framework-check`.

The explicit layout tuple passed:

```text
gen_bk7258_partitions.py --input ... --expect-layout-id ... \
  --expect-layout-sha256 ... --check
verify_bk7258_partitions.py --input ... --expect-layout-id ... \
  --expect-layout-sha256 ...
verify_bk7258_sdk_partition_wrapper.py --input ... \
  --expect-layout-id ... --expect-layout-sha256 ...
verify_bk7258_rptun_layout.py --headers-only --input ... \
  --expect-layout-id ... --expect-layout-sha256 ...
```

Reported results were partition generation PASS, partition verification PASS
with seven negative cases, SDK wrapper host PASS with a dynamic-layout case,
and RPTUN headers verification PASS. `bash -n` passed for the dual builder,
postbuild and auto-debug scripts. `git diff --check` passed.

During the source ownership move, one clean Classic compilation compiled the
wrapper, MTD and guard into `arch/arm/src/board/libboard.a`; they were absent
from `staging/libarch.a`. The later final link stopped at the unrelated
pre-existing `apctl_main` configuration gap, so no new firmware artifact or
hardware result is claimed here.

A direct CMake minimal CP build completed configure, compilation, link and
postbuild with its private partition contract. Targeted compiler checks also
covered the RPTUN-enabled AP lifecycle changes and the board-injected radio
storage path. These checks do not replace a final signed dual-image rebuild.

## Residual risks

- Partition header inputs are product/role-private, but the real compatibility
  build still shares the NuttX source/config tree. The framework has no
  canonical executor that isolates all role outputs and SDK views.
- The validation registry is partial: JPEG/temperature are command-driven,
  while MIC/AUD/SARADC/TF and other frozen paths still auto-start.
- All 27 profiles remain `MIGRATION_PENDING`; P9b and a unified CMake
  signed-dual/package executor are outside this phase.
- T5AI-Core intentionally remains the raw bring-up product; its existing
  signed/validation variants still require P9b product-mode mapping.
- Automated Flash now requires the verified MCUboot package contract. Legacy
  raw directory packages are not download-authorized by this path.
- No full clean dual-image build was rerun after the final metadata-only
  archive correction. No COM, J-Link, reset, Flash or runtime validation ran.

## Evidence locations

- Product/framework contract: `board/bk7258/scripts/bk7258_framework.py`
- Active layout source: `board/bk7258/partitions/bk7258/auto_partitions.csv`
- Build propagation: `board/bk7258/scripts/build_dual_image.sh`
- Package/Flash contract: `board/bk7258/scripts/bk7258_bkpack.py` and
  `board/bk7258/scripts/bk7258_auto_debug.sh`
- Ownership metadata: `board/bk7258/scripts/bk7258_layer_ownership.json` and
  `board/bk7258/scripts/bk7258_compatibility_migration_ledger.json`
