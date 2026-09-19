# BK7258 CP/AP configuration seeds

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
| AIDK AI Toy | `app` | `openvela_ap` | None; product-local `xts` and `drivercheck` configurations retired, source retained |

The SDK pair is selected by the two role profile files, not inferred from the
word "AP": T5AI-Core and T5-Board use `cp + ap`, while AIToyBoard uses
`cp-aidk + ap-aidk`. Install/rebuild and verify the exact pair before building.
The root README contains all three normal build commands and discloses the
unpublished Agent dependency that still prevents claiming clean product reproduction.

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
