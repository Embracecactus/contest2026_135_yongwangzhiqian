# BK7258 Bringup and Physical-Board Layers

- Date: 2026-08-13
- Baseline: `0db960795af878bb4ea1bfd96ec33b91b0af5bbc`
- Branch: `refactor/bk7258-bringup-layers`
- State: implementation, host/build verification and T5-Board hardware
  regression complete; uncommitted

## Conclusion

The shared BK7258 port no longer uses one board-name-agnostic monolithic file
to own mandatory platform startup, application storage registration and
T5-Board attached devices.  Those responsibilities now have stable layers,
and both supported physical variants provide the same board-hook interface.

The configuration boundary applies to all peripherals, not only LED/key:
fixed electrical facts live in the selected physical-board variant, generic
controller behavior stays in `chip/`, and runtime I2C/SPI transaction settings
remain controlled by the NuttX upper half or caller.  This prevents a defconfig
from becoming a second pinout database or globally freezing a shared bus.

The resulting model is **one physical-board description with multiple product
profiles**.  A defconfig chooses a coherent use case (for example debug,
camera or networking); it is not another board and is not expected to enable
every mutually exclusive peripheral at once.

## Layering result

- `bk7258_boot.c`: thin `board_late_initialize()` entry.
- `bk7258_appinit.c`: thin `board_app_initialize()` entry.
- `bk7258_platform.c`: idempotent mandatory SDK, SWD, IPC, PM, AP, PSRAM,
  watchdog and touch lifetime initialization in the previous order.
- `bk7258_bringup.c`: application-facing procfs, DVFS, Flash MTD, LittleFS and
  MCUboot MTD registration.
- `boards/<variant>/src/bk7258_board_bringup.c`: selected-board early and
  attached-device hooks.  T5-Board registers LCD, GT1151 and camera with the
  previous best-effort semantics; its PWM validation retains the previous
  fatal early-gate behavior.  T5AI-Core hooks return `OK`.

Classic Make and CMake both select the same variant-local hook source.  The
Classic source path also appears in `DEPPATH`, so dependency generation works
for a source reached through `VPATH`.

## Peripheral configuration result

- Both board headers declare the same capability macro set.
- T5-Board owns its camera I2C frequency, GT1151 maximum I2C frequency and LCD
  backlight PWM frequency; the board-local LCD timing structures remain the
  canonical panel binding.
- GT1151 rejects a configured frequency above the board binding limit at
  compile time.
- LED/key setup and diagnostics use the selected board's polarity to choose
  inactive output level, input pull, default press edge and user-facing state.
  The standard `/dev/gpioN` reads/writes remain raw GPIO levels.
- Existing T5-Board and T5AI-Core polarity values were not changed.
- `BK7258_I2C_BUS` and `BK7258_SPI_BUS` now select both the SDK controller and
  matching NuttX device minor.  I2C is constrained to controller 0 or 1.
  The previous unused SPI timeout Kconfig value was removed rather than
  pretending the immutable SDK exposes a timeout control that it does not.

Configuration ownership is therefore:

| Property | Owner |
| --- | --- |
| SoC controller availability and wrapper mechanics | shared `chip/` code |
| Fitted devices, fixed wiring, polarity and electrical limits | physical-board variant |
| Enabled feature combination | product defconfig |
| I2C frequency | each NuttX I2C message/caller |
| SPI frequency, mode and word width | NuttX SPI upper half/caller |
| UART baud and frame format | serial configuration and termios |
| PWM waveform, ADC channel and timer duration | runtime client/API |

The official v3.1.1.9 AP/CP `gpio_map.h` tables were also audited.  They show,
among other alternatives, I2C1 on P0/P1, SPI1 and QSPI1 sharing P2-P7, SPI0
groups on P14-P17 or P44-P47, I2C0 on P20/P21, QSPI0 on P22-P27, I2S groups
on P6-P9, P40-P43 or P44-P47, and CAN on P44-P46.  Those overlaps are physical
evidence that the all-enabled drivercheck configuration is compile coverage,
not a runnable product profile.  A holistic conflict/profile cleanup is kept
for the next phase instead of partially changing Kconfig dependencies here.

