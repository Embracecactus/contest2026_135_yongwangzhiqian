# Current Progress

Last updated: 2026-08-17
Updated by: Codex

## Snapshot

- Active branch: `feat/bk7258-app-config-decouple`.
- Base/HEAD: `origin/dev-ai-contest-2026@34f4a37bbab8e4ed49904812aaa8dc6330391d9a`.
- Task plan:
  [2026-08-16 BK7258 应用配置解耦与框架精简](tasks/2026-08-16-bk7258-app-config-decouple.md).
- Unrelated untracked files preserved; nothing committed or pushed.

## Completed

### P1: App Kconfig / build-registration decoupling (PASS)

- One `CONFIG_BK7258_APP_*` group per NSH command (enable + PROGNAME +
  PRIORITY + STACKSIZE + `depends on`); CMake/Make register only App symbols.
- Legacy driver/test symbols no longer register apps or select BUILTIN.
- Focused test `board/bk7258/tests/test_bk7258_app_config.py`.

### P2: final .config authority (PASS)

- Board catalogs no longer carry console/debug Kconfig facts.
- Validation suite catalog carries resources only (no Kconfig injection).
- `verify_final_config()` + `verify-config` CLI; executor records
  `final_config_sha256`/`config_verification` per role.
- Undefined Kconfig symbols (`BK7258_H264`/`TF`/`TF_WIDTH`/`WIFI`) absent.

### P3: standard build.sh --cmake path (PASS)

- AP seed official CMake build PASS; CP seed official CMake build PASS with a
  clean shared tree (temporary relocation of stale generated files,
  restored).
- menuconfig shows the App menu; dependency-unsatisfied apps invisible.
- App on/off verified in ELF/map; config SHA-256 changes with .config.

### P4: fragment/merge system retired (PASS)

- Fragment catalogs deleted (10 JSON files); product catalogs keep only
  identity/board/role/boot/layout/SDK/trust/package/artifact metadata.
- Framework `resolve` is metadata-only; `config_document` binds a retained
  seed defconfig or a user-supplied final `.config` (`--config-root` with
  `cp.config`/`ap.config`); no Kconfig synthesis remains.
- Isolated executor materializes seed/external configs, compiles through the
  standard CMake/Kconfig path, verifies the final .config, and binds hashes
  into the manifest/package identity.
- Source snapshot now excludes stale shared-tree `nuttx/include/nuttx/config.h`
  (and shared `.config`/`defconfig`/`Make.defs`), fixing RTT/GPIO false
  failures in isolated builds.
- Real isolated four-role compile PASS (t5ai_core_bringup, raw):
  prepare -> materialize-sources -> compile-runtime, manifest
  `runtime-built`, CP/AP final configs verified and artifact-hash matched.

### P5: test/tool ownership cleanup (PARTIAL)

- Tool tests migrated from `board/bk7258/tests/` to `tools/bk7258/tests/`
  (framework, executor, paths, bkpack, container, trust, transport, source
  snapshot, validation, boot policy, app config).
- Chip/Boot C host tests moved to repo-level `tests/bk7258/` (mailbox, BL1
  policy, PM activity, RPTUN core); all C host checks PASS.
- Retired: `test_bk7258_scripts_gate.py` (exact scripts-count one-off) and
  experimental `qemu_mbox_proxy/`.
- Deferred with justification: legacy profile freeze/shadow machinery
  (`verify_legacy_profile_freeze.py`, freeze manifest, legacy ledgers,
  `test_legacy_profile_freeze.py`) still has active consumers in
  `framework_check`/`shadow_parity`/validation descriptors; coordinated
  retirement belongs with P9b profile-cutover work.

## Remaining

- P5 tail: retire legacy freeze/shadow machinery together with P9b.
- P6: final acceptance (three seeds, no overlay/fragment API, package
  identity carries final .config hashes, full clean build).

## Exact next action

Run P5: confirm zero consumers for the legacy freeze/shadow machinery and
retire it, then migrate tool tests under `tools/bk7258/tests/` and run the
final P6 acceptance.

## Boundaries

- No commit/push/PR, no hardware/Flash/COM/J-Link, no SDK import, no private
  keys, no official NuttX/apps tree changes (temporary moves restored).
