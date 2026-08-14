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

#include <stdbool.h>

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

/* Cold-reset CPU-clock fallback in Hz.  Runtime code reads the live mux and
 * divider because BL1 and CP-owned DVFS can leave the cores at a higher
 * operating point.  Scheduler SysTick instead uses the independently routed
 * fixed 32-kHz source and does not derive its reload from this macro.
 *
 * NOTE: NuttX has no CONFIG_CPU_FREQ_HZ Kconfig symbol; chips expose the
 * clock as a header macro (cf. mps MPS_SYSTICK_CLOCK).  Board-side
 * calibration TODO: keep this fallback aligned with the XTAL baseline.
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

#ifdef CONFIG_BK7258_T5_BOARD_TF_VALIDATION
int bk7258_t5_board_tf_validation_initialize(void);
#endif

#ifdef CONFIG_BK7258_AUD
int bk7258_board_speaker_initialize(void);
int bk7258_board_speaker_set(bool enable);
bool bk7258_board_speaker_is_enabled(void);
#endif

#ifdef CONFIG_BK7258_AP_CORE
/* Selected physical-board hooks.  Generic AP controller wrappers call these
 * at stable ordering boundaries; the variant implementation owns attached
 * devices and all fixed electrical policy.
 */

int bk7258_board_early_initialize(void);
int bk7258_board_devices_initialize(void);
#endif

#endif /* __ARCH_BOARD_BK7258_BOARD_H */