## Requirement closure

| Requirement | Closure evidence |
| --- | --- |
| Remove the monolithic bringup ownership | Four explicit shared layers plus thin NuttX entry points build and link |
| Preserve mandatory startup semantics | Platform order and idempotent result/lock behavior were retained; Classic and CMake images link/postbuild |
| Keep physical wiring out of shared wrappers | Both variants implement the same early/device hooks and capability contract |
| Apply the model to every peripheral class | The ownership table above covers controller, fitted-device, pinless and runtime-controlled resources |
| Keep bus controls configurable | I2C/SPI unit selection is end-to-end; message frequency and SPI frequency/mode/width stay runtime-controlled |
| Support differing LED/key wiring | Lower half and both diagnostics consume declared polarity, pull and press edge |
| Cover both board variants and build systems | CP/T5AI-Core, AP/T5-Board and AP/T5AI-Core CMake builds plus Classic dual build passed |
| Avoid creating a false release image | Mixed-board drivercheck remains compile coverage only; the flashed image used a physically consistent T5-Board CP/AP pair |

Defconfig/profile deduplication and packer compatibility metadata are not an
unclosed item in this change: they are the explicitly separated next phase.
Mixing that redesign into the bringup/board-layer patch would make the build
and hardware regression boundary ambiguous.

## Verification

The following CMake builds passed with SDK checksum, partition generation,
link and postbuild steps:

- `cp_nsh_drivercheck`: CP with T5AI-Core headers.
- `ap_smp_drivercheck`: AP with T5-Board hook and attached-device sources.
- `ap_smp`: AP with the T5AI-Core hook.

Classic `build_dual_image.sh` passed with
`cp_nsh_drivercheck + ap_smp_drivercheck`, including Tier-1 BL1, CP, AP,
authoritative CP rebuild, dual package, factory-layout and RPTUN-layout output.
This pair is build coverage only: CP defaults to T5AI-Core while AP explicitly
selects T5-Board, so it must not be published as a physical release profile.

A temporary generated CP configuration enabled
`BK7258_GPIO_FOUNDATION_TEST` and `BK7258_GPIO_IRQ_TEST`.  The resulting ELF
contained `bk7258_gpio_foundation_test`, `bk7258_gpio_irq_test`, `bkgpioc0_main`
and `bkgpioirq_main`.  The standard `cp_nsh_drivercheck` configuration was
then restored, confirmed to have both options unset and rebuilt successfully;
no maintained defconfig was modified.

A separate temporary clean CMake configuration selected
`BK7258_I2C_BUS=1` and `BK7258_SPI_BUS=1`.  The generated `.config` contained
both values, and AP disassembly showed unit/minor 1 passed to the SDK-backed
I2C state, `i2c_register()` and `spi_register()`.  The temporary defconfig
edits were then removed and `git diff` confirmed the maintained profile was
unchanged.  A final standard `cp_nsh_drivercheck + ap_smp_drivercheck` Classic
build subsequently passed BL1, CP, AP, dual packaging, factory-layout and
RPTUN-layout checks.

`board/bk7258/tests/run_tests.sh` passed its RPTUN mailbox state-machine
checks (`0/31` failed), BL1 policy executable and PM activity executable.
`git diff --check` passed.

## Hardware candidate and physical regression

The mixed-board drivercheck pair was not reused for hardware.  A separate
signed candidate was built from the physically consistent T5-Board profiles
`cp_nsh_drivercheck_rtt_mcuboot + ap_smp_camera_h264_mcuboot` with SDK
v3.1.1.9, image version `18.6.26` and security counter `80`.  Both profiles
select T5-Board; CP enables P0/P1 SWD, RTT0 console and RTT1 syslog, while AP
includes the camera/H.264 validation path.

