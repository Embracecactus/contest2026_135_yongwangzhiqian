# BK7258 physical-board variants

`chips/bk7258` owns the shared BK7258 CP/AP, boot-chain, SDK wrapper and
SoC lifecycle.  This follows the official [openvela platform-adaptation guide
(id=1443)](https://doc.openvela.com/document?id=1443&version=dev-ai-contest-2026&language=cn): chip contains reusable SoC capability; board contains product
policy and electrical binding.  `boards/bk7258/common` owns shared board,
partition and linker integration;
the three sibling board directories contain only physical-PCB wiring and
capability facts. Each physical board's `openvela.conf` additionally owns the
normal CP/AP config and partition selection used by the generic build entry;
the tool contains no table of known physical boards.

## CP/AP startup ownership

`CONFIG_BOARD_LATE_INITIALIZE` is selected by the shared BK7258 board Kconfig.
`board_late_initialize()` is therefore active and remains a thin board-level
bridge in both roles, not the owner of SoC mechanisms or lifecycle order.
Before NuttX starts the initial application, the CP bridge binds immutable
board facts and drives the chip-owned lifecycle to its two explicit board
checkpoints.  It executes an eligible board policy/device operation and
reports that result back to the same chip runner.  AP main later consumes the
cached result and never silently reorders preparation.

- Chip owns the complete CP/AP SoC stage tables, order, prerequisites,
  first-error propagation and cached status.  The CP entry is the
  `bk7258_cp_platform_begin()` / checkpoint / `finish()` lifecycle; the AP
  entry is `bk7258_ap_platform_prepare()`.  Chip also owns the raw reset-source
  reader, Wi-Fi controller/proxy, boot-slot selection and OTA engine mechanics.
- Board owns the partition/layout and storage binding, OTA trial/product
  policy, explicit checkpoint work, `BOARDIOC` mapping, final-init/ROMFS
  policy, and the selected physical board's GPIO/LCD/audio/TF/camera binding.

The board bridge passes immutable data into chip mechanisms; chip code neither
discovers the selected board, calls a board symbol nor retains platform-level
board callbacks.  A checkpoint is a typed result handoff, not dependency
inversion: its position and failure class remain in the chip stage table.  The
table has explicit `requires_mask` prerequisites, so a stage runs only after
every required earlier stage succeeded. `ALWAYS_RUN` therefore means
“may cross an unrelated mandatory failure”, not “may ignore a failed storage
or hardware dependency”.  For example, OTA trial policy requires a validated
OTA layout, while WDT requires a validated reset-marker domain when pretimeout
persistence is enabled.

`board_late_initialize()` is a `void` NuttX hook, so it retains the initial
diagnostic shell after a mandatory CP failure. This is not a degraded
application boot: the cached chip result makes `board_app_initialize()` /
`bk7258_bringup()` fail closed before procfs, storage and other
application-facing registration. The shell is available only to diagnose the
failure.

The WDT pretimeout marker is a dedicated `reset_marker` erase sector declared
by every selected board partition CSV. One immutable board storage binding
supplies the OTA layout, marker geometry and Flash serialization policy; raw
Flash mechanics remain chip-owned. The chip marker/WDT code does not share an
OTA-specific Flash helper. Boot-slot remap MMIO decoding is likewise
chip-owned.
CP also owns `bk7258_system_reset()`: callers select the semantic
`REBOOT`, `WATCHDOG` or `NMI_WDT` reason, and the chip performs the AON/PMU
whole-device reset sequence with an architecture-reset fallback. OTA uses the
explicit `REBOOT` reason. A marker is not written when WDT is armed or fed;
only the task-context pretimeout worker writes one after independent generation
and elapsed-time checks confirm a missed feed. PMU `POWERON` and `REBOOT` are
primary evidence, so a stale marker never replaces them; a confirmed WDT
marker only corroborates a PMU WDT/NMI-WDT reason or fills an unknown raw PMU
value. The confirmed-pretimeout record is format version 2; legacy version-1
arm-time records fail validation and therefore cannot participate in reset
attribution.

## Naming

Stable directory and Kconfig names identify the board product, not a PCB
revision:

| Physical board | Directory | Kconfig | Documented revision |
|---|---|---|---|
| T5AI-Core | `t5ai_core` | `CONFIG_BK7258_BOARD_T5AI_CORE` | V1.0.1 |
| T5-Board | `t5_board` | `CONFIG_BK7258_BOARD_T5_BOARD` | V1.0.2 |
| AIDK AI Toy | `aidk_ai_toy` | `CONFIG_BK7258_BOARD_AIDK_AI_TOY` | schematic-v1.0 |

Hardware revisions live in `BK7258_BOARD_HARDWARE_VERSION`.  A revision gets
its own selector only if it changes a software-visible electrical contract.
T5AI-Core is the default so all existing configurations retain their verified
board behavior.

## Source boundary

The current pin facts come from these schematics supplied by the project
owner:

- `T5AI-Core_V101-SCH-a69f7b5a91b4bf21a39bdb7c17812373.pdf`
- `T5-Board_V102_SCH250617.pdf`
- `AIDK_AI玩具开发板_原理图.pdf` (BK7258 AI Demo V1.0)

TuyaOpen's `TUYA_T5AI_CORE` confirms the Core board's P9 LED, P29 key and P39
speaker-control naming.  TuyaOpen's `TUYA_T5AI_EVB` is not electrically
equivalent to T5-Board V1.0.2 and is not used as a pin source.

## Board-level mapping

| Function | T5AI-Core V1.0.1 | T5-Board V1.0.2 | AIDK AI Toy V1.0 |
|---|---:|---:|---:|
| User LED | P9, active high | P1, active high | P40, active high |
| User key | P29, active low | P12 (`ADC_KEY`), active low | P8 (`KEY3`), active low |
| Speaker control | P39 | P28 (`SPK_CTL`) | P50 (`MUTE`/`PA_SD`) |
| Battery ADC | P28 | not fitted | internal VBAT ADC0 |
| Charge detect | P38 | not fitted | P51 `5V_DET`; P26 `FULL_DET` |
| Audio capture | MIC1 on MICP1/MICN1 (mono) | MIC1 on MICP1/MICN1 + MIC2 on MICP2/MICN2 (stereo) | MIC1 primary microphone; MIC2 input is AUDLP/AUDLN loopback for AEC, not a second microphone |
| Block media | not fitted | TF: CLK P2, CMD P3, D0 P4, D1 P5, D2 P10, D3 P11; P6 CD label has no verified edge | soldered SD NAND: CLK P14, CMD P15, D0..D3 P16..P19 |
| Display connector | not fitted | RGB LCD fitted | CN5 QSPI route present; single-screen module not connected |
| DVP camera connector | not fitted | fitted | GC2145 connected; Phase 0 identity probe only |
| Motor | not fitted | not fitted | CN10 route on P9 present; motor not connected |
| External 32.768 kHz crystal | not fitted | schematic-dependent | X2/C16/C17 not fitted |

The Core board is the broad hardware-verified baseline.  T5-Board entries are
promoted from schematic evidence only when their peripheral record captures a
real-board result; the TF record, for example, rejects P6 card detect after
inserted/removed level sampling.  Variant selection does not automatically
enable every fitted peripheral; Kconfig still controls driver ownership and
pin-compatible profiles.

The AIDK AI Toy has one maintained normal OpenVela CP/AP pair.  Its reviewed
bindings currently cover UART0 at 115200 8N1, P40/P8 user GPIO, P50 speaker
control, MIC1 plus the MIC2 AEC-reference capture input and soldered SD NAND.
The maintained AP profile enables the I2C0 controller route on P20/P21 and
UART1 on P0/P1 because the schematic connects SC7A20H and MFRC522 there, but
the current tree has no sensor/NFC device binding, probe or production driver;
the capability and conflict macros record wiring only.
Flow control, SWD, boot hold, RTT and RTS/DTR reset stay disabled.  COM/USB port
identity is dynamic transport metadata and is not a board or product identity.
The unpopulated X2/C16/C17 network does not reserve P8/P9: P8 is enabled as
KEY3, while P9 remains unclaimed because no motor is connected to CN10.  The
CN5 single-screen module is likewise disconnected, so its QSPI/display route
is recorded but not initialized.  The AIDK CP SDK overlay also disables the
vendor MP_A external-32-kHz override and retains the calibrated internal ROSC
as the low-power clock source.

The normal AP config performs only the GC2145 Phase 0 identity check.  It
enables both camera LDOs through active-high P49, supplies 24 MHz MCLK on P27,
releases active-low reset on P28, and uses hardware I2C1 map mode 1 on P42/P43
to read register `0xf0` followed by `0xf1`.  A value of `0x21`/`0x45` reports
ID `0x2145`; no sensor initialization register table or DVP capture path is
run, and the probe asserts reset and powers the camera off again before board
bring-up continues.

## Peripheral configuration boundary

The physical-board directory is the owner of every fixed electrical fact, not
only LED and key GPIOs.  This includes fitted-device capability, pin routes,
polarity, pull/drive policy, bus instance, device address or chip select,
board-device frequency limits, LCD timing, SD-card presence policy and mutually
exclusive connector routes.  A value may live in `bk7258_board_config.h` or in
a board-local binding structure when it is used only by that binding.

The shared `chips/bk7258/` wrappers own BK7258 controller mechanics and the NuttX
lower-half contract.  They must not describe a T5-Board connector or attached
part.  In particular, the generic I2C wrapper applies each message's
`frequency`, and the generic SPI wrapper applies the upper half's frequency,
mode and word width.  Those runtime transaction values are not global board
constants.  Only a fixed device such as the GT1151 or camera supplies a
board-device default or maximum through its selected-board binding.

The selected physical board implements `bk7258_board_ap_initialize()` and
orders its board-specific pre-device and attached-device work around the
shared `bk7258_board_ap_controllers_initialize()`,
`bk7258_board_ap_buses_initialize()` and
`bk7258_board_ap_finalize_initialize()` phases.  A new physical board therefore
adds its own header and AP composition entry; it does not add board-name tests
or pin literals to shared chip mechanisms.  A CP-only attached device must
provide its explicit board hook, such as
`bk7258_board_cp_devices_initialize()` when `CONFIG_BK7258_TOUCH` is enabled.
Its board Kconfig selector must itself depend on `!BK7258_AP_CORE`; the shared
chip CMake and Classic Make entries deliberately reject an AP selector that
bypasses the chip symbol's dependency through Kconfig `select`.
SPI follows the standard NuttX compile-time `spiNselect`/`spiNstatus`
board-hook model and is selectable only for a physical board that declares
such a binding.

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
maintained openvela AP bases remain on UTC and do not select localtime policy.
A future UI or product config may enable `CONFIG_LIBC_LOCALTIME` and choose a
POSIX timezone or a mounted zoneinfo database; that policy does not belong in
the physical-board baseline.

## System-log ownership and safety

The maintained no-console AP base profiles send their NuttX syslog stream to
the paired CP over the existing RPTUN/RPMsg link.  CP is the RPMsg syslog
server and writes both local and received records through its early
`up_putc()` channel to the board-owned UART0.  NuttX initializes both RPMsg
syslog roles from the generic driver lifecycle; board code must not register a
second client or server.  The AP profile explicitly keeps the high-priority
work queue because the upstream RPMsg syslog client always schedules its
drain worker on `HPWORK`.

The maintained base profiles use buffered line output, a 512-byte
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
from an interrupt buffer and force-flushed before the task-context reset worker
performs the whole-device reset.

## Trace diagnostics

The maintained console CP base profiles, the T5AI-Core CP driver-check profile
and the T5-Board CP xTS/diagnostic profile own the interactive Trace service.
They provide the NuttX `trace` command, `/dev/notectl` and a 32-KiB
`/dev/note/ram` circular buffer.  The size leaves enough contiguous CP heap for
the 4-KiB Trace command stack after the maintained T5-Board services are
running.  The separate T5-Board performance profile disables Trace and the
other asynchronous diagnostic monitors; its contract and test procedure are
documented in
[`../../docs/platforms/bk7258/p0-diagnostics-performance.md`](../../docs/platforms/bk7258/p0-diagnostics-performance.md).
The accepted generation 143 diagnostic capture and generation 145
low-noise benchmark results are recorded in
[`../../progress/verification/2026-08-27-bk7258-p0-diagnostics-performance.md`](../../progress/verification/2026-08-27-bk7258-p0-diagnostics-performance.md).
Scheduler switches, interrupt entry/exit and custom `sched_note_*` events are
compiled in.  The default `0x3e` filter follows the openvela Trace guide;
it masks task start/stop/suspend/resume/name records while leaving IRQ records
available.  `trace start` clears the RAM buffer and enables capture, while
`trace stop` freezes it before `trace dump` prints Perfetto-compatible text.
The `hello` builtin is retained as a deterministic smoke-test workload.

Trace timestamps use the Armv8-M DWT cycle counter through
`CONFIG_ARCH_PERF_EVENTS`, with 32-bit overflow correction enabled.  BK7258
initializes the DWT conversion from the decoded live CPU clock and refreshes it
after DVFS changes; the fixed 32-kHz scheduler SysTick remains independent.
The CP service deliberately does not merge AP Note records into its RAM
channel: AP and CP have independent DWT epochs, and combining their raw records
without clock synchronization would produce a misleading shared timeline.

On the CP console, the minimal hardware acceptance sequence is:

```text
nsh> trace start
nsh> hello
nsh> trace stop
nsh> trace dump
```

That default sequence validates IRQ entry/exit capture.  To retain explicit
`sched_switch` records in the small CP buffer, enable switch records and mask
the high-rate IRQ stream before starting a second capture:

```text
nsh> trace mode +w -i
nsh> trace start
nsh> hello
nsh> trace stop
nsh> trace dump
```

The mode change is runtime-only; reboot restores the configured `0x3e`
default.

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
