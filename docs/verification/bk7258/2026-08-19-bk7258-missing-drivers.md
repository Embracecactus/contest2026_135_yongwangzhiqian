# BK7258 missing peripheral drivers (ETH / IrDA / GDMA) implementation and dual-image verification

- Date: 2026-08-19
- Board: BK7258 (T5AI-Core default binding)
- Status: dual-image build PASS (compile + link + partition layout), hardware not run

## Scope

The BK7258 datasheet lists four peripherals that previously had no NuttX
driver in this tree: Ethernet MAC, Smart Card reader (SCR), IrDA and GDMA.
Three are implemented in this work; SCR is blocked by incomplete SDK source
(see below).

## Implemented drivers (all board-owned; NuttX/SDK sources unchanged)

| Peripheral | Device | Source | SDK backing |
|---|---|---|---|
| Ethernet MAC | `eth0` (netdev) | `board/bk7258/chip/ap/bk7258_eth.c` | AP `HAL_ETH_*` + LAN8742 PHY (CONFIG_ETH=y) |
| IrDA NEC receiver | `/dev/irda0` | `board/bk7258/chip/cp/bk7258_irda.c` | board-owned register-level (base `0x458b0000`) |
| GDMA mem-to-mem | `/dev/dma0` | `board/bk7258/chip/ap/bk7258_dma.c` | AP `bk_dma_*` channel API |

Supporting files:

- Headers: `chip/include/bk7258_eth.h`, `bk7258_irda.h`, `bk7258_irda_abi.h`,
  `bk7258_dma.h`
- SDK-copied ETH headers (the SDK bundle does not export
  `middleware/driver/eth/*.h`): `chip/include/eth_mac.h`,
  `eth_mac_types.h`, `eth_mac_regs.h`, `eth_mac_ex.h`, `lan8742.h`.
  `eth_mac_types.h` renames `ERROR`→`ETH_ERROR` and guards `UNUSED` to avoid
  the NuttX `sys/types.h` collision.
- Board binding: `bk7258_board_eth_config()` in
  `boards/t5ai_core/src/bk7258_board_bringup.c` (PIN_GROUP0 + locally
  administered MAC).
- Kconfig: `BK7258_ETH` (with RMII PIN_GROUP choice, conflicts DVP/LCD),
  `BK7258_IRDA` (CP), `BK7258_DMA` in `chip/Kconfig`.
- Registration: `chip/CMakeLists.txt` + `chip/Make.defs`; AP drivers through
  `bk7258_peripherals_initialize()` (ETH via `arm_netinitialize()`),
  CP IrDA through `bk7258_platform_initialize()`.

Driver notes:

- Ethernet uses the STM32H7-compatible SDK HAL verbatim
  (`stm32h7xx_hal_eth.h` port; BK7258 EMAC shares the ST IP). The wrapper
  supplies the STM32-style helpers the SDK does not export:
  `HAL_RCC_GetHCLKFreq()`, `HAL_ETH_MspInit()` (GPIO mux + INT_SRC_ETH),
  `HAL_ETH_RxCpltCallback()` (RX → `ipv4/ipv6/arp_input`), and
  `xTaskGetTickCount()` (HAL_ETH_GetTick dependency).
- IrDA: board-owned register-level NEC decoder against `0x458b0000`
  (mirrors the SDK decoder offsets CTRL+0/INT_MASK+4/INT+8/RX_FIFO+12);
  receiver GPIO `GPIO_25 -> GPIO_DEV_IRDA`; INT_SRC_IRDA hook; key ring +
  semaphore + NuttX wdog debounce (replaces the SDK timer channel). The
  BK7258 ICU clock-power and interrupt-enable ll hooks are empty SDK
  implementations, so no ICU register writes are needed. The legacy-base
  SDK driver (`irda_init/Irda_init_app`) is no longer referenced — the final
  CP image contains only the register-level symbols.
- GDMA allocates a private channel user token (`DMA_DEV_LA`), so it never
  collides with the AUD/MIC/JPEG/H264 SDK DMA owners.

## Build verification

Command (fresh output root, drivercheck profiles):

```text
BK7258_OUTPUT_ROOT=<empty-root> \
CP_CONFIG_NAME=t5ai_core_cp_drivercheck AP_CONFIG_NAME=t5ai_core_ap_drivercheck \
  tools/bk7258/build_dual_image.sh
```

