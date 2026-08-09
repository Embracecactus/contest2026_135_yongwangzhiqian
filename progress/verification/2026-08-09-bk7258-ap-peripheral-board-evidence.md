# BK7258 AP Peripheral Board Evidence (drivercheck MCUboot image)

Date: 2026-08-09 (GMT+8)
Board: BK7258 T5-AI, CP console COM11 @460800
Image: `cp_nsh_drivercheck_mcuboot` + `ap_smp_drivercheck_mcuboot`
       (MCUboot 18.1.3, security counter 20, same-slot CP/AP pair)
Capture tooling: `board/bk7258_t5ai/scripts/bk7258_auto_debug.sh`
(flash + J-Link shared-SRAM capture) and
`scripts/capture_windows_serial.ps1` (COM11 serial, `apctl` commands).

## Scope

Board-level verification of the AP peripheral wrappers that reached the
compile/link gate in `2026-08-09-bk7258-ap-lowerhalf-bindings.md`:
AUD, GPIOE, I2C, I2S, LCD, RTC, SARADC, SDIO, SDMADC, SPI, timer and TRNG.
Microphone capture is config-excluded (AUD owns the AUD ADC).

## AP health at capture time

- AP state `READY(2)`, error 0, rising heartbeat (supervisor alive).
- CPU2 `SECONDARY_READY(7)`, error 0, VTOR/MSP sane.
- AP SMP IPI state `READY(2)`, both IPI directions idle with no
  dup/lost/fail counters.
- RPTUN remains `CONNECTING(3)` (known: CP rpmsg transport not brought
  up in this profile), so AP syslog is unavailable.  The early fault-isolation
  evidence below came from a temporary shared-SRAM check block.  That probe
  was removed after the root causes were fixed; the final cleaned image is
  verified through the permanent `apctl status` telemetry.

## Historical /dev registration probe

Before probe cleanup, `apctl devices` enumerated 13 AP /dev nodes:

```text
/dev/adc0 /dev/adc1 /dev/audio /dev/gpio0 /dev/i2c0 /dev/i2schar0
/dev/null /dev/rpmsg /dev/rptun /dev/rtc0 /dev/spi0 /dev/timer0 /dev/zero
```

## Historical per-peripheral init results

| Peripheral | Result | Evidence slot |
|---|---|---|
| AUD (`bk_aud` register) | 0 | aud_init |
| I2C | 0 | i2c_init |
| RTC | 0 | rtc_init |
| SARADC | 0 | saradc_init |
| SDMADC | 0 | sdmadc_init |
| TIMER | 0 | timer_init |
| SDK IPC table (`bk_ipc_init`) | 0 | bk_ipc_init |
| GPIOE bound (early path) | 1 | gpioe_bound |
| I2S init / `i2schar_register` | 0 / 0 | i2s_init / i2schar_register |
| SDIO init / `mmcsd_slot` | 0 / 0 | sdio_init / mmcsd_slot |
| SPI init / `spi_register` | 0 / 0 | spi_init / spi_register |
| MIC | config-excluded | mic_init = '-' |
| `fb_register` | -19 (-ENODEV) | initial fb_register baseline |
| LCD framebuf alloc | -12 (-ENOMEM) | initial lcd_framebuf baseline |

`/dev/mmcsd0` is intentionally absent: NuttX mmcsd defers the node until
media insert is detected; no SD card was inserted.

## Runtime self-check (user-space access from CP-side apctl proxy)

| Check | Result | Meaning |
|---|---|---|
| `/dev/gpio0` open + `GPIOC_READ` | 1 | upper-half reachable, pin reads high |
| `/dev/rtc0` `RTC_RD_TIME` | tm_year=70 | RTC answers; time never set (epoch) |
| `/dev/adc0` read | -1 | open succeeds, raw read errors (channel not configured) |
| `/dev/adc1` read | -1 | same |

## GPIOE root cause found this session (permanent fix)

