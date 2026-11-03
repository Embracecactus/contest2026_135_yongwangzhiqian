# BK7258 CP/AP configuration seeds

## Current verification status (2026-10-25)

This is the single current board/profile acceptance summary. Product goals
remain in `goal-objective.md` and the
[desktop companion contract](../../docs/platforms/bk7258/shaniu-desktop-companion.md).
Dated verification records retain their original version and evidence scope.

- The reviewed team source is `930d75ae`. Its recorded validation covers
  three boards' CP/AP and unsigned BL1 builds, five SDK profiles, host checks,
  33 Android unit tests and debug/application-test APK assembly. That pass did
  not run physical boards, phone instrumentation, real cloud, signing or
  flashing; it is not a new verified product firmware generation.
- Published dependency identities are owned by the team manifest and
  [source provenance](../../SOURCE_PROVENANCE.md#2026-10-21-评审修复依赖).
  The local NuttX and SDK checkouts have now been synchronized to those pins;
  NuttX HEAD and all 18 source hashes pass the unchanged compatibility gate.
  The [dependency synchronization record](../../docs/verification/bk7258/2026-10-25-dependency-sync.md)
  separates this source check from builds and device acceptance. Never bypass
  `kernel_compat` to build or flash a mismatched checkout.
- AIDK's last identified device generation in the retained feature evidence
  is `0.7.124+770`; this was not read back again on 2026-10-25. Generation 770
  failed automatic reconnection after OTA, and long-term stability remains
  unaccepted. CPU1 heartbeat failures in 755/756/757/759 and a later 770
  `PRIMARY_TIMEOUT` have no established common root cause. Earlier bounded
  healthy windows do not close those failures.

The N1–N4 evidence below comes from the
[2026-10-18 record, including its later sections](../../docs/verification/bk7258/2026-10-18-stability-and-phone-hil.md)
and the separate
[2026-10-20 camera candidate record](../../docs/verification/bk7258/2026-10-20-workbench-camera-lifecycle.md).
It does not establish physical acceptance of `930d75ae`.

| Goal | Existing source / version-bound evidence | Acceptance still missing |
|---|---|---|
| N1 motion, expression, haptics | Single motion worker, 100 ms sampling lease and admission generation; the later 770 session has real sensor samples and the owner's confirmation of an expression change | Correlated pickup/putdown events, human haptic experience and current-candidate device validation |
| N2 scene cards | NFC service and eight-slot protocol; status readback and one no-card content load on 2026-10-18 | Physical-card scene/content triggering and its exception paths |
| N3 computer tasks | The later 770 session preserves a real process exit code 0, a separate timeout cancellation, confirmed device results and corresponding LCD submissions | Full M3 resource workflow and current-candidate acceptance; the earlier session with a missing exit code remains a separate incomplete record |
| N4 on-demand camera | Earlier 15,211 B / 76.6 s transfer; later 770 session decoded 13,575 B at 640×480 with 512 B chunks in 2.240 s frame transfer / 5.257 s total. Real browser capture, cancel and recapture also passed in that earlier feature session | The 2026-10-20 preview/save candidate has host/browser-fixture checks only: both device attempts stopped at TLS before a camera command. Current lifecycle changes, local save on a real device, concurrency, long-term stability and human image-quality review remain unaccepted |

The 2026-10-18 decision still places OTA and post-upgrade reconnection after
the desktop features. Historical 634 and later 767 successes retain their own
scope; neither overrides 770's reconnection failure or validates later source.
The 637/638 voice and 641 resource results remain dated historical evidence,
not the current firmware acceptance label.

## Configuration ownership

Each maintained role seed contains only:

- `defconfig`: one complete openvela role/capability/system-policy seed;
- `profile.conf`: build-tool-only schema, physical board, role, runnable class,
  CP/AP compatibility and SDK profile. NuttX does not consume this file.

Each physical board also owns one `openvela.conf`. It selects that board's
maintained normal CP/AP pair and partition CSV for the short `--board` command.
This is board-owned build metadata, not a NuttX defconfig and not chip policy.
The generic tool contains no physical-board-name table: adding a board adds
its board directory, role seeds and declaration without changing Python.

## Adding another physical board

Use a lowercase underscore name and add these board-owned inputs:

1. `boards/bk7258/<board>/` with the normal physical binding, adjacent
   `Make.defs`/`CMakeLists.txt`, and `CONFIG_BK7258_BOARD_<BOARD>` selector;
2. `configs/app/{defconfig,profile.conf}` and
   `configs/openvela_ap/{defconfig,profile.conf}` with the same board owner and
   CP/AP compatibility value;
3. one strict `openvela.conf` naming those two role seeds, a partition CSV and
   a release-policy CSV below `boards/bk7258/`;
4. a new partition CSV only when no reviewed common layout matches the new
   board's Flash and storage topology.

Partition layout CSVs are maintained centrally in
`boards/bk7258/common/partitions/bk7258/`; a board's `openvela.conf` selects
one of them and a board directory does not keep a private copy of a layout.
The partition CSV remains the only geometry/build-write source.  The release
policy maps every selected partition name to product semantics without copying
an offset, size or Flash capacity.  A new board may reuse a reviewed policy
when every partition name and semantic matches; otherwise it adds its own
small policy CSV.  The existing manifest already projects the whole
`boards/bk7258` directory,
so a physical board does not add another linkfile. Build it with
`bk7258.py build --board <board> --boot direct` (or `mcuboot` plus that
generation's trust inputs). The resulting build manifest carries the board's
resolved role configs and layout into the common signing, package,
materialization and loader-image path; those stages never branch on a board
name. If adding a board requires editing `tools/bk7258`, the descriptor or
layout contract is incomplete and must be reviewed instead of adding a board
special case.

This descriptor-only extension guarantee is intentionally bounded to the
current BK7258 dual-core BL1/BL2, MCUboot A/B product model. Its signed
release contract uses `boot`, `cp`, `ap`, `bl2_a`, `bl2_b`,
`manifest_a`, `manifest_b`, `pair` and a writable `persistent_data`
partition. A board with a different boot chain or update model must introduce
a reviewed product-mode contract; it must not be hidden behind a board-name
branch or forced into the current partition semantics.

This follows the official openvela board-config rule: a board may have more
than one `configs/<purpose>/defconfig`, but each must represent a real core
function and the set should stay small. BK7258 needs two seeds for one normal
system because CP and AP are independently linked NuttX images; they form one
logical base pair, not two product variants.

| Physical board | Normal openvela CP | Normal openvela AP | Additional maintained purpose |
|---|---|---|---|
| T5-Board | `app` | `openvela_ap` | `xts`, `perf` |
| T5AI-Core | `app` | `openvela_ap` | `xts`; paired `drivercheck_cp` / `drivercheck_ap` |
| AIDK AI Toy | `app` | `openvela_ap` | Paired `csi_cp` / `csi_ap` for bounded raw CSI; `app` + `openvela_ap_native_test` for bounded native ELF loader checks; product-local `xts` and `drivercheck` configurations retired, source retained |

`openvela_ap_native_test` is a diagnostic profile. It independently compiles
the fixed breathing example into an ELF fixture. The accepted ownership split
runs all filesystem, RPMsg and ELF lifecycle work on CPU0. One phase executes
the already-loaded import-free callback on CPU1 before CPU0 unloads it. It
also proves that a CPU1 loader attempt is rejected with `-EXDEV` before file
or OpenAMP work. The product native manager applies the same CPU0 gate to
initialize, install, start, step, stop and readback entry points.
The profile neither configures a module publisher nor installs a persistent
feature or starts a remote execution service. It is not a product release
profile; build
success alone does not accept the loader. The normal `openvela_ap` profile
does not launch this automatic test.

Further same-day isolation passed CPU1 thread/yield, CPU0-load/CPU1-call,
and a single CPU1 load/unload. Ordinary CPU1 file I/O also reproduced the
failure, including before any ELF load in the task-mutex comparison; ELF
execution is therefore not required. Frozen RPMsg traces observed premature
loss of ownership of the same `rdev` lock and recursive-count underflow.
The task-mutex comparison also failed and was removed. These are diagnostic
findings, not a fix or permission for CPU1 to perform filesystem or RPMsg
work. A trace-off profile passed 100 CPU0 load/call/unload cycles, plus 100
CPU0-load/CPU1-call/CPU0-unload cycles without a fault or supervisor stall.
The later CPU0-gate candidate additionally rejected the native manager and ELF
loader entry points on CPU1 with `-EXDEV`, then passed the same two 100-cycle
paths. Its serial wrapper reported an evidence-check error only because one
requested expression omitted the intervening `observed=0` field; the complete
capture contains all three PASS records and no configured failure expression.

A separate transport-diagnostic run passed the dedicated-region lock and then
timed out an ordinary internal-SRAM lock while its word read as unlocked. This
is evidence that the lower-layer cross-core lock problem remains intermittent;
it does not establish the exact hardware mechanism. Lock-placement and direct
CPU1 RPMsg probes therefore run only with
`CONFIG_TESTS_BK7258_NATIVE_TRANSPORT`, never in the acceptance or product
profile. No OpenAMP or NuttX upstream source was patched.

Both AP profiles use
`CONFIG_SMP_DEFAULT_CPUSET=0x1`, so ordinary product tasks inherit CPU0 unless
code explicitly opts into CPU1. Failure evidence remains under
`out/native-cpu1-20261102/`; the CPU0-gate acceptance, download logs and normal
boot evidence are under `out/native-cpu0-guard-20261102/`. The restored normal
candidate is `0.24.2+2026110203`, full-image SHA-256
`82a34fcf2e16432f257384ffef167320297b34733d40836ff884162f34d53812`.
Its publisher fingerprint remains intentionally empty, so it does not accept
module trust or installation until an approved first-party public-key
fingerprint is configured; no test signing identity was promoted.

`CONFIG_TESTS_BK7258_NATIVE_TRACE` is an optional, default-off diagnostic.
It instruments the OpenAMP/RPMsg call path and raises SysTick priority to
sample masked waits. It changes timing and may drop ticks: results with it
enabled cannot serve as normal-priority acceptance. The test's ordinary
thread, file and ELF phases remain available without this instrumentation.

The SDK pair is selected by the two role profile files, not inferred from the
word "AP": T5AI-Core and T5-Board use `cp + ap`, while AIToyBoard uses
`cp-aidk + ap-aidk`. Install/rebuild and verify the exact pair before building.
The root README contains all three normal build commands. Agent source
identity is owned by the team manifest and `SOURCE_PROVENANCE.md`.

The AIDK `csi_cp` / `csi_ap` diagnostic pair uses compatibility group
`aidk_ai_toy_csi_v1`, SDK `cp-aidk-csi + ap-aidk`, and the existing fixed-block
partition CSV. It enables CSI on both roles; the AP starts networking without
the voice/media product. Normal board presets are unchanged. The CP SDK variant
keeps the AIDK internal ROSC selection and adds `CONFIG_WIFI_CSI_EN=y`.
This profile is an acquisition experiment, not a claim of presence accuracy or
board acceptance. Its data and experiment contract is
[documented separately](../../docs/chips/bk7258/wifi-csi.md).

T5-Board's Dolphin recording and WAV storage on SD have passed owner-operated
hardware verification; the existing `0.1.0+13` record identifies its image.
Commit `8de0ae78` restores `DOLPHIN_RECORDER=y` and the missing CMake source,
undoing the disablement in `c10a7668`. This standalone recorder uses the existing
NuttX audio upper-half adapter only when `DOLPHIN_RECORDER && BK7258_MIC && !MEDIA`;
CMake, Make and the source guard now agree. It does not enable Agent or shadow
a running Media service. Recorder application and capture function bodies are
unchanged from the earlier Dolphin publication. T5 CP/AP builds, existing host
checks and ELF/map linkage passed; the new build was not flashed or board-tested. See
[the Dolphin record](../../docs/platforms/bk7258/dolphin-master-plan.md).
AIDK's current acceptance summary is maintained at the top of this file;
older firmware records retain their own version boundaries.

Each physical board owns exactly one normal application entry at
`configs/app/defconfig`. It is a complete CP configuration, not a shared
cross-board fragment. `configs/openvela_ap/defconfig` remains the companion
AP image configuration because BK7258 links CP and AP independently.
`openvela.conf` selects this pair for `--board`; no app selection table or
additional config parser exists in the build tool.

T5-Board selects Dolphin; AIDK AI Toy retains Shaniu; T5AI-Core retains its
existing system features. The product application source remains in `app/`,
SoC mechanisms in `chips/`, and populated hardware/wiring in each board.
The remaining T5-Board/T5AI-Core `xts`, `perf` and `drivercheck` profiles are
separate purposes; they are not part of the AIDK Shaniu product build.
Retired duplicate product entries: `openvela_cp`, `dolphin_cp`, `dolphin_ap`.
Generated `.config` files and historical build evidence retain their original
paths; they are not new maintained application entries.

Boot mode is not duplicated in the profile. Every build selects it explicitly:

```bash
tools/bk7258/bk7258.py build \
  --board t5ai_core --boot direct
```

Special-purpose pairs such as xTS, performance measurement and drivercheck
use the explicit `--cp-config`, `--ap-config` and `--partition` form. This
keeps diagnostic selection visible without teaching the tool any board names.

The generated CP/AP config pair, normalized partition identity, role seed,
profile metadata, accepted SDK bundle, locked toolchain and public signing
source all participate in the role build identity.  CMake outputs therefore
live below:

```text
<workspace>/out/bk7258/<board>/<cp>__<ap>/<layout-id>/roles/<boot>/<role>/<build-id>/cmake
```

An incremental build reuses only that exact identity.  `--clean` removes its
CMake binary directory and prunes stale content-addressed siblings for the
same pair, boot mode and role.  It never configures or distcleans the NuttX
source tree, and it does not remove published releases or device bases.

During the OpenVela Make-to-CMake transition, every source or feature-gate
change must be mirrored in the same component's `Make.defs` and
`CMakeLists.txt`.  The chip, shared board and each physical board keep those
pairs adjacent for direct review.

The runnable CP base profiles use the standard OpenVela startup lifecycle:

1. `board_app_initialize()` registers procfs entries and storage devices;
2. `/etc/init.d/rc.sysinit` mounts procfs and the selected system storage;
3. `board_app_finalinitialize()` verifies the ROMFS scripts and mounted
   filesystems;
4. `/etc/init.d/rcS` is the designated place for CP product services and is
   selects enabled board applications (including Dolphin on T5-Board).

Final-init is a diagnostic contract, not a startup gate: the current NuttX
NSH continues to `rcS` even when `BOARDIOC_FINALINIT` returns an error.  Any
future service added to `rcS` must therefore check its own required mounts or
devices before starting.

The T5-Board and T5AI-Core CP XTS profiles retain the normal CP diagnostic
startup baseline.  AIDK AI Toy no longer carries a product-local XTS or
drivercheck profile; its maintained pair is the Shaniu `app` and
`openvela_ap` product configuration.
`t5_board/configs/xts` is also the maintained P0 diagnostic profile: it keeps
AP/RPTUN/Wi-Fi, Trace, watchdog supervision, Backtrace, Allsyms,
IRQ/critical-section/CPU-load monitoring and memory stress together so one
image can reproduce system-level faults.  The separate T5AI-Core
`drivercheck_cp` profile intentionally has no ROMFS startup scripts or
SYSINIT/FINALINIT/RCS marker contract and is not accepted by the XTS pytest
suite.  AP physical peripherals still belong to `bk7258_ap_main()` and the
selected physical-board bindings; CP ROMFS scripts must not initialize
AP-owned LCD, touch, audio, camera or removable storage.

The XTS profile alone registers 64 KiB of the CP role-local PSRAM as a second
NuttX system-heap region (`MM_REGIONS=2`).  This keeps the upstream 16 KiB
testsuites runner stack and supplies the concurrent task/pthread allocations
required by the scheduler suites.  The remaining 64 KiB stays in the CP
private PSRAM heap.  This is a diagnostic capacity policy, not a change to
base CP/AP profiles or to the chip's role-partition ownership.  The
generation 147--149 diagnosis and board evidence are recorded in
[`../../docs/verification/bk7258/2026-08-27-bk7258-p0-xts-completion.md`](../../docs/verification/bk7258/2026-08-27-bk7258-p0-xts-completion.md).

The T5-Board and T5AI-Core `xts` profiles pair with their board's normal
`openvela_ap` profile.  They enable the same linked BK7258 CMocka CP/AP
lifecycle contract; normal product profiles do not carry test applications.
Official pytest drives that CMocka program over the UART0 CP NuttShell,
applies a BK7258-wide boot baseline, then selects additional markers from an
explicit `t5_board` or `t5ai_core` board contract.

`t5_board/configs/perf` is the one narrow measurement-policy exception to the
profile-directory rule below.  It does not introduce another physical-board
or CP/AP ABI boundary: its `profile.conf` remains in compatibility group
`t5_board_openvela_v1`.  A separate seed is necessary because trustworthy timing
requires the opposite policy from diagnostics: fixed SDK-defined CP/CPU0
maximum of 240 MHz and `-O3`, with AP autostart, Wi-Fi, RPTUN, watchdogs, Trace,
Backtrace, Allsyms and scheduler monitors disabled.  The paired AP image is
still packaged for the common layout but is not started while measuring.
Benchmark results are valid only when accompanied by the resolved config hash,
image hash, frequency, command parameters and repeated-run statistics.
The SDK 320M/480M names are shared OPP labels, not CPU0 frequencies; their
CPU0/AP/bus mappings are documented in
[`../../docs/chips/bk7258/sdk-clock-operating-points.md`](../../docs/chips/bk7258/sdk-clock-operating-points.md).
Generation 144 demonstrated why the SDK IRQ bridge remains part of that
minimal contract: polling TX reached NSH, but interrupt-driven UART RX could
not accept commands.  Generation 145 restored only the bridge and completed
CoreMark, Ramspeed and Whetstone in ten independent sessions each.  The exact
config/image identities and results are recorded in
[`../../docs/verification/bk7258/2026-08-27-bk7258-p0-diagnostics-performance.md`](../../docs/verification/bk7258/2026-08-27-bk7258-p0-diagnostics-performance.md).
Generation 146 then selected SDK OPP 240M and completed a fresh signed full
download, stable read-back, cold boot, and another ten independent sessions
for each benchmark.  Its exact identities and 160-to-240 MHz comparison are
recorded in
[`../../docs/verification/bk7258/2026-08-27-bk7258-sdk-clock-240m-validation.md`](../../docs/verification/bk7258/2026-08-27-bk7258-sdk-clock-240m-validation.md).

Every full-flash acceptance run must reuse the compatible, approved installed
BL1 and MCUboot identities, a strictly increasing version/counter, the
Agent partition CSV, and one complete-Flash operator image at address zero.
The accepted base must be one exact full-device readback whose canonical
`package accept-base` evidence binds its hash/size to the board, selected
layout, stable device ID and capture method.  Materialization
preserves `usr_config`, Agent persistent data and device-unique calibration/
network state while resetting only declared transactional state; BK Loader
must not chip-erase.  The resulting recovery is bound to that device and must
not be copied to another board. Ordinary builds and downloads do not authorize
key generation, rotation or deletion.

`--boot mcuboot` derives private build-local defconfigs with
`CONFIG_BK7258_MCUBOOT_IMAGE=y`; it does not require another pair of tracked
configuration directories. It additionally requires explicit BL1 and MCUboot
public PEM files, OpenSSL and a rollback floor.

Persistent storage is a system topology, not an application or board name:

- `CONFIG_BK7258_STORAGE_ONCHIP_PERSISTENT`;
- `CONFIG_BK7258_STORAGE_REMOVABLE_BLOCK`;
- `CONFIG_BK7258_STORAGE_FIXED_BLOCK`.

Exactly one topology across the resolved CP/AP pair must equal the selected
CSV's `STORAGE_TOPOLOGY`. Board bindings expose only electrical capability.
Applications consume the mounted storage service and do not select Flash
geometry, filesystems or cross-core transport.

Do not add product catalogs, generated full configs, boot-mode copies or
arbitrary feature-specific profile directories. Add a persistent seed only
for a real board/role compatibility boundary, or for a reviewed measurement
policy whose required negative configuration cannot coexist with the normal
or diagnostic image.  New measurement exceptions must document their negative
contract and remain in the existing compatibility group unless the ABI really
changes.
