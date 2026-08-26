# BK7258 physical-board variants

`chips/bk7258` owns the shared BK7258 CP/AP, boot-chain and SDK-wrapper.
`boards/bk7258/common` owns shared board, partition and linker integration;
the three sibling board directories contain only physical-PCB wiring and
capability facts.

## Naming

Stable directory and Kconfig names identify the board product, not a PCB
revision:

| Physical board | Directory | Kconfig | Documented revision |
|---|---|---|---|
| T5AI-Core | `t5ai_core` | `CONFIG_BK7258_BOARD_T5AI_CORE` | V1.0.1 |
| T5-Board | `t5_board` | `CONFIG_BK7258_BOARD_T5_BOARD` | V1.0.2 |
| AIDK AI Toy | `aidk_ai_toy` | `CONFIG_BK7258_BOARD_AIDK_AI_TOY` | schematic-only |

Hardware revisions live in `BK7258_BOARD_HARDWARE_VERSION`.  A revision gets
its own selector only if it changes a software-visible electrical contract.
T5AI-Core is the default so all existing configurations retain their verified
board behavior.

## Source boundary

The current pin facts come from these schematics supplied by the project
owner:

- `T5AI-Core_V101-SCH-a69f7b5a91b4bf21a39bdb7c17812373.pdf`
- `T5-Board_V102_SCH250617.pdf`

TuyaOpen's `TUYA_T5AI_CORE` confirms the Core board's P9 LED, P29 key and P39
speaker-control naming.  TuyaOpen's `TUYA_T5AI_EVB` is not electrically
equivalent to T5-Board V1.0.2 and is not used as a pin source.

## Board-level mapping

| Function | T5AI-Core V1.0.1 | T5-Board V1.0.2 |
|---|---:|---:|
| User LED | P9, active high | P1, active high |
| User key | P29, active low | P12 (`ADC_KEY`), active low |
| Speaker control | P39 | P28 (`SPK_CTL`) |
| Battery ADC | P28 | not fitted |
| Charge detect | P38 | not fitted |
| On-board microphone | MIC1 on MICP1/MICN1 (mono) | MIC1 on MICP1/MICN1 + MIC2 on MICP2/MICN2 (stereo) |
| TF card | not fitted | CLK P2, CMD P3, D0 P4, D1 P5, D2 P10, D3 P11; P6 CD label has no verified edge |
| RGB LCD connector | not fitted | fitted |
| DVP camera connector | not fitted | fitted |

The Core board is the broad hardware-verified baseline.  T5-Board entries are
promoted from schematic evidence only when their peripheral record captures a
real-board result; the TF record, for example, rejects P6 card detect after
inserted/removed level sampling.  Variant selection does not automatically
enable every fitted peripheral; Kconfig still controls driver ownership and
pin-compatible profiles.

The AIDK AI Toy binding is a minimal, no-device bring-up target.  Its only
documented board binding is UART0 at 115200 8N1 for console/download, with
flow-control, SWD, boot hold, RTT and RTS/DTR reset disabled.  COM/USB port
identity is dynamic transport metadata and is not a board or product identity.
The schematic records possible P20/P21 SC7A20-vs-SWD, P0/P1 MFRC522-vs-CN1
UART, P8/P9 32-kHz-vs-KEY3/motor and USB0 conflicts; no unknown route or BOM
peripheral is enabled or claimed.

## Peripheral configuration boundary

The physical-board directory is the owner of every fixed electrical fact, not
only LED and key GPIOs.  This includes fitted-device capability, pin routes,
polarity, pull/drive policy, bus instance, device address or chip select,
board-device frequency limits, LCD timing, SD-card presence policy and mutually
exclusive connector routes.  A value may live in `bk7258_board_config.h` or in
a board-local binding structure when it is used only by that binding.

The shared `chip/` wrappers own BK7258 controller mechanics and the NuttX
lower-half contract.  They must not describe a T5-Board connector or attached
part.  In particular, the generic I2C wrapper applies each message's
`frequency`, and the generic SPI wrapper applies the upper half's frequency,
mode and word width.  Those runtime transaction values are not global board
constants.  Only a fixed device such as the GT1151 or camera supplies a
board-device default or maximum through its selected-board binding.

`bk7258_peripherals_initialize()` may initialize enabled generic controllers.
The selected board's `bk7258_board_early_initialize()` and
`bk7258_board_devices_initialize()` hooks own attached-device registration and
its ordering relative to those controllers.  A new physical board therefore
adds its own header and hook implementation; it does not add board-name tests
or pin literals to the shared chip orchestration.

The rule is applied by peripheral class as follows:

| Peripheral class | Configuration owner |
|---|---|
| UART, hardware I2C/SPI/I2S, PWM, ADC and timer controllers | Kconfig selects an SoC unit/channel and initial policy; standard NuttX calls control baud, message frequency, SPI mode/width, PWM waveform, sample channel or timeout at runtime |
| LCD, touch, camera, SD card and other fitted devices | Selected-board header and binding own pins, polarity, bus attachment, address/CS, limits and registration |
| CAN and QSPI fixed mux groups | Shared wrapper owns the SoC-fixed route; the selected product profile must choose a conflict-free owner before exposing a connector device |
| RTC, TRNG, DMA and media accelerators | Chip-level resources with no physical-board pin database |
| On-board analog microphone | Selected-board header fixes whether MIC1 only or MIC1+MIC2 is fitted; Kconfig supplies default sample rate/gains/buffering, and the NuttX audio application negotiates a supported stream rate/channel count at runtime |