Pair signing, BL1 manifests, primary/secondary BL2, partition checks,
factory-layout and RPTUN-layout verification passed.  The sparse artifacts in
`nuttx/bk7258-dual` are:

| Segment | SHA-256 |
| --- | --- |
| BL1 `bl_crc.bin` | `f29e783e725f88109e2dbd74eef1fb699b3d40cd91b616a5744e829f94760448` |
| CP `app_crc_flash.bin` | `f1c6cd3858840854a5f1d4189ad67bdcc17cb6b201b340cc06ac9ab278b84b9f` |
| AP `app1_crc_flash.bin` | `1760b0998387fa9de36b79edfe10f258a5174c6272e9a781c461ed3d4a298abe` |
| each BL2 image | `83b06e7d15990cff0ed4e19e507ff6cbd9fe9ff01150147e6942d2482971a177` |

With owner authorization, the package was sparsely written through COM3 with
`bk7258_auto_debug.sh --flash --sparse-flash --no-console`.  BKFIL reported
`Writing Flash OK` and `{All Finished Successfully}`.  The download log is
`logs/bk7258-auto-debug/20260813-203204`.  The bounded write covered BL1 at
`0x000000`, both BL2 slots at `0x51d000` and `0x53f000`, CP at `0x011000`, and
AP at `0x165000`; LittleFS, `usr_config`, the secondary CP/AP pair and the
calibration tail were preserved.  COM4 was never opened.

P0/P1 SWD then identified probe S/N `20790067`, VTref about `3.29 V`, SW-DP
`0x1be12aeb` and the STAR r1p0 core.  The documented `JLNK` word was written to
the BL2 release mailbox, with no target reset or halt in the accepted run.
RTT control block `0x28014260` became live and supplied all runtime evidence:

- RTT0 contained `NuttShell (NSH)` and accepted `apctl status` through its
  normal down buffer.  It reported AP `READY(2)`, RPTUN `CONNECTED(4)` with
  flags `0x00007f3f`, supervisor `HEALTHY(2)` with reason `NONE(0)`, and CPU2
  `SCHEDULER_ONLINE(8)`, all with `error=0`.
- The shared AP SMP record at `0x2809f280` reported `PASSED(4)`, `error=0`,
  `online_mask=0x3` and `boot_count=1`.
- RTT1 contained live BPSR, AP and DVP/H.264 logs, proving the independent
  syslog transport was active rather than merely registered.
- The T5-Board camera registration flag at `0x28054d48` was `1`.  Its retained
  diagnostic at `0x28054d4c` reported magic `BCAM`, version `1`, size `0x50`,
  state `PASSED(2)`, result `0`, stage `dvfs-after`, one H.264 frame of `20288`
  bytes and checksum `0x63fad9a7`.  DVFS moved `3 -> 6 -> 3`, transition counts
  moved `2 -> 3 -> 4`, active sleep/clock votes were both `1`, and both were
  `0` after teardown.

An exploratory J-Link GDB RTT-server attempt was rejected as an acceptance
path because that tool halts the core on attach and provoked a watchdog
restart.  The final evidence above was reproduced from a fresh BL2 release
using running-target Commander access only.  This is a debugger-tool caveat,
not a firmware failure, and future BK7258 RTT checks should avoid the GDB
server unless the watchdog interaction is explicitly controlled.

## Evidence boundary

- No official NuttX, apps or Beken SDK source was edited.
- No QEMU result is used as physical pin, cache, timing or attached-device
  evidence.
- The physical evidence comes from the signed T5-Board CP/AP pair above;
  mixed-board drivercheck remains build coverage only.
- COM3 was used only by the downloader, COM4 was never opened, and no OTP,
  eFuse, lifecycle or debug-lock operation was performed.
- The refactor preserves existing call order and current board electrical
  values, and its physical runtime gate is now closed.
- Defconfig deduplication, CP/AP board compatibility manifests and packer
  redesign are intentionally deferred to the next phase.