`gplh_setpintype()` (NuttX `drivers/ioexpander/gpio_lower_half.c`)
ignores lower-half return codes and issues
`IOEXPANDER_OPTION_INTCFG` followed by `IOEXPANDER_OPTION_WAKEUPCFG`.
The board WAKEUPCFG handler previously called the SDK
`bk_gpio_unregister_wakeup_source()`, which sends a **synchronous**
`gpio_ipc` message (`MIPC_CHAN_SEND_FLAG_SYNC`,
`rtos_get_semaphore(..., BEKEN_WAIT_FOREVER)`). This NuttX port has no
CPU0 service answering that channel, so any pin-type change after
`bk_ipc_init()` hung the caller permanently. Before `bk_ipc_init()` the
same call fails fast, which is why only the early bind succeeded.

Fix (board-owned, NuttX/SDK untouched): the WAKEUPCFG branch in
`board/bk7258_t5ai/chip/ap/bk7258_gpioe.c` now returns `-ENOTSUP` —
wakeup-source policy belongs to the CP power-management domain. With the
fix, the real SDK `bk_gpio_driver_init()` path is used and AP reaches
READY.

Side finding: `0xffffdff7` observed in traces is
`BK_ERR_GPIO_INTERNAL_USED` (GPIO0 is muxed to I2C1_SCL in the SDK
`GPIO_DEFAULT_DEV_CONFIG`), not a fault artifact.

## LCD: root cause found (config omission, fixed)

`fb_register` failed with -ENODEV because `board_graphics_setup()`
failed: the 307215-byte framebuffer `bk7258_psram_zalloc()` returned
NULL. On-board probes proved the AP PSRAM heap was not "consumed":

- Drain probes (16 KiB alloc cycles) at peripheral entry and LCD init
  measured **0 KiB free**; the allocation audit log recorded **zero
  successful allocations** since boot.
- Raw AP bus probe at `0x60720100` failed its own read-back; heap node
  headers at `0x60720000/0x60720008` read 0; `bk7258_psram_malloc(64)`
  returned NULL.
- Cross-core check: AP-written markers in the heap, media-slab and
  AP-section windows read back 0 for AP **and** for CP; CP's own
  write/read test at `0x60720200` also read 0. The PSRAM array was
  unreachable from both cores.

Root cause: `cp_nsh_drivercheck_mcuboot` was created without
`CONFIG_BK7258_PSRAM=y`, so CP bring-up (`bk7258_bringup.c`, the sole
PSRAM hardware owner) skipped `bk7258_psram_initialize()` and the PSRAM
controller was never powered/configured. The AP path deliberately trusts
CP for hardware init, and its MPU-only validity check cannot detect an
unpowered array. Fix: add `CONFIG_BK7258_PSRAM=y` to the CP drivercheck
defconfig (vendor + board copies). Evidence: captures
`logs/jlink/trace_devs9.bin` .. `trace_devs11.bin`.

Also noted: `bk7258_psram_free_size()` (mm_mallinfo path) hangs when
called from the init task; the drain probe avoids metadata walks. The
hang is secondary to the dead-bus condition above and will be re-tested
with live PSRAM.

### Confirmation after enabling CP PSRAM (capture `trace_devs12.bin`)

- `psram_raw_rw=1`, `psram_guard_sz=9`, `psram_freenode_sz=655344`
  (= 0x9fffe, exactly 640 KiB minus the two guard nodes),
  `psram_malloc64=1`; AP-written markers visible from both cores.
- `psram_kib_pre=624`, `psram_kib_lcd=624`: the whole heap is free at
  peripheral entry; nothing consumes it before LCD.
- `lcd_framebuf=0`: the 307215-byte framebuffer now allocates.
- `lcd_panel_init=0` (software SPI ILI9488 sequence completes).
- `lcd_drv_init=-4096` = `BK_ERR_COMMON_BASE`: the display-clock setup
  inside `bk_lcd_driver_init()` failed.

### LCD PM vote investigation

The official `rgb_display_ctlr_open()` votes
`PM_POWER_PSRAM_MODULE_NAME_VIDP_LCD` (pwr_clk.h enum value 8) ON via
`bk_pm_module_vote_psram_ctrl()` before the LCD driver initializes.
The wrapper temporarily issued the same vote before
`bk_lcd_driver_init(LCD_30M)` to test whether it was the missing gate.

