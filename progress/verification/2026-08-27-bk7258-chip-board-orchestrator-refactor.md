# BK7258 chip/board orchestrator refactor verification

- Date: 2026-08-27 to 2026-08-28
- Baseline: `ae83523` (`origin/dev-ai-contest-2026`)
- State: source refactor, host/header verification, the maintained target-build
  matrix, fresh-key production/xTS full downloads, xTS execution and final
  generation-156 production cold/status hardware acceptance are complete

## Ownership after the refactor

The former board-owned `bk7258_platform.c` was split by capability instead of
moved as a whole:

- `chips/bk7258/cp/bk7258_cp_platform.c` owns the CP one-shot initialization
  graph. `chips/bk7258/ap/bk7258_ap_platform.c` owns the AP-local preparation
  graph. Both use the typed, data-only stage model in
  `chips/bk7258/common/bk7258_stage_runner.c`.
- Chip code owns SDK/IPC, PM, temperature, PSRAM, AP supervision, watchdog,
  raw Flash access, boot-slot remap, OTA mechanics, Wi-Fi control, typed system
  reset and raw BK7258 reset-source decoding.
- `boards/bk7258/common/src/bk7258_boot.c` keeps the NuttX board API
  `board_late_initialize()`, but is now only a role-aware bridge to the chip
  orchestrator. It does not contain SoC initialization policy.
- Board code owns the immutable storage binding (partition geometry and five
  Flash guards), OTA trial/product policy, reset-cause-to-`BOARDIOC` mapping,
  ROMFS/final bring-up and physical-board electrical bindings.

