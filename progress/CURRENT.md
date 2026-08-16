# Current Progress

Last updated: 2026-08-16
Updated by: Codex

## Snapshot

- Canonical upstream baseline is `origin/dev-ai-contest-2026` at
  `54ff505912baf4c23e2515ffa60e6c8df18933b5`.
- The active branch is `feat/bk7258-partition-layout-identity`; its working
  changes are not committed or pushed.
- P0-P9a and the AIDK MCUboot framework baseline are merged. The approved
  27-row historical ledger is now represented by the three retained seed
  profiles (`bl2_mcuboot`, `t5ai_core_cp_base`, `t5ai_core_ap_base`); P9b
  equivalence and hardware authorization remain open.
- A final isolated T5-Board four-role `compile-runtime` completed at the exact
  evidence root recorded below. BL1/BL2/CP/AP each compiled in `COMPILE_ONLY`
  mode; no postbuild, signing/package, Flash, network or COM/J-Link hardware
  action ran.

## Current structural result

- T5AI-Core, T5-Board and AIDK AI Toy now have strict product catalogs, SDK
  set/lock metadata and resource graphs. T5AI-Core bring-up remains raw;
  T5-Board and AIDK bring-up select the common MCUboot A/B chain.
- Every product explicitly binds the canonical partition source, layout ID
  and SHA-256. Resolved IR, role config, build plan, private BL1/BL2/CP/AP
  headers, link checks, packers, `.bkpack` and Flash plans recheck the same
  tuple. Secureboot staging remains an inactive host-reference contract.
- Partition/MTD/Flash-guard composition and its five SDK `--wrap` policies
  live in the logical-board layer. Chip reset uses linker `_vectors`; AP
  lifecycle and radio persistence receive typed board descriptors/callbacks.
  Chip sources do not include board image/partition contracts.
- `bk7258_framework.py execute` remains host-only and dry-run for full
  delivery. Its isolated bridge now exposes the verified four-role
  `compile-runtime` phase; the phase consumes one read-only entity snapshot,
  uses role-private roots and records BL1/BL2/CP/AP artifacts. It reconciles
  boot policy in `COMPILE_ONLY` mode but does not run postbuild, signing,
  packaging or hardware.
- The compatibility builder can materialize hash-bound temporary CP/AP seed
  profiles for all three products, verifies their SDK/layout/product identity,
  and reuses the existing signing, dual-image and `.bkpack` implementations.
- The standard dual-core OpenVela surface is role-qualified
  `vela_nuttx_cp.bin`/`vela_nuttx_ap.bin` plus
  `vela_nuttx_manifest.json`; single-role postbuild retains
  `vela_nuttx.bin`, while `app.bin`/`app1.bin` and CRC intermediates remain
  internal. `firmware.bkpack` is the sole delivery archive; host fixture
  packaging tests cover the alias/member contract only.
- JPEG and temperature validation no longer auto-start from generic chip
  peripheral bring-up; `bkvalidate` starts them explicitly and waits for their
  versioned terminal diagnostics. Other legacy validators still auto-start in
  MIC lower-half or selected-board bring-up, so the published policy is
  explicitly `mixed-legacy`.
- Cross-backend metadata requires role-specific `libarch.a` and selected-board
  `libboard.a`. Classic Make's additional `libboards.a` is backend-internal.

## Verification

- A focused 33-test suite passed for framework, AIDK, T5-Board product and
  validation contracts. It includes rejection of framework `execute --build`.
- The final T5-Board isolated BL1/BL2/CP/AP compile-only build reached
  `runtime-built`; its manifest identity, snapshot/tree gate, 12 command
  results and policy/side-effect boundaries are in
  [the four-role verification](verification/2026-08-16-bk7258-four-role-isolated-build.md).
- The final focused acceptance reported 50 tests passed and `git diff --check`
  passed. The four boot artifacts are recorded as non-runnable and untrusted;
  no signed or bootable package is claimed.
- The isolated executor focused suite passed 13 tests and the framework
  bridge suite passed 16 tests for this runtime contract.
- `BK7258_PROFILE_CHECK_ONLY=YES` passed for all three products and verified
  their generated board/boot/SDK identities.
- The latest root focused acceptance repaired and passed the P0-P8 host
  contract checks, including T5AI-Core, T5-Board and AIDK
  product/config/resource/package-plan coverage.
- Layer ownership, migration metadata, generator, partition, SDK-wrapper and
  RPTUN header checks passed for layout
  `bk7258-v3119-ab-124ebfab37ca1fcd`.
- `bash -n` passed for build and auto-debug entry points; `git diff --check`
  passed.
- Earlier broad/full-link evidence is superseded for this phase by the exact
  four-role evidence above. No final signed package or runnable/trusted boot
  artifact is claimed.
- Detailed evidence is in
  [partition/product framework verification](verification/2026-08-16-bk7258-partition-layout-identity.md).

## Remaining debt

- The isolated executor proves four-role `compile-runtime` in reconciled
  `COMPILE_ONLY` mode and its isolated postbuild emits the canonical aliases
  and manifest. Production signing, package delivery and hardware remain
  `NOT_RUN`; the host-only bkpack fixtures do not constitute a signed package.
- The 27 legacy profiles are historical/cutover ledger rows; P9b must still
  prove resolved config, map, artifact and runtime equivalence for the three
  retained seeds before the migration is accepted as complete.
- The runtime phase builds individual CP/AP roles but does not compose/sign
  the dual pair or emit `.bkpack`; the compatibility shell remains the only
  complete delivery path.
- MIC, AUD, SARADC, TF and other legacy validation auto-start paths have not
  all moved to `bkvalidate`; the descriptor registry is intentionally partial.
- T5AI-Core's current canonical entry is intentionally the raw bring-up mode;
  its existing signed/validation variants still require P9b product-mode
  mapping rather than changing the bring-up product in place.
- AIDK remains schematic/build-only until the common package and first standard
  GPIO binding pass COM9 hardware regression. No post-refactor T5 hardware
  regression or final clean dual signed rebuild exists for this working tree.

## Exact next action

Run a clean product build and then the separately authorized hardware gate.
Signing still requires separate authorization; production package delivery,
P9b, validation migration and hardware verification remain open.

## Boundaries

- Do not stage `bootloader.tmp`, `bl2_crc.bin.json`, generated caches, or the
  unrelated `logs/driver-review-*` and `logs/hardware-debug/` trees.
- Do not Flash hardware, weaken trust preflight, inspect N17, add SDK bytes, or
  mutate a private SDK mirror in this phase.
- Do not claim complete framework migration, concurrent product builds,
  complete validation migration, or final signed-package verification.