Result (capture `trace_devs13.bin`): `lcd_framebuf=0`,
`lcd_panel_init=0`, but `lcd_drv_init=-4096` was unchanged.  The vote was
not the missing prerequisite and the experiment was removed; no unmatched
power vote remains in the final driver.

### SDK system-resource IPC root cause and fix

The real v3.1.1.9 `driver_init()` sequence initializes four layers before
SDK-backed peripherals run:

```text
sys_drv_init -> ipc_init -> mb_ipc_init -> bk_ipc_init
```

The NuttX wrapper path had omitted that sequence and had also replaced the
SDK `.ipc_chan_reg` start/stop boundaries with NULL globals.  Consequently
`sys_drv_lcd_set()` could not acquire the cross-core system-register resource
and returned `BK_ERR_COMMON_BASE`.

The permanent board-owned fix is deliberately smaller than SDK
`driver_init()`:

- `bk7258_sdk_runtime_initialize()` runs only the four verified prerequisite
  calls on each image;
- CP initializes the runtime and its GPIO IPC service before releasing AP;
- both linker scripts retain `.ipc_chan_reg` and define its real boundaries;
- the fake NULL boundary symbols are removed;
- NuttX and the immutable SDK archives remain untouched.

After this fix the hardware trace changed to `lcd_drv_init=0`,
`lcd_rgb_init=0`, panel init 0 and framebuffer allocation 0.  AP reached
READY and CPU2 reached SECONDARY_READY.

### Direct NuttX framebuffer registration

The generic `LCD_FRAMEBUFFER` adapter allocated a second 320x480x2 shadow
buffer from the AP SRAM heap after the SDK wrapper had already allocated the
PSRAM scanout buffer.  That duplicate allocation failed with `-ENOMEM`.

The final driver implements the standard NuttX `fb_vtable_s` directly and
registers the existing PSRAM scanout buffer with
`fb_register_device(0, 0, ...)`.  It therefore publishes `/dev/fb0` without
another 300 KiB allocation.  The RGB engine continuously scans this same
buffer.  The final source, complete CP/AP build and board boot all passed.

The SDK clamps any RGB pulse width above seven to 2/2.  The board timing now
states the effective 2/2 values explicitly; the final boot log no longer
contains the earlier overflow warning.

## GPIO and shared-pin ownership

An all-driver image cannot simultaneously exercise every bus: GPIO, I2C,
I2S, SPI, SDIO and LCD share physical pins.  Automatically passing every
GPIO0..15 expander pin to `gpio_lower_half()` configured those pins during
registration and collided with the bus wrappers.  This contradicted the
driver's own explicit-ownership contract.

The permanent integration no longer auto-publishes all expander pins.
`bk7258_gpioe_initialize()` returns the standard ioexpander object; a board
consumer must explicitly choose and claim each GPIO before registering a
character device.  Hardware transfer tests must therefore use one
pin-compatible peripheral profile at a time.  Remaining busy/DMA messages in
the all-driver boot log are profile resource conflicts, not an AP boot
failure.

## TRNG hardware random-device proof

The AP drivercheck profile enables `BK7258_TRNG`, which selects NuttX
`ARCH_HAVE_RNG` and `DEV_RANDOM`.  Link-map evidence shows
`devrandom_register()` coming from the board wrapper and `bk_fill_rand()` /
`bk_trng_driver_init()` coming from the immutable v3.1.1.9 AP
`libdriver.a`.

A temporary fail-closed bring-up probe opened the standard `/dev/random`
node, read two 32-byte samples and rejected short reads or identical samples.
The AP published `READY(2), error=0` only after both reads passed; capture:
`logs/jlink/trng_probe_status.bin`.  The probe was then removed from source,
the clean CP/AP MCUboot package was rebuilt and sparse-flashed, and permanent
telemetry again reported AP READY, CPU2 SECONDARY_READY and AP IPI READY:
`logs/jlink/trng_clean_status.bin` and
`logs/bk7258-auto-debug/20260809-101729`.

## QSPI compile/link boundary