Results:

- `setup_bk7258_sdk.sh: check PASSED` (CP and AP bundles)
- Partition generation/layout/factory/sdk-partition-wrapper: PASS
- CP image `app.bin` (164092 bytes) contains `/dev/irda0`,
  `BK7258 IRDA: open/ready` → IrDA linked
- AP image `app1.bin` (125804 bytes) contains `BK7258 ETH: ready`,
  `BK7258 DMA: open/ready` → ETH + GDMA linked
- New verification configs: `configs/t5ai_core_cp_drivercheck`,
  `configs/t5ai_core_ap_drivercheck` (AP enables `CONFIG_NET` +
  `CONFIG_SCHED_WORKQUEUE/LPWORK`)

Individual wrappers additionally pass `arm-none-eabi-gcc -c` with the
production ABI (`-mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16`);
all referenced SDK symbols were confirmed present in the AP/CP
`libdriver.a` archives.

## Failures and investigation

1. **`work_queue`/`work_cancel` undefined**: CONFIG_NET pulls in the TCP
   timers and the ETH wrapper uses the work queue; the drivercheck defconfig
   had no workqueue. Fixed by adding `CONFIG_SCHED_WORKQUEUE=y` +
   `CONFIG_SCHED_LPWORK=y`.
2. **`xTaskGetTickCount` undefined**: SDK `HAL_ETH_GetTick()` calls it and
   the FreeRTOS archive is excluded from the NuttX link set. Fixed by
   providing it board-owned in `bk7258_eth.c` (returns
   `clock_systime_ticks()`).
3. **`arm_netinitialize` undefined**: NuttX `up_initialize()` requires the
   symbol when CONFIG_NET is set. Fixed by providing it in `bk7258_eth.c`
   (calls `bk7258_eth_initialize()`); the ETH block was removed from
   `bk7258_peripherals_initialize()` to avoid double registration.
4. **Kconfig menu trap**: `BK7258_IRDA` was initially placed inside
   `menu "BK7258 AP peripheral wrappers"`, which carries
   `depends on BK7258_AP_CORE`. On the CP build the whole menu (and the
   symbol) is invisible — the final `.config` had no `BK7258_IRDA` line at
   all. Moved the CP-only symbol out of that menu (next to `BK7258_WDT`).

## SCR: not implemented (SDK source incomplete)

`scr_driver_v1_26.c` references `gpio_scr_sel()` and
`gpio_scr_map_group_t`, which exist only as call sites in the v3.1.1.9 SDK
tree; `scr_hw.h` pulls in `scr_reg.h`/`scr_struct.h`/`scr_ll.h` which do not
exist in the tree or in upstream v1.6.0. Building with `CONFIG_SCR=y`
therefore fails, so the SDK-export path (the PWM profile-overlay approach)
is not viable for SCR without Beken source material.

## IrDA host unit tests (PASS, 2026-08-19)

`tests/bk7258/modules/ap/test_bk7258_irda.c` — 11 cmocka tests against the
true driver source (patched copy via `framework/patch.py` profile `irda`):

- initialize/register (`/dev/irda0`), double-init -EBUSY
- open bring-up: GPIO_25 → GPIO_DEV_IRDA mux, INT_SRC_IRDA hook, receiver
  config (NEC_EN, polarity 0, 0x3921 divider, RIGHT/REPEAT/END mask)
- NEC decode: leader+end valid frame → SHORT key; wrong usercode rejected;
  bad key inverse rejected
- repeat classification: LONG (3 repeats) and HOLD (8 repeats) via the
  explicit wdog fire
- read/write (-ENOTSUP)/ioctl (BKIOC_IRDA_SET_USERCODE, unknown -ENOTTY)
- close teardown (interrupt mask cleared, NEC disabled)

Infrastructure added:
- `framework/patch.py` profile `irda` (routes the driver's direct register
  accessors through `mock_reg32`, exposes the char-device callbacks and a
  test-reset hook in the throwaway copy)
- `framework/mock_reg32` IRDA window `0x458b0000`
- `framework/mock_sdk_irda.{c,h}` (gpio_dev_map, bk_int_isr_register,
  register_driver programmable mocks)
