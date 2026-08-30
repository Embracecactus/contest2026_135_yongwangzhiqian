# BK7258 Platform Integration

English | [简体中文](README.md)

This is the shared platform-integration entry for T5AI Core, T5 Board, and AIDK
AI Toy. It covers paired CP/AP builds, delivery compliance, debugging procedures,
and retained engineering-stage records.

> **Current-status correction (2026-08-10):** The custom N15/N17 OTA selector,
> writer, journal, validation profiles, and scripts have been retired from the
> maintained source. Their records are historical evidence only. The current
> boot chain is board-owned BL1 → pinned NuttX MCUboot BL2 → signed same-slot
> CP/AP images; it provides no field OTA writer, confirm, or rollback service.

Use the following sources according to scope:

- [Official compliance review](official-compliance-review.en.md) for the exact
  interpretation of openvela documents 1443, 1444, and 1445;
- [Chinese platform index](README.md) and the [porting report](porting-report.md)
  for implementation details and historical stage links;
- [BK7258 SoC documentation](../../chips/bk7258/README.md) for contracts that do
  not depend on a physical board;
- `boards/bk7258/` for board pinout, profiles, and
  partition selection; and
- `boards/bk7258/CONFIGS.md` plus matching verification
  records for current acceptance status.

## T5-Board ILI9488 driver boundary

NuttX already provides the ILI9488 command definitions in
`include/nuttx/lcd/ili9488.h`. Its existing `sam_ili9488.c`, however, is a
SAMV71 SMC/DMA board binding and explicitly leaves the SPI path unsupported;
it cannot drive T5-Board's RGB scanout plus separate three-wire control bus.
The team overlay therefore reuses the official command header and adds only a
transport-independent RGB initializer. The BK7258 chip layer owns the generic
GPIO 9-bit transport. T5-Board owns GPIO49/48/50 control, GPIO53 reset, GPIO9
backlight, RGB pins, and timing. No BK7258 GPIO or SDK-private panel object is
part of the generic panel layer.

## AIDK AI Toy peripheral bus and driver boundary

The MFRC522 on AIDK AI Toy is **physically connected over UART, not SPI**.
UART1 uses P0/TX and P1/RX at 9600 baud, 8N1. P53 is `NFC_IRQ`, P54 is
`NFC_MX`, and P55 is active-low `_NFC_DTRQ`. The device is powered by the
active-high P52 `LDO33_EN` signal. The same `LDO_3V3` rail supplies the
on-board 1-Gbit SD NAND `NAND_VDD` through R45 and the LEDA/VDD pins of both
GC9D01 panels. Because P52 belongs to the SDK cross-core power-management
domain, board initialization must vote for it with
`bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_NFC, P52, HIGH)`;
SDIO independently uses the corresponding `GPIO_CTRL_LDO_MODULE_SDIO` vote
before the first MMC/SD probe, and LCD uses `GPIO_CTRL_LDO_MODULE_LCD` before
the first panel command. The SDK keeps the shared rail enabled while any of
these consumers holds a vote. The AP must not take P52 by directly removing
its GPIO mapping.

The two LCD panels share one backlight. P25 `LCD_BL_PWM` drives the NPN Q3
through R61, and Q3 controls the common `LCD_BL` net. A high PWM level turns
Q3 on, so the panels can only be enabled or dimmed together. The current CN5
optional single-display module is disconnected, but this is separate from the
two directly fitted GC9D01 panels. The maintained profile claims and drives P25
for those fitted panels during startup.

The two framebuffer instances are wired as follows:

- LCD1 `/dev/fb0`: QSPI1, P2 `CLK`, P3 `CS`, P4 `D0/SDA`, P5 `D/C`, and
  P45 `RESET`;
- LCD2 `/dev/fb1`: QSPI0, P22 `CLK`, P23 `CS`, P24 `D0/SDA`, P7 `D/C`, and
  P6 `RESET`.