The AP-owned v3.1.1.9 QSPI controller is implemented as the standard NuttX
`qspi_dev_s` lower half.  The drivercheck MCUboot configuration compiles,
links and completes board post-processing using only public types and symbols
exported by the immutable SDK bundle.  The bundle's public `driver/qspi.h`
leaks a private `qspi_hal.h` dependency, so the board wrapper deliberately
uses the exported HAL types plus a minimal declaration of the five linked SDK
entry points instead of adding an SDK source include path.

This is not hardware-transfer evidence.  The board does not bind an MTD upper
half because the SDK's verified indirect-command subset and per-command
256-byte program limit are insufficient to claim arbitrary Flash semantics.
QSPI0 also overlaps RGB LCD pins and QSPI1 overlaps SDIO/SPI/I2S pins.  A
future transfer check therefore requires a known external QSPI device and a
dedicated pin-compatible profile; initializing QSPI in the current all-driver
image would disrupt already verified peripherals.

## CP capacitive-touch button proof

SDK v3.1.1.9 enables the internal touch controller on CP and includes the
implementation in the immutable CP bundle.  Its advertised multi-channel
scan setters are successful no-ops, so the board wrapper deliberately exposes
one selected channel through the standard NuttX buttons lower half.  It keeps
the SDK interrupt disabled and samples on LPWORK because the SDK ISR silently
claims `TIMER_ID1`, which is already owned by the NuttX timer lower half.

A dedicated CP configuration disabled the GPIO lower half before selecting
touch channel 3 / GPIO29.  The 32-job CP build, link and postbuild passed.  Its
raw image was then signed with the already provisioned development MCUboot
key, encoded with the v3.1.1.9 32+2 CRC rule and written to both CP slots; no
bootloader, AP, filesystem, OTP or eFuse range was changed.  BL2 accepted the
image, selected slot A, handed off to CP/AP and reached NSH:
`logs/bk7258-auto-debug/20260809-113338`.

The standard node is present as `/dev/buttons` in
`logs/jlink/touch_device_list.bin`.  A one-sample NuttX `dd` read returned four
bytes `08 00 00 00`, the bit for configured channel 3, in
`logs/jlink/touch_read_sample.bin`.  This proves initialization, upper/lower
binding and the hardware status-read path.  A press/release transition test
still requires a board pad wired as a capacitive electrode; GPIO29 on this
module is the mechanical USERKEY connection and is not claimed as capacitive
sensitivity evidence.

## Immutable-bundle blockers

- CAN and Ethernet source exists behind SDK feature gates, but neither the CP
  nor AP v3.1.1.9 immutable bundle exports the required controller data-plane
  ABI.  No placeholder lower half was added.
- USB Host is compiled into the AP bundle as a complete CherryUSB stack.  Its
  private root-hub thread, class enumeration and MUSB ownership cannot be
  combined with NuttX `usbhost_driver_s` without two stacks controlling the
  same hardware.
- USB Device source is not compiled into the immutable bundle.  The old MUSB
  host-pipe symbols do not provide NuttX endpoint/request or class-binding
  semantics, so no fake UDC wrapper was added.

## Final cleaned-image proof

- Full 32-job CP/AP MCUboot build passed with SDK v3.1.1.9 checksum gates.
- Sparse flash passed while preserving LittleFS, slot B and calibration tail:
  `logs/bk7258-auto-debug/20260809-095035`.
- Boot reached `B2HANDOFF` and NuttShell; the LCD pulse-width warning is gone.
- Permanent telemetry capture `logs/jlink/status_post_gpio_fix.bin` reports:
  AP `READY(2)`, error 0, rising heartbeat; CPU2
  `SECONDARY_READY(7)`, error 0; AP IPI `READY(2)` with zero loss/failure.
- RPTUN remains `CONNECTING(3)` in this drivercheck profile and is tracked
  separately from the peripheral/runtime fix.

## Red lines honored

- No NuttX or SDK source modifications; all changes are board-owned
  wrappers under `board/bk7258_t5ai/` and the board app.
- No OTP/eFuse or secure-lifecycle writes.
- All temporary shared-SRAM/device-list/allocation probes and temporary
  `apctl` debug commands were removed before the final build and flash.
