/****************************************************************************
 * chips/bk7258/include/bk7258_cp_platform.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-owned BK7258 platform lifecycle contract.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_CP_PLATFORM_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_CP_PLATFORM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>

#include <nuttx/compiler.h>

#include <arch/chip/bk7258_platform.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Stable stage identifiers describe the CP-owned lifecycle status.  The two
 * board-owned operations are external checkpoints: board code executes them
 * and reports their result, while chip code retains ordering and failure
 * propagation without calling or discovering a board symbol.
 */

enum bk7258_cp_platform_stage_e
{
  BK7258_CP_STAGE_TRACE_ENTRY = 0,
  BK7258_CP_STAGE_STORAGE_CONFIG,
  BK7258_CP_STAGE_OTA_LAYOUT,
  BK7258_CP_STAGE_RESET_MARKER_POLICY,
  BK7258_CP_STAGE_SDK_RUNTIME,
  BK7258_CP_STAGE_DEBUG_ROUTE_AFTER_SDK,
  BK7258_CP_STAGE_SARADC_SERVER,
  BK7258_CP_STAGE_PM_SERVER,
  BK7258_CP_STAGE_TEMPERATURE_SERVER,
  BK7258_CP_STAGE_RADIO_STORAGE,
  BK7258_CP_STAGE_WIFI_CONTROLLER,
  BK7258_CP_STAGE_BT_CONTROLLER,
  BK7258_CP_STAGE_AP_CONTROL,
  BK7258_CP_STAGE_GPIO_SERVER,
  BK7258_CP_STAGE_PSRAM,
  BK7258_CP_STAGE_AP_SUPERVISOR,
  BK7258_CP_STAGE_AP_START,
  BK7258_CP_STAGE_OTA_TRIAL,
  BK7258_CP_STAGE_WIFI_CONTROL,
  BK7258_CP_STAGE_WDT,
  BK7258_CP_STAGE_IRDA,
  BK7258_CP_STAGE_PM_COORD,
  BK7258_CP_STAGE_GPIO_FALLBACK,
  BK7258_CP_STAGE_BOARD_DEVICES,
  BK7258_CP_STAGE_DEBUG_ROUTE_FINAL,
  BK7258_CP_STAGE_COUNT
};

struct bk7258_storage_config_s;
struct bk7258_gpio_config_s;

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Bind immutable board storage/GPIO facts before the first lifecycle step.
 * Checkpoints must be reached and, when eligible, completed in declaration
 * order.  The reach/complete calls return only protocol errors; stage errors
 * remain cached so later ALWAYS_RUN work is not suppressed.  finish() seals
 * and returns the first mandatory failure.  Chip code never includes, calls
 * or discovers a concrete board implementation.
 */

int bk7258_cp_platform_begin(
  FAR const struct bk7258_storage_config_s *storage,
  FAR const struct bk7258_gpio_config_s *gpio);
int bk7258_cp_platform_reach_ota_trial(FAR bool *eligible);
int bk7258_cp_platform_complete_ota_trial(int result);
int bk7258_cp_platform_reach_board_devices(FAR bool *eligible);
int bk7258_cp_platform_complete_board_devices(int result);
int bk7258_cp_platform_finish(void);
int bk7258_cp_platform_result(void);
int bk7258_cp_platform_get_status(
  FAR struct bk7258_platform_status_s *status);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_CP_PLATFORM_H */