A defconfig is therefore a product feature profile, not a board description.
Several profiles may select the same board; another board may select an
equivalent feature set without duplicating that board's electrical database.
The retained profiles and their CP/AP compatibility groups are documented in
[`CONFIGS.md`](CONFIGS.md).

## Time ownership and persistence

`CONFIG_BK7258_RTC` exposes the SDK AON free-running counter to the AP as the
NuttX system RTC and `/dev/rtc0`.  The counter uses the SDK-selected 32-kHz
clock source and does not need an RTC battery while the SoC remains powered.
The T5-Board has no fitted battery, and the available board evidence does not
establish a separate backup-power domain on any of the three variants.

The calendar value is deliberately an AP-RAM offset from the AON counter.
Before a trusted UTC source calls `clock_settime()` or `settimeofday()`, it is
seeded from `CONFIG_START_YEAR`, `CONFIG_START_MONTH` and `CONFIG_START_DAY`.
The offset is not retained across an AP restart or complete power loss; adding
a battery alone does not make this software representation persistent.  A
future persistent UTC owner must use a CP service across RPMsg or synchronize
from the network after connectivity is available.

Timezone conversion is presentation policy rather than RTC state.  The
T5-Board UI AP enables `CONFIG_LIBC_LOCALTIME` and sets the POSIX timezone to
`CST-8` (UTC+8); CP and non-display roles remain on UTC.  This fixed POSIX
string does not require a zoneinfo image.  IANA names such as `Asia/Shanghai`
would additionally require a mounted zoneinfo database.

## System-log ownership and safety

The maintained no-console AP base profiles send their NuttX syslog stream to
the paired CP over the existing RPTUN/RPMsg link.  CP is the RPMsg syslog
server and writes both local and received records through its early
`up_putc()` channel to the board-owned UART0.  NuttX initializes both RPMsg
syslog roles from the generic driver lifecycle; board code must not register a
second client or server.  The AP profile explicitly keeps the high-priority
work queue because the upstream RPMsg syslog client always schedules its
drain worker on `HPWORK`.

The maintained base and product profiles use buffered line output, a 512-byte
per-CPU interrupt buffer, monotonic timestamps, priority, PID and an `ap` or
`cp` prefix.  The AP's SMP CPU index is added independently by NuttX.  Default
and RPMsg channel force operations are non-blocking, and the interrupt buffer
prevents records from an ISR and a task from being interleaved.  File and
device/console output channels are intentionally disabled: they require a
mounted filesystem or a locking character driver and are not safe as an early
or interrupt sink.  CP registers `/dev/log` only as the syslog ioctl/control
frontend required by `setlogmask`; it is not configured as an output channel.

CP enables the builtin registry and exposes the `setlogmask` command for
runtime severity and channel control.  Its severity mask applies to records
produced on CP; AP records have already been formatted and filtered before the
server writes them to CP's output channel.  Disabling CP's `default` channel
suppresses both sources because it is their shared final sink.

Syslog timestamps remain monotonic even on the RTC-enabled T5-Board AP.  A
formatted realtime timestamp would look authoritative while the current RTC
is only seeded from the build date and has no trusted, power-loss-persistent
UTC source.  Realtime/localtime logging may be enabled after network or CP
time synchronization owns that contract.  New kernel and driver diagnostics
must use the `debug.h` macros so production builds can compile them out; direct
`syslog()` is reserved for application output and reviewed compatibility or
crash-path contracts.  In particular, the SDK varargs bridge preserves its
caller-selected priority, while the xTS watchdog pre-timeout record is emitted
from an interrupt buffer and force-flushed before whole-device reset.

On T5-Board the two switch pairs are independent:

- S1-1/S1-2 ON connect the CH342F download UART to P10/P11, which are TF
  D2/D3.  This position supports one-bit TF on CLK/CMD/D0; four-bit TF
  requires both switches OFF while the application runs.  UART flashing may
  use them temporarily, followed by switching both OFF and resetting.
- S1-3/S1-4 ON connect the CH342F log UART to P1/P0.  That route conflicts
  with P0/P1 SWD (and P1 LED), but it does not change TF bus width.  A
  four-bit TF profile may keep P0/P1 SWD and RTT when S1-3/S1-4 are OFF.

The schematic marks optional serial flash U3 as NC/DNP.  U3 shares TF
CLK/CMD/D0/D1 (and its remaining data pins occupy the adjacent SFC nets), so
fitting it makes the TF socket unavailable rather than creating a third
software profile.  A console-enabled build deliberately leaves P1 under
UART ownership and therefore exposes only the P12 key as `/dev/gpio1`; it
does not register the P1 LED as `/dev/gpio0`.  A non-console build may use
the LED.  The Type-C connector terminates at CH342F; BK7258 native USB D+/D-
is routed to the USB-A connector.

P6 stays high with the TF card both inserted and removed on the tested board,
including under the SDK-equivalent input/pull-up setup.  The T5-Board binding
therefore uses NuttX fixed-media semantics: insert the FAT card before reset,
do not claim hotplug, and do not repair this by reversing polarity.
