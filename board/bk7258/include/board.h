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
#include <arch/chip/bk7258_console.h>
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

/* Selected UART console metadata.  RTT and NONE builds intentionally expose
 * no UART console macros here.
 */

#ifdef BK7258_HAVE_UART_CONSOLE
#  define BOARD_CONSOLE_UART_ID   BK7258_CONSOLE_UART_ID
#  define BOARD_CONSOLE_UART_BASE BK7258_CONSOLE_UART_BASE
#  define BOARD_CONSOLE_BAUD      BK7258_CONSOLE_BAUD
#endif

/* CPU/system clock frequency in Hz.  The Tier-1 bootloader does NOT enable
 * the DPLL, so the app core runs at the BootROM default = the 26 MHz XTAL
 * (confirmed via armino sdkconfig.h CONFIG_XTAL_FREQ=26000000).
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

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA
int bk7258_t5_board_camera_initialize(void);
#endif

#ifdef CONFIG_BK7258_T5_BOARD_RGB_LCD_PWM_VALIDATION
int bk7258_t5_board_rgb_lcd_backlight_validation_initialize(void);
#endif

#endif /* __ARCH_BOARD_BK7258_BOARD_H */