NuttX/OpenVela currently has no GC9D01 driver, so the project provides an
upstream-oriented generic NuttX driver whose canonical sources are
`nuttx/drivers/lcd/gc9d01.c` and
`nuttx/include/nuttx/lcd/gc9d01.h`.
The project manifest maps the team repository's whole `nuttx/` directory to
`vendor/beken/nuttx`, and the build consumes that external overlay without
modifying the official `open-vela/nuttx` checkout. The generic driver owns the
GC9D01 commands, initialization table, state, and standard `lcd_dev_s` ABI.
The BK7258 chip layer supplies only SPI-over-QSPI/DMA transport, while the AIDK
board layer owns the two instances, RESET/D-C pins, shared P25 backlight, and
P52 LCD power vote. The initialization table records its Apache-2.0 SDK
provenance, but the product does not link the SDK-private `lcd_device_t` panel
object. Use `fb /dev/fb0` and `fb /dev/fb1` to test each framebuffer.

This power sequence belongs to the AIDK board layer, not to the BK7258 chip
SDIO lower half. P52, R45, and `NAND_VDD` are facts of this board schematic;
another BK7258 board may have no external LDO or may use a different control
pin. The common chip driver owns the SDIO controller, commands, clock, and
interrupts, and invokes the board `initialize()` callback before touching the
controller. The AIDK callback votes for power, waits for the rail to settle,
and then maps P14-P19. The SDK default GPIO table preselects one-line SDIO on
P2/P3/P4, and selecting map mode 1 does not remove that old group. The callback
therefore releases P2/P3/P4 before mapping the wired P14/P15/P16 group (and
P17-P19 in four-line mode). Dual-display initialization then lets LCD1 take
P2-P4 for its real wiring. P6/P7 and P25-P27, which QSPI mapping mode may claim
temporarily, are reclaimed by the board devices wired to those pins. The
maintained AIDK configuration uses the existing
`ap-sdio4` SDK variant and enables four-line D0-D3 transfers. That variant
changes only the SDK data path's compile-time bus width; it adds no board
script and duplicates no SDIO driver. SD NAND, NFC, and LCD now share P52 with
independent module votes. P52 remains an AIDK board fact and must not be
hard-coded in the generic chip driver.

The upstream NuttX `drivers/contactless/mfrc522.c` driver currently exposes
only the `mfrc522_register()` entry point taking a `struct spi_dev_s *`.
To retain its ISO14443-A discovery, anticollision, cascade selection, CRC, and
`MFRC522IOC_*` ABI, the board-owned
`boards/bk7258/aidk_ai_toy/src/bk7258_aidk_mfrc522.c`
provides an in-memory `spi_dev_s` adapter. It translates register operations
to the module's UART protocol: a read sends `0x80 | register` and receives the
value; a write sends `register`, receives its echo, and then sends the value.
The `aidk_nfc_spi_*` names describe the NuttX software contract implemented by
the adapter. They do not initialize an SPI controller, reserve SPI pins, or
emit SPI waveforms. The public device remains the standard `/dev/nfc0`.

This split avoids duplicating the MFRC522 protocol driver and avoids depending
on a private SDK NFC application API. The AIDK board layer owns UART1 and the
power vote; the upstream NuttX driver continues to own the card protocol and
character-device ABI. The AIDK AP configuration must enable
`CONFIG_STANDARD_SERIAL`; otherwise the UART1 lower half is not registered as
`/dev/ttyS1` and the board UART adapter fails with `-ENOENT`. The SDK default
GPIO table maps P53-P55 as LCD outputs. NFC board initialization must remove
that mapping, configure P53 as a pulled-up input, leave the unused P54/P55
handshake lines as unpulled inputs, and disable the MFRC522 MX/DTRQ outputs
through `TestPinEnReg` to avoid output contention. The current upstream NuttX
driver polls device registers and does not consume the P53 interrupt.

The detailed N1–N17 material retained below the Chinese index is historical
engineering evidence. It must not be read as a claim that every historical
profile remains part of the current product configuration.