This boundary follows the openvela
[new-platform adaptation guide (id=1443)](https://doc.openvela.com/document?id=1443&version=dev-ai-contest-2026&language=cn):
architecture/chip code supplies SoC capabilities, while the board layer keeps
board entry points and board-specific resources. Public cross-layer contracts
are exposed through normal chip/board include paths; no new deep relative
header forwarding was introduced.

## Initialization and failure semantics

The common runner records the first mandatory failure, skips stages whose
declared prerequisites did not complete, still permits explicitly independent
always-run cleanup/safety stages, and caches the terminal result. A role has one
typed leaf executor, so stage descriptors contain no untyped function-pointer
casts.

CP host coverage includes ten dependency/failure scenarios. In particular,
an invalid OTA layout does not suppress reset-marker validation or watchdog
setup; a reset-marker-policy failure does suppress the watchdog; and a later
success cannot overwrite the first mandatory error. A dedicated legacy-leaf
scenario also proves that SDK failure does not suppress the independently
attempted SWD route, IRDA, GPIO fallback, touch/board-device and final-debug
leaves. This preserves the old unconditional call structure without reviving
the old `apret` error-overwrite bug.

AP timing remains compatible with the pre-refactor boot chain:

1. `board_late_initialize()` runs before the initial application and prepares
   SDK runtime, PM, temperature and PSRAM in that order.
2. PSRAM remains independently eligible when an earlier SDK/PM stage fails,
   matching the old unconditional call structure.
3. AP main validates the cached result before adding the system heap.
4. The post-RPTUN PM call remains an idempotent compatibility step.

The AP host test compiles the real board late hook and covers four success and
failure paths, including cached-result consumption rather than a second
initialization attempt.

## Storage and reset contracts

Board and chip now share one immutable versioned
`bk7258_board_storage_binding_s`. Board code supplies OTA layout, reset-marker
geometry and five non-overlap guards (stage primary/secondary, confirm
primary/secondary and reset marker). Chip code validates and lazily publishes
the geometry and performs raw Flash operations. Binding the same object is
idempotent; replacing it is rejected with `-EALREADY`. The former parallel OTA
and reset-marker backend contracts were removed.

WDT pretimeout persistence uses the dedicated `reset_marker` erase sector. A
marker is written only from the generation/time-revalidated task-context
confirmed-pretimeout path. Marker format version 2 rejects legacy arm-time
records. PMU `POWERON` and `REBOOT` remain authoritative; confirmed WDT marker
evidence only corroborates a WDT source or resolves an otherwise unknown raw
source.

Raw Flash controller ownership is now also one-way. Board MTD and the SDK
partition compatibility wrappers keep layout, permissions and NuttX device
composition, but call the public chip Flash service. The chip service owns SDK
driver initialization, the four accepted 8-MiB JEDEC identities, controller
serialization, range/alignment checks and the unprotect/operation/restore
lifecycle. The SYS_NET sector-preserving MAC update and Bluetooth controller
startup use that same service; no board source calls the raw SDK Flash driver.
The board partition wrapper includes `driver/flash_types.h` explicitly for the
SDK error-code ABI instead of depending on `driver/flash.h` transitively.

OTA health confirmation has the same one-way policy boundary. The board trial
policy passes its configured freshness limit as an explicit argument; chip
code revalidates the generation-bound Supervisor evidence twice under the
Flash guard, immediately before each AP/CP trailer mutation. Chip sources have
no direct dependency on `CONFIG_BK7258_OTA_AUTO_CONFIRM` or
`CONFIG_BK7258_OTA_TRIAL_HEALTH_MAX_AGE_MS`. With OTA enabled but no board
auto-confirm callback, the CP after-AP stage is an explicit successful no-op.

## Verification completed

- `tests/bk7258/run_tests.sh`: PASS with final marker
  `BK7258_HOST_TEST_PASS`.
- Header self-containment audit: PASS for 86 public/private boundary headers
  under C11 and C++17 in default, CP and AP modes with
  `-Wall -Wextra -Werror`; include guards are unique and mock forwarding does
  not use relative `../` chip-header paths.
- Focused stage, CP/AP platform, storage-binding, reset-marker, reset-cause,
  typed-reset, boot-slot, raw-Flash and OTA contract suites: PASS. The OTA
  health-confirm test compiles the real chip implementation both with and
  without the AP Supervisor: the no-Supervisor path fails closed without
  storage side effects, while the Supervisor path proves the caller's 1,234-ms
  limit reaches both revalidations and a second-check failure prevents the CP
  trailer write. The raw-Flash
  test covers all four accepted JEDEC IDs, repeated initialization, bounds and
  alignment, lock failures, operation failures and protection restoration.
  New/refactored focused sources are compiled with `-Werror`. Some legacy host
  fixtures and upstream target sources still emit their pre-existing warnings;
  no blanket warning-free claim is made.
- Locked compiler: Arm GNU Toolchain 10.3-2021.10, GCC 10.3.1.
- Clean CP/AP target builds: PASS for T5AI-Core base (on-chip persistent), AIDK
  AI Toy base (fixed block), T5-Board production base (agent partition) and
  T5-Board xTS. The T5-Board production and xTS runs also built BL1 and BL2
  successfully. An initial target compile exposed the SDK error-code header's
  missing transitive include; after the explicit type-header correction all
  four combinations were clean-built again from the final sources.
- Final production identities: layout `bk7258-8e503fff4dcc50f0`, CP role
  `0ebd81f07cb40d04` / config `91792e9f...`, AP role
  `8a73ea8974de9c3c` / config `149736eb...`; CP/AP raw sizes are 899,704 and
  1,045,408 bytes. BL1 size is 65,376 bytes; BL2 raw/copy sizes are
  13,700/13,728 bytes.
- Every full download used a newly generated, independent BL1/MCUboot P-256
  pair and a strictly increasing version/counter: production generation 152,
  xTS generation 153 and final production generation 154. Their full-package
  SHA-256 values are respectively
  `adb82349d6eb1be408416015f9037daaf64a6f460ab5f79dd18a66b5ed45a3f7`,
  `9668f1f901edc30a90c3f9af3ec87626daa84482f70f9b2df17033f3afb31f22`
  and
  `ac5e2fff3a7a08605789ddf4fe6f2d83f6504340deca70649353f0840d97bc8c`.
  Structure, public BL1/BL2/CP/AP trust and the selected-layout Flash contract
  pass for all three packages.
- The final generation-154 trust-manifest fingerprints are
  `2e83d58307e0ed79d8da1a371600145fcb7d9d21ba717c493b55745d22c83eac`
  for BL1 and
  `398f0c3889dd0dda8501580338f01ac8aa3af18eae21f7beb3b10cbd51c44de6`
  for MCUboot. No private path or key material is recorded; all three protected
  temporary private-key directories were deleted after their acceptance use.
- The tracked repository-root file count remains 6 before and after the
  refactor, and there is no root-level status entry. No script file was added;
  the existing Python build entry was corrected to pass the official
  `build.sh` preflight the same configuration path consumed by `lunch()`.

The deleted board implementation layers are the monolithic platform file, the
parallel boot-slot implementation, four board-owned OTA mechanism C units and
their two private headers, and the board-owned Wi-Fi control implementation.
They are replaced by chip capability modules and one immutable board storage
binding; there is no compatibility forwarding layer or second source of
layout, profile, SDK-version or build-policy truth.

The manifest-pinned SDK archive was not present in this checkout. Target
verification used the locally prepared SDK trees rebuilt from the manifest
source commit, then restored the tracked expected SDK pins. The resulting
package is valid build/signature evidence, but installing the official prepared
SDK bundle remains necessary for a byte-for-byte reproducible packaging
environment.

## xTS capacity resolution and acceptance

The first xTS build correctly failed closed: its 1,308,844-byte CP raw image was
6,316 bytes above the safe signed-content threshold. Map attribution showed
that the excess came from three diagnostic-profile operator/sample frontends,
not from the chip mechanisms or this ownership split. The xTS defconfig now
disables only `BK7258_APP_OTA`, `BK7258_APP_WIFI` and `EXAMPLES_GPIO`.
Production keeps all three commands; xTS keeps `BK7258_OTA`, OTA RPMsg and
auto-confirm, Wi-Fi VNET, GPIO lower-half, `ALLSYMS` and scheduler backtraces.

The clean xTS CP raw image is 1,298,008 bytes. Generation 153 then signed and
packaged successfully with about 3.7 KiB remaining before the protected
trailer reserve. One `0x7fa000` BK Loader input (SHA-256
`86c28a9096ba14b7d9ca46ebb16eeb0be3ea4990c8466cf484e96f123973e811`)
was written from address zero. Cold boot and the pressure-test cold reboot both
passed with `BPSR SYSTEM HEAP PASS size=65536`, full CP/AP handoff and Agent
ready. Cmocka MM 8/8 and scheduler 16/16, getprime, allocator test, 4-KiB
ramtest, tmpfs scanftest 164/0, hello/FIFO/pipe and complete NuttX `ostest`
(`ostest_main: Exiting with status 0`) all passed without Fault/ASSERT/panic.

## Hardware acceptance completed

- Generations 152, 153 and 154 each used exactly one BK Loader input at address
  zero, length `0x7fa000`; each log contains one erase, one write, Flash
  reprotection, `Writing Flash OK` and `{All Finished Successfully}`. No chip
  erase was used and the immutable/calibration tail beginning at `0x7fa000`,
  OTP/eFuse, lifecycle and debug-lock state were outside every write.
- The generation-154 production operator SHA-256 is
  `7b12a3bcf9ebce1ee36048b14bb07da465285f7ec8a8ea698a70e55678936fe0`.
  Materialization verified nine authorized writes and byte-identical
  `usr_config`, `reset_marker` and full `persistent_data` ranges against the
  accepted generation-153 base. The owner explicitly declined Flash readback;
  protected-range evidence therefore comes from the signed contract,
  materializer comparisons, exact operator length and loader write log rather
  than a post-write read.
- Final physical cold boot passed BL1 primary, BL2 RAM execution, AP release and
  handoff, CP boot, SYSINIT, FINALINIT, RCS and Agent readiness. Runtime reports
  confirmed CP/AP pair `18.6.98+154` / counter 154; AP, CPU2, RPTUN and the AP
  supervisor are healthy with error/fault count zero. Wi-Fi status RPC returns
  success (link remains down because credentials are intentionally absent),
  `/dev/watchdog0`, `/dev/rptun/ap` and `/dev/rpmsg/ap` exist, and the CP PSRAM
  heap remains available.
- `bkota reboot` exercised the CP whole-device watchdog. The complete signed
  boot chain and Agent recovered, and a subsequent status query again reported
  generation 154, healthy AP/CPU2/RPTUN/supervisor and successful Wi-Fi control.

## Final strict separation and acceptance pass (generations 155-156)

After the generation-154 acceptance, the final official/NuttX-aligned audit
removed every remaining chip-Kconfig dependency on board capability symbols
and moved physical SWD/UART route policy to board Kconfig.  Static gates find
no chip include or reference to `<arch/board>`, a physical board name,
`BK7258_BOARD_HAS_*`, a board path or a deep relative include.  Board sources
use public `<arch/chip/...>` ABIs and do not include chip-private
`ap/cp/common/bootloader` headers.  Standard NuttX `spiNselect/status` remains
the sole compile-time board hook used by the generic SPI lower half.  No
upstream NuttX or apps tracked file is modified.

Generation 155 used fresh, independent BL1 and MCUboot P-256 roots.  Its
public SPKI SHA-256 values are
`3ec9d6616c323b266537357b46d679a4ed9e3c41509540a52ecd93a7dbdf1364`
and
`077aeca08c7563046db057d776135b90376a9b8ab556e33e5b72445d54ecda51`.
The `18.6.98+155` full package SHA-256 is
`42c534e8a4c733db005f8770b037443dae12bc655906ec2d3eafa3d7dc4c9b49`;
the one-file `0x7fa000` operator SHA-256 is
`9060e09c5bf73b59edaea60fd7cfc720b0571be588189f88650273da45110f31`.
Package structure, public BL1/BL2/CP/AP trust, Flash contract and
materialization all passed.  BK Loader accepted exactly one address-zero
input and reported erase/write/reprotect/final success.  Cold boot passed BL1,
BL2, AP handoff, CP, SYSINIT, FINALINIT, RCS and Agent readiness.  The minimal
current-source xTS gate passed Cmocka MM 8/8 and scheduler 16/16; watchdog,
RPTUN/RPMsg nodes and the CP PSRAM heap also passed.  A production-only
`bkota`/`bkwifi` probe was intentionally retained as failed evidence because
those command frontends are disabled in the size-bounded xTS profile; no
source or profile was changed to make that invalid probe pass.

Generation 156 then used a second fresh, independent root pair.  Its public
SPKI SHA-256 values are
`ec2c3d51605e986a2aa803d115ac91ad30e48189e631496a4f372bd00dd89755`
and
`97d004fd6cbe749dc2b90ef4a69aa3ae42f2c4f24ceacb952fbfeece9aadbe9b`.
The clean production identities are CP
`bk7258-role-3a2e7bd6c4a91c8a` / config
`695b7b309f388f96448e0a2d1dcd842e0be62d4d86bf7ba8a3ab464ea88b8913`
and AP `bk7258-role-d96ba6dcee74f0ce` / config
`eef9a64469a800de8566308f157dd0b5a835d6aa40d804abd0169c0d16155772`.
The `18.6.98+156` full package SHA-256 is
`dfdb0cea45a4c8dcaf0fc0dd696490bef859bb7fa7eb04898ccecb9d0e364e73`;
the one-file operator is exactly `0x7fa000` bytes with SHA-256
`32e638e9f346d017c5f8dfeea62633963979a5580613e108cb1df1e4a6e2fbec`.
The same package/trust/contract/materialization gates and BK Loader success
markers passed.  Cold boot again passed the complete chain without
Fault/ASSERT/panic.  Runtime reports the confirmed pair `18.6.98+156` /
counter 156, healthy AP/CPU2/RPTUN/supervisor with zero errors/faults,
successful Wi-Fi control, watchdog/RPTUN/RPMsg nodes and CP PSRAM.

Across generation 154 to 155 to 156, `usr_config`, `reset_marker` and all
`persistent_data` bytes compare identical.  Every operator ends at
`0x7fa000`; `[0x7fa000,0x800000)`, OTP/eFuse, lifecycle and debug-lock state
were never input to the loader.  Both new temporary private-key directories,
their extracted package work directories and Windows staging images were
deleted after acceptance; only public fingerprints, signed artifacts and
UART/loader evidence remain.
