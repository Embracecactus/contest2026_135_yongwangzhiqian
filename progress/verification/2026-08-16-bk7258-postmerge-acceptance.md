# Post-merge acceptance: boot-policy/executor reconciliation and clean build gate

- Date: 2026-08-16
- Owner: CodeBuddy (hy3)
- Branch: `feat/bk7258-postmerge-acceptance` from `origin/dev-ai-contest-2026@56e574c`
- Scope: handoff Step 1 (branch) + Step 2 (policy/executor authority) + Step 3
  gate attempt (T5-Board four-role build). Steps 3b (`prepare-delivery`) and
  Step 4 (AIDK GPIO) are deferred — see blocker below.

## Step 1 — branch and tree equivalence (PASS)

- New branch created from `origin/dev-ai-contest-2026` at
  `56e574caf9b0fd46cd2e8a701b0120b94e51ff9b`.
- `git rev-parse HEAD^{tree}` = `1583e0ddc43aac58ebe2a1da49720809f1c3cd5c`,
  identical to the canonical upstream tree (no source drift).
- The only tracked diff vs upstream is the documentation-only handoff patch
  (`memory/ARCHITECTURE.md`, `memory/decisions/ADR-026-...md`,
  `progress/CURRENT.md`, `progress/verification/2026-08-16-bk7258-four-role-isolated-build.md`).
- Four untracked exclusions preserved untouched:
  `board/bk7258/bootloader/bl2/bl2_crc.bin.json`,
  `board/bk7258/bootloader/bootloader.tmp`, `logs/driver-review-20260812/`,
  `logs/hardware-debug/`. The new handoff doc
  `progress/tasks/2026-08-16-bk7258-codebuddy-handoff.md` is the intended
  entry point and remains untracked as expected.

## Step 2 — boot-policy / isolated-executor authority reconciliation (PASS, tested)

### Contradiction resolved

The three checked-in boot policies declared `metadata_only=true`,
`executor_authoritative=false`, `integration_status=BLOCKED` and their
tests asserted that state, while `bk7258_boot_policy.py` *forced*
`integration_status == "BLOCKED"` at validation and `resolve_policy` hardcoded
`BLOCKED` in its output. Meanwhile `bk7258_isolated_executor.py` implemented
`prepare-delivery` and an explicitly authorized `deliver` but **never consulted**
`integration_status`/`permissions` to gate them — so the BLOCKED declaration was
decorative and any mcuboot product (including AIDK) could run `prepare-delivery`.

### Correction (coherent T5-Board keyless-plan contract; no authority widened)

- Introduced `INTEGRATION_STATUS_KEYLESS_PLAN = "RECONCILED_KEYLESS_PLAN"` and
  allowed only `t5_board_bringup` to adopt it; AIDK and T5AI-Core stay `BLOCKED`.
- `validate_policy` now accepts `BLOCKED` or `RECONCILED_KEYLESS_PLAN`, rejects
  the reconciled status for non-T5 products, and requires the matching
  `blocked_until` (`sign/package-delivery-authorization`).
- `_validate_permissions` gained `prepare_delivery`: `allow` only for the
  reconciled contract, `forbidden` otherwise (AIDK/T5AI-Core fail closed).
- `resolve_policy` now surfaces the real `integration_status` and adds
  `integration.keyless_delivery_plan` instead of forcing `BLOCKED`.
- Added executor gate `_require_keyless_delivery_authorized`, called by
  `prepare_delivery`: only a resolved policy with
  `integration_status == RECONCILED_KEYLESS_PLAN` **and**
  `permissions.prepare_delivery == "allow"` may proceed; every BLOCKED product
  fails closed. This makes the checked-in policy authoritative for the
  transition rather than leaving BLOCKED decorative.
- `bk7258_boot_policy_t5_board_bringup.json` promoted to
  `RECONCILED_KEYLESS_PLAN` with `prepare_delivery: allow`; AIDK/T5AI-Core JSONs
  gained `prepare_delivery: forbidden`; all three `identity_sha256` recomputed.
- Schema `bk7258_boot_policy_schema.json` updated (`integration_status`/`blocked_until`
  enum, `prepare_delivery` permission) so the documented contract matches code.

### Invariants preserved (no authority widened)

- role-isolated compile still gated by explicit `--authorize-compile`;
- keyless `prepare-delivery` prepares/validates a plan and binds the standard
  aliases but grants no sign/package/hardware authority;
- `deliver` still requires `--authorize-sign` + `--authorize-package` + external
  keys + explicit version/security-counter inputs;
- `sign`/`package`/`flash`/`hardware`/`network`/`key_read` remain
  `requires-user-authorization`/`forbidden`/`NOT_RUN`;
- AIDK does not inherit T5 trust or SWD assumptions (stays BLOCKED, SWD disabled);
- wrong product/plan/profile/phase combinations continue to fail closed.

### Tests

- `test_bk7258_boot_policy.py`: 16/16 pass, including new
  `test_reconciled_keyless_plan_is_accepted_only_for_t5_board`,
  `test_reconciled_status_requires_prepare_delivery_allow`,
  `test_blocked_products_keep_delivery_and_hardware_forbidden`.
- `test_bk7258_isolated_executor.py`: 28/28 pass, including new
  `test_require_keyless_delivery_authorized_transition_rules` (positive +
  three negative cases).

## Step 3 — T5-Board four-role build gate (PARTIAL: blocked on absent SDK)

Ran from a fresh external build root under `/tmp` (no repo mutation).

- `prepare` → **PASS**, manifest identity
  `1837c85fdab13e60205692a7660ea9083575530d987c95e2dc619707229a08d6`.
  Live-verifies the reconciled policy is loaded and recorded on the manifest.
- `materialize-sources` → **PASS**, source snapshot
  `f15f4d566e23bb22fd2993298e29c4e85cad86c2625304e5a79075f51084c358`.
- `compile-runtime --authorize-compile` → **BLOCKED** with
  `olddefconfig executable is not available on the safe PATH`. The deeper,
  definitive blocker is the Beken SDK v3.1.1.9 bundle:
  - `bk7258_sdk_registry.json` declares `sdk_bytes_tracked: false` and a
    `private_mirror` with `destination: metadata-only`,
    `redistribution_authorized: false` — only metadata is in-tree.
  - The canonical store `board/bk7258/bk_idk/armino_as_lib/versions` is absent
    and no `*.a` (e.g. `libarch*.a`) exists anywhere in the workspace.
  - The toolchain (`arm-none-eabi-gcc/g++`, `cmake`, `make`) and
    `kconfiglib`/`yaml` Python deps are present; `olddefconfig`
    (kconfiglib script) is not on the safe PATH.

Because `compile-runtime` cannot reach `runtime-built` without the SDK static
libraries, the dependent steps are deferred:

- Step 3b (`prepare-delivery`) requires a `runtime-built` manifest, so it cannot
  run yet. It is gated to fail closed for AIDK/T5AI-Core and will bind
  `vela_nuttx_cp.bin`/`vela_nuttx_ap.bin`/`vela_nuttx_manifest.json` with
  sign/package/hardware/key-read at `NOT_RUN` once the build is available.
- Step 4 (AIDK standard GPIO binding) depends on Steps 2 and 3 passing, so it is
  deferred.

## Boundaries honored

- No `deliver`, signing, packaging, Flash, reset, COM/J-Link, or key read.
- No commit or push (per handoff authority).
- The four known untracked artifacts/logs were left untouched.
- `git diff --check` on the policy/executor/schema changes: clean (no trailing
  whitespace / conflict markers introduced).
