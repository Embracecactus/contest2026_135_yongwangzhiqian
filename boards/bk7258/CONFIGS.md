# BK7258 CP/AP configuration seeds

Each maintained role seed contains only:

- `defconfig`: application, physical-board capability and system policy;
- `profile.conf`: schema, physical board, role, runnable class, CP/AP
  compatibility and SDK profile.

Boot mode is not duplicated in the profile. Every build selects it explicitly:

```bash
tools/bk7258/bk7258.py build \
  --cp-config boards/bk7258/t5ai_core/configs/t5ai_core_cp_base \
  --ap-config boards/bk7258/t5ai_core/configs/t5ai_core_ap_base \
  --boot direct \
  --partition boards/bk7258/common/partitions/bk7258/bk7258_ab_onchip_persistent.csv \
  --jobs 8
```

The generated CP/AP config pair, normalized partition identity, role seed,
profile metadata, accepted SDK bundle, locked toolchain and public signing
source all participate in the role build identity.  CMake outputs therefore
live below:

```text
<workspace>/out/bk7258/<cp>__<ap>/<layout-id>/roles/<boot>/<role>/<build-id>/cmake
```

An incremental build reuses only that exact identity.  `--clean` removes only
its CMake binary directory and never configures or distcleans the NuttX source
tree.

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
   currently marker-only.

Final-init is a diagnostic contract, not a startup gate: the current NuttX
NSH continues to `rcS` even when `BOARDIOC_FINALINIT` returns an error.  Any
future service added to `rcS` must therefore check its own required mounts or
devices before starting.

The CP XTS and driver-check profiles intentionally retain their diagnostic
startup baseline.  `t5_board_cp_xts` is also the maintained P0 diagnostic
profile: it keeps AP/RPTUN/Wi-Fi, Trace, watchdog supervision, Backtrace,
Allsyms, IRQ/critical-section/CPU-load monitoring and memory stress together
so one image can reproduce system-level faults.  AP physical peripherals
still belong to `bk7258_ap_main()` and the selected physical-board bindings;
CP ROMFS scripts must not initialize AP-owned LCD, touch, audio, camera or
removable storage.

`t5_board_cp_perf` is the one narrow measurement-policy exception to the
profile-directory rule below.  It does not introduce another physical-board
or CP/AP ABI boundary: its `profile.conf` remains in compatibility group
`t5_board_base_v1`.  A separate seed is necessary because trustworthy timing
requires the opposite policy from diagnostics: fixed maximum board-verified
frequency and `-O3`, with AP autostart, Wi-Fi, RPTUN, watchdogs, Trace,
Backtrace, Allsyms and scheduler monitors disabled.  The paired AP image is
still packaged for the common layout but is not started while measuring.
Benchmark results are valid only when accompanied by the resolved config hash,
image hash, frequency, command parameters and repeated-run statistics.
Generation 144 demonstrated why the SDK IRQ bridge remains part of that
minimal contract: polling TX reached NSH, but interrupt-driven UART RX could
not accept commands.  Generation 145 restored only the bridge and completed
CoreMark, Ramspeed and Whetstone in ten independent sessions each.  The exact
config/image identities and results are recorded in
[`../../progress/verification/2026-08-27-bk7258-p0-diagnostics-performance.md`](../../progress/verification/2026-08-27-bk7258-p0-diagnostics-performance.md).

Every full-flash acceptance run, including a switch between these two
profiles, is a new trust generation.  It must use freshly generated, distinct
BL1 and MCUboot P-256 keypairs, a strictly increasing version/counter, the
Agent partition CSV, and one `0x7fa000` operator image at address zero.  The
materialized image preserves all of `usr_config` and Agent persistent data;
BK Loader must not chip-erase or reach the immutable/calibration tail.  Delete
the temporary private-key directory after package, flash and board evidence
are accepted.

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
