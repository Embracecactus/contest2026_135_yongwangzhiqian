# Current Progress

Last updated: 2026-08-19
Updated by: CodeBuddy (agent)

## Snapshot (missing peripheral drivers, 2026-08-19)

Implemented 3 of the 4 peripherals missing from the BK7258 datasheet list
(Ethernet MAC, IrDA, GDMA); Smart Card (SCR) is blocked by incomplete SDK
source. Full dual-image drivercheck build PASS (link + partition layout).

- **Ethernet** `eth0`: `chip/ap/bk7258_eth.c` — NuttX netdev over the SDK
  STM32H7-compatible `HAL_ETH_*` + LAN8742 PHY; supplies board-owned
  `HAL_RCC_GetHCLKFreq/HAL_ETH_MspInit/HAL_ETH_RxCpltCallback/
  xTaskGetTickCount/arm_netinitialize`.
- **IrDA** `/dev/irda0`: `chip/cp/bk7258_irda.c` — **board-owned register-level
  NEC decoder** against `0x458b0000` (SDK IrDA driver uses legacy base
  `0x00802400`, unusable on BK7258). GPIO_25 -> GPIO_DEV_IRDA, INT_SRC_IRDA
  hook, key ring + semaphore + NuttX wdog debounce; no ICU register writes
  needed (SDK ll hooks are empty).
- **GDMA** `/dev/dma0`: `chip/ap/bk7258_dma.c` — mem-to-mem engine over
  `bk_dma_*` with a private channel token (`DMA_DEV_LA`).
- **SCR blocked**: `scr_driver_v1_26.c` depends on `gpio_scr_sel()`/
  `gpio_scr_map_group_t`/`scr_reg.h` which do not exist in the v3.1.1.9 SDK
  tree or upstream v1.6.0; `CONFIG_SCR=y` build would fail.
- Kconfig/CMake/Make.defs/peripherals/platform/board registrations complete;
  verification configs `configs/t5ai_core_{cp,ap}_drivercheck` (AP enables
  CONFIG_NET + workqueue).
- Build: `build_dual_image.sh` with the drivercheck profiles → `setup check
  PASSED`, all partition checks PASS, AP image carries ETH/DMA strings, CP
  image carries IRDA strings.
- Residual risks: Ethernet requires a physical RMII PHY (T5 boards fit none);
  IrDA register-offset layout assumption needs board verify.
- **IrDA host unit tests (2026-08-19)**: `tests/bk7258/modules/ap/
  test_bk7258_irda.c` — 11/11 PASS against the register-level driver
  (NEC decode, repeat SHORT/LONG/HOLD classification, open/close/read/ioctl,
  invalid-frame rejection). New mock infra: patch.py `irda` profile,
  mock_reg32 IRDA window, mock_sdk_irda, mock_wdog, mock_sem.
  Regression: jpeg 68 / yuv 42 / scale_rotate 54 PASS; `test_bk7258_can`
  has 5 pre-existing data-comparison failures (independent mocks).
- See `progress/verification/2026-08-19-bk7258-missing-drivers.md`.

## Snapshot (host unit-test suite, agent: opencode)

- Active branch: `feat/bk7258-aidk-ai-toy`. HEAD `42a81f0`.
- **All 18 test binaries PASS** (`cd tests/bk7258 && make all && for b in
  build/test_*; do ./$b; done`):
  - bl1/bl2: 95 cmocka tests (bl1 43; bl2 security_cnt 12, flash_map 23,
    keys 5, mcuboot_boot 12).
  - boot: 31 cmocka (clock 4, flash 6, libc 6, runtime 9, sha256 6).
  - ap: `jpeg_decoder` 68 + `yuv_h264` 42 + `scale_rotate` 54 cmocka tests,
    0 failures.
  - pm_activity PASS; rptun: core_ap 32 checks, core_cp 47, mbox 31 (0 fails).
- **yuv_h264 pilot done**: true source `bk7258_yuv_h264.c` compiled
  unmodified, `-no-pie`, all encode buffers static (32-bit address
  contract), SDK mock snapshots results at driver-return time.
  Suite mock `framework/mock_sdk_yuv_h264.{h,c}` implements counting nxsem
  (deterministic, suite-driven ISR hook, mock clock 1 kHz tick); 42 tests
  cover init/reject/mailbox/event-state machine/full-pipe/happy path.
  Key driver facts pinned: CHUNK=10*1024, DMA_DEV_H264=40, happy path 4
  blocks/3 re-encodes + final; errors map -EINVAL/-ETIMEDOUT/-ENOSPC/
  -EBUSY/-ENODEV/-EIO; `encode_dma_setup_step_fail` registers with
  `group_setup` (test loop owns init/uninit; `setup_ok` double-init hits
  real -EBUSY).
- **scale_rotate suite done**: true source `bk7258_scale_rotate.c`
  compiled unmodified via `patch.py` profile `scale_rotate` (arm barriers,
  Scale1 threshold reg → `mock_reg32` window 0x480e0000, board-relative
  include → local header + `mock_reg32.h` injection). 54 tests; suite mock
  `framework/mock_sdk_scale_rotate.{h,c}` mirrors hw_scale/rott_driver/
  sys_drv_core_intr_group2*, ISR capture + tickwait-hook drive,
  `mock_sr_set_ret_once` single-shot injection (persistent failure would
  wedge the driver's singleton on partial cleanup). Key driver facts pinned:
  initialize EBUSY nulls `*out` but leaves `initialized` set; uninitialize
  is all-or-nothing (failure leaves the component wedged); rotator route
  disable(2)/enable(1) with 3 disables on route failure; scale1 threshold
  buckets 64/32/16/8 by dst_width &127/&63/&31; dst_width must be ×16.
- **Mock include separation (rptun world vs yuv model)**: `mocks/nuttx/
  {semaphore,clock}.h` + `mocks/mock_sdk.c` stay the original POSIX-sem
  implementations (rptun/jpeg need real blocking); the yuv build selects
  its deterministic variants via `-I mocks/nuttx_yuv` (ahead of `-I
  mocks`) for the 3 yuv TUs only. Mocked nuttx headers are per-TU-visibility
  identical, so no cross-TU type mismatch (rptun mbox uses pthread sems).
- Next: ap drivers can/dma2d/dvp -> jpeg_encoder -> usbhost/usbserial_ch34x
  -> cp (23) -> on-board testsuites (Kconfig `CM_*_TEST` + PROGNAME).

## Snapshot (previous phase, agent: Codex)

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
