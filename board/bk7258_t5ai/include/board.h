/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/include/board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal board header for the Beken BK7258 (T5-AI) Stage N1 port.
 * NuttX's configure step exposes this via <arch/board/board.h>.
 ****************************************************************************/

#ifndef __ARCH_BOARD_BK7258_T5AI_BOARD_H
#define __ARCH_BOARD_BK7258_T5AI_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LEDs / buttons: not modelled at N1. */

#define BOARD_NLEDS       0
#define BOARD_NBUTTONS    0

/* Physical memory layout (informational; the authoritative copy is in
 * scripts/ld.script).
 */

#define BOARD_FLASH_ADDR  0x02010000u
#define BOARD_FLASH_SIZE  0x00100000u
#define BOARD_RAM_ADDR    0x28000000u
#define BOARD_RAM_SIZE    0x000a0000u

/* UART1 (console) MMIO base and register offsets.  The console driver in
 * chip/bk7258_serial.c hardcodes these too; they are repeated here for any
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

#endif /* __ARCH_BOARD_BK7258_T5AI_BOARD_H */
