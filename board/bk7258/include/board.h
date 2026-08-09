/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/include/board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared board header for the Beken BK7258 T5-AI board family.
 * NuttX's configure step exposes this via <arch/board/board.h>.
 ****************************************************************************/

#ifndef __ARCH_BOARD_BK7258_BOARD_H
#define __ARCH_BOARD_BK7258_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <arch/chip/bk7258_amp.h>
#include <bk7258_board_config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The board-owned LED and key currently use the generic GPIO upper half,
 * not NuttX's automatic LED/button board APIs.  Keep these counts at zero
 * until those interfaces are implemented rather than advertising devices
 * which do not exist.
 */

#define BOARD_NLEDS       0
#define BOARD_NBUTTONS    0

/* Physical memory layout (informational; the authoritative copy is in
 * scripts/ld.script).
 */

#ifdef CONFIG_BK7258_AP_CORE
#  define BOARD_FLASH_ADDR  BK7258_AP_FLASH_ADDR
#  define BOARD_FLASH_SIZE  BK7258_AP_FLASH_SIZE
#  define BOARD_RAM_ADDR    BK7258_AP_RAM_BASE
#  define BOARD_RAM_SIZE    BK7258_AP_RAM_SIZE
#else
#  define BOARD_FLASH_ADDR  BK7258_CP_FLASH_ADDR
#  define BOARD_FLASH_SIZE  BK7258_CP_FLASH_SIZE
#  define BOARD_RAM_ADDR    BK7258_CP_RAM_BASE
#  define BOARD_RAM_SIZE    BK7258_CP_RAM_SIZE
#endif

/* UART1 (console) MMIO base and register offsets.  The console driver in
 * chip/common/bk7258_serial.c hardcodes these too; they are repeated here for any
 * board-level code that needs them.  See bk7258_serial.c / probe.c for the
 * register/bit definitions and the bootloader-inherited baud (~460800).
 */

#define BOARD_UART1_BASE     0x45830000u
#define BOARD_UART1_BAUD     460300     /* nominal; clk_div=0x37 -> 464286 Hz */

/* CPU/system clock frequency in Hz.  The Tier-1 bootloader does NOT enable
 * the DPLL, so the app core runs at the BootROM default = the 26 MHz XTAL
 * (confirmed via armino sdkconfig.h CONFIG_XTAL_FREQ=26000000 and the UART
 * divider math: clk_div=0x37=55 -> 26 MHz/(55+1) = 464286 Hz ~= 460800).
 * SysTick is clocked at the processor clock (CLKSOURCE=1, no /8 divisor).
 *
 * NOTE: NuttX has no CONFIG_CPU_FREQ_HZ Kconfig symbol; chips expose the
 * clock as a header macro (cf. mps MPS_SYSTICK_CLOCK).  Board-side
 * calibration TODO: update here if a future BSP enables the DPLL.
 */

#define BOARD_CPU_FREQ_HZ    26000000u

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_SDK_IRQ_TIMER_TEST
int bk7258_sdk_irq_timer_test(void);
#endif

#ifdef CONFIG_BK7258_SDK_TIMER_SELFTEST
int bk7258_sdk_timer_selftest(uint32_t iterations);
#endif

#ifdef CONFIG_BK7258_GPIO_FOUNDATION_TEST
int bk7258_gpio_foundation_test(void);
#endif

#ifdef CONFIG_BK7258_GPIO_IRQ_TEST
int bk7258_gpio_irq_test(void);
#endif

#ifdef CONFIG_BK7258_GPIO_LOWERHALF
int bk7258_gpio_lowerhalf_initialize(void);
#endif

#ifdef CONFIG_BK7258_GT1151
int bk7258_board_gt1151_initialize(void);
#endif

#endif /* __ARCH_BOARD_BK7258_BOARD_H */
