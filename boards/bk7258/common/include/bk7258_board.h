/****************************************************************************
 * boards/bk7258/common/include/bk7258_board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared board header for the Beken BK7258 logical board.
 * NuttX's configure step exposes this via <arch/board/board.h>.
 ****************************************************************************/

#ifndef __ARCH_BOARD_BK7258_BOARD_H
#define __ARCH_BOARD_BK7258_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <arch/chip/bk7258_image_layout.h>
#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_console.h>
#include <arch/chip/bk7258_gpio.h>

#ifdef CONFIG_BK7258_AP_CORE
#  include <arch/chip/bk7258_aud.h>
#  include <arch/chip/bk7258_mic.h>
#  include <arch/chip/bk7258_sdio.h>
#endif

#ifdef CONFIG_BK7258_OTA_AUTO_CONFIRM
#  include "bk7258_ota_trial.h"
#endif

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

#ifdef CONFIG_BK7258_AP_CORE
struct bk7258_i2s_board_s;
#endif

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

#ifdef __cplusplus
extern "C"
{
#endif

/* Immutable physical wiring selected at build time.  Board code passes this
 * record explicitly to chip GPIO users; chip code never discovers a board.
 */

extern const struct bk7258_gpio_config_s g_bk7258_board_gpio_config;

#if defined(CONFIG_BK7258_TOUCH) && !defined(CONFIG_BK7258_AP_CORE)
int bk7258_board_cp_devices_initialize(void);
#endif

#ifdef CONFIG_BK7258_AP_CORE

/* Shared board-layer AP registration phases.  A physical board calls these
 * around its own pre-device and attached-device initialization so controller
 * and device ordering remains explicit.
 */

int bk7258_board_ap_controllers_initialize(
  FAR const struct bk7258_mic_config_s *mic,
  FAR const struct bk7258_aud_board_s *audio);
int bk7258_board_ap_buses_initialize(
  FAR const struct bk7258_i2s_board_s *i2s,
  FAR const struct bk7258_sdio_board_s *sdio);
int bk7258_board_ap_finalize_initialize(void);

/* Implemented by the selected physical board. */

int bk7258_board_ap_initialize(void);

#ifdef CONFIG_BK7258_AUD
extern const struct bk7258_aud_board_s g_bk7258_board_audio;
#endif

#ifdef CONFIG_BK7258_BOARD_HAS_LVGL_UI_BINDING
int bk7258_board_ui_initialize(void);
int bk7258_board_ui_wait_ready(void);
#endif

#endif /* CONFIG_BK7258_AP_CORE */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_BOARD_BK7258_BOARD_H */
