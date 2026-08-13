# BK7258 per-image CMake parity verification

Date: 2026-08-13 (Asia/Shanghai)

Scope: make the repository-owned BK7258 CP/AP board port buildable through the
standard openvela CMake path while preserving the existing Classic Make
contract. Official NuttX/apps and Beken SDK sources were not modified.

## Result

The CMake backend now mirrors the Classic Make source gates, SDK archive
closures, link wrappers and board postbuild behavior for individual CP/AP
images. Four representative configurations configure, link and emit usable
raw/CRC artifacts under CMake 4.0.2.

Signed dual-image assembly and COM3 flashing still run through
`build_dual_image.sh`, whose backend remains Classic Make. The board run below
is therefore a regression of the same source tree and hardware/debug contract;
it is not evidence that a CMake-produced executable was flashed.

## Implementation boundary

- `bk_idk/sdk-bundles.cmake` centralizes SDK version, role and path selection
  and fails configure on an unsupported or incomplete bundle.
- `cmake/Toolchain.cmake` sets the documented CMake 3.5 policy compatibility
  floor only when CMake is 4.0 or newer. This keeps the workaround in the
  board-owned custom toolchain hook rather than patching OpenAMP/libmetal.
- `chip/CMakeLists.txt` now targets NuttX's `arch` library, uses
  `NUTTX_BOARD_DIR`, and mirrors the Classic Make CP/AP/AP-SMP source gates.
  It also mirrors SDK compile definitions, wraps and entry/build-id options.
- The board root CMake file imports the same CP/AP static archive closure,
  forces DVP sensor registrations, supplies the MCUboot include overlay and
  invokes the existing `postbuild.sh` target.
- CMake deliberately relies on upstream `arm_m/CMakeLists.txt` to select
  `arm_systick.c`; it does not duplicate the Classic Make workaround for that
  backend's historical symbol typo. Upstream already omits `arm_vectors.c`
  when `CONFIG_ARCH_HAVE_CUSTOM_VECTORS` is selected.

## CMake build evidence

The task-specific ccache directory was under `/tmp` because the sandbox's
default cache directory is read-only. That environment choice is not a source
change.

| Configuration | ELF bytes | Raw bytes | CRC bytes |
|---|---:|---:|---:|
| `cp_nsh_drivercheck` | 1,772,216 | 245,856 | 261,222 |
| `ap_smp_drivercheck` | 2,141,088 | 241,328 | 256,428 |
| `cp_nsh_drivercheck_mcuboot` | 1,701,020 | 227,676 | 241,910 |
| `ap_smp_drivercheck_mcuboot` | 1,793,716 | 235,800 | 250,546 |

The output directories are workspace-local under `cmake_out/bk7258_*` and
are not repository content. CP builds also emitted their established CP-only
`all-app.bin` postbuild artifact.

For every ELF, `arm-none-eabi-nm` found one definition each of `_vectors`,
`__start` and `systick_initialize`. `readelf` reported the following entry
points:

```text
CP drivercheck          0x020103d5
AP drivercheck          0x0215059d
CP drivercheck MCUboot  0x02010501
AP drivercheck MCUboot  0x0215079d
```

These addresses are within the fixed CP/AP XIP ranges. Successful linking of
the MCUboot drivercheck profiles additionally exercised coordinated PM on CP
and the AP media/DMA2D/scale/JPEG/YUV-H264 closure.

## Classic and host regression

A full Classic Make `cp_nsh_drivercheck + ap_smp_drivercheck` dual build
passed SDK checksum validation, generated-partition checks, factory layout,
SDK partition-wrapper validation and the ELF-checked RPTUN layout:

```text
rsc=264 vring=222/224 carveout=0x7e80 spare=0x4cc0
```

The existing test entry point then passed:

- RPTUN mailbox: `31/31`;
- PM activity: PASS;
- BL1 policy sanitizer binary: PASS;
- `git diff --check`: PASS.

The remaining SDK macro-redefinition and old-header prototype warnings are
present in the established Classic Make build as well; this phase did not
silence them by modifying immutable SDK headers.

## Physical-board regression

Target and routes:

- T5-Board;
- COM3 downloader only;
- P0/P1 SWD to CP at 1 MHz after initial 100 kHz attach;
- RTT0 NSH and RTT1 syslog;
- UART1/COM4 physically off and never opened.

The signed Classic Make pair was `cp_nsh_wifi_rtt_mcuboot +
ap_smp_wifi_mcuboot`, version `18.6.11`, security counter `65`. The COM3 sparse
download wrote only these executable ranges:

| Segment | Physical offset | Length | SHA-256 |
|---|---:|---:|---|
| BL1 | `0x000000` | `0x11000` | `d2ab5bb744b8ea48f97c24ea6650feb2d9371ae023f58175370ed698ef34d818` |
| primary BL2 | `0x51d000` | `0x4000` | `3343ef545833547225da3932dcfcfe0de1fb62cf97c894b4a2f08421912dc9ed` |
| secondary BL2 | `0x53f000` | `0x4000` | same as primary BL2 |
| CP | `0x011000` | `0x10e000` | `3954bee25f1d2c338ae082dd5d029964fc9ba8c7972a8e51f7b664293fd46c13` |
| AP | `0x165000` | `0x40000` | `273de07bf2a3864b01b3e5ae39d311ae03d39cc14f6e25657c90daaefef54902` |

All five writes reported `WriteFlash -> pass`, followed by `Writing Flash OK`
and `All Finished Successfully`. LittleFS, `usr_config`, the calibration tail,
OTP and eFuse were outside the arguments. Canonical download log:
`/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260813-141836`.

J-Link identified STAR over P0/P1, read the BL2 hold word as zero and wrote
only `JLNK` (`0x4a4c4e4b`) to release it. The running target then reported:

```text
VTOR=0x28010800
RTT control block=0x2802b9a0 ("SEGGER RTT")
P0/P1 function=0x22
P0/P1 control=0x00050048 / 0x00050048
```

RTT0 `apctl status` reported AP READY, RPTUN CONNECTED, CPU2
SCHEDULER_ONLINE, both AP CPUs online, and the SMP, affinity, semaphore wake,
semaphore loop and lifecycle checks PASSED with zero errors.

`bkrpmsgtest all 20 30000` passed all six idle/load and 1/64/464-byte runs.
Both AP CPUs completed 20 sends and receives in each run: 240 request/reply
operations, zero errors and unchanged start/spawn/report heap snapshots.
`bkbttest info 15000` passed with a valid non-fallback address, ACL MTU 70
and 20 ACL buffers. Final `apctl health` remained HEALTHY with
fault/recovery/consecutive/last-error values `0/0/0/0`.

Host RTT/J-Link processes were closed after collection and the board was left
running. Temporary private keys were generated with mode `0600` under `/tmp`
only and deleted after the run. No credential or private key is present in
the repository or this record.

## Evidence boundary

- This closes individual-image CMake build/link/postbuild parity for the
  exercised CP/AP configurations.
- The physical run closes Classic Make regression and P0/P1/RTT retention for
  the same source baseline. It does not claim a CMake binary was flashed.
- A future request may add an explicit CMake backend to signed dual-image
  assembly; it must reuse the existing packer and safety gates rather than
  inventing a second flash path.
- CMake success does not replace real-board cache, timing, interrupt or
  cross-core verification. This run covers the retained-service profile, not
  the complete peripheral runtime matrix.