- `framework/mock_wdog.c` + `mocks/nuttx/wdog.h` (explicit wdog firing)
- `framework/mock_sem.c` (POSIX nxsem_* for this suite)
- mock headers: `mocks/nuttx/{arch,irq,fs/fs,fs/ioctl,wdog}.h`,
  `mocks/driver/{gpio,gpio_types,int,int_types}.h`,
  `mocks/arch/chip/bk7258_irda.h`
- `mocks/nuttx/semaphore.h` adds `SEM_INITIALIZER`/`nxsem_wait_uninterruptible`;
  `mocks/nuttx/spinlock.h` adds `enter/leave_critical_section`;
  `mocks/mock_sdk.c` adds `nxsem_wait_uninterruptible`

Run: `make build/test_bk7258_irda && ./build/test_bk7258_irda` → **11/11 PASS**.
Regression: jpeg_decoder 68, yuv_h264 42, scale_rotate 54 remain PASS.
(`test_bk7258_can` fails 5 data-comparison cases in this tree — pre-existing,
uses the independent `mocks/nuttx_can` shim and is unrelated to the IrDA work.)

## Residual risks

### IrDA base-address mismatch (resolved, 2026-08-19)

Disassembly of the CP SDK `irda.c.obj` (extracted from `cp/libs/libdriver.a`)
shows its literal pool loads base `0x00802000` and accesses
`+0x400/0x404/0x408`, i.e. `IRDA_CTRL/INT_MASK/INT` at
`0x00802400` — the legacy Beken APB base from `middleware/driver/irda/irda.h`.
BK7258 hardware locates IrDA at `SOC_IRDA_REG_BASE=0x458b0000`
(`include/soc/bk7258/reg_base.h`), and the SDK exposes **no** IrDA register
structure or offset definitions for that base (no `irda_ll.h`/`irda_reg.h`,
upstream v1.6.0 likewise absent).

**Resolution**: the `/dev/irda0` wrapper now implements the NEC decode path
board-owned against `0x458b0000` (register offsets assumed identical to the
legacy layout: CTRL+0, INT_MASK+4, INT+8, RX_FIFO+12). The receiver GPIO is
`GPIO_25 -> GPIO_DEV_IRDA`; INT_SRC_IRDA is hooked via `bk_int_isr_register`
(the BK7258 ICU ll clock-power/interrupt hooks are empty SDK stubs, so no
ICU register writes are required); the key debounce timer is a NuttX wdog
instead of the SDK timer channel. The final CP image contains only this
register-level implementation (verified by strings) and no longer links the
legacy-base SDK `irda_init/Irda_init_app`.

### Ethernet PHY dependency (confirmed, 2026-08-19)

The BK7258 EMAC is a MAC-only block: it needs an external RMII PHY. The SDK
default PHY is SMSC LAN8742 (`CONFIG_PHY_SMSC=y`), and the lwIP
`ethernetif.c` shows the canonical sequence the wrapper mirrors
(`HAL_ETH_Init` with `MediaInterface=HAL_ETH_RMII_MODE`, `RxBuffLen=1536`,
then LAN8742 init + autonegotiation). The PHY address is internal to
LAN8742 (SMSC default 0), passed through the wrapper's IO callbacks.

However, **neither T5AI-Core V1.0.1 nor T5-Board V1.0.2 fits an Ethernet
PHY** (their board peripheral inventory — LED/key/mic/TF/RGB-LCD/DVP — has
no PHY). Hardware verification therefore requires a PHY-equipped carrier;
the wrapper's PIN_GROUP0 (conflicts DVP) and PIN_GROUP1 (conflicts LCD)
choices remain the board-level wiring decision.

## Evidence locations

- Sources: `board/bk7258/chip/{ap/cp/include}/bk7258_{eth,irda,dma}*`,
  `chip/include/eth_mac*.h`, `chip/include/lan8742.h`
- Registration: `chip/Kconfig`, `chip/CMakeLists.txt`, `chip/Make.defs`,
  `chip/ap/bk7258_peripherals.c`, `src/bk7258_platform.c`,
  `boards/t5ai_core/src/bk7258_board_bringup.c`
- Verification configs: `board/bk7258/configs/t5ai_core_{cp,ap}_drivercheck/`
