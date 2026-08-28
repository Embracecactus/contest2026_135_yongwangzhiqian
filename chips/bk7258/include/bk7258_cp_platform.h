/****************************************************************************
 * chips/bk7258/include/bk7258_cp_platform.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-owned BK7258 platform-stage implementation contract.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_CP_PLATFORM_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_CP_PLATFORM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/compiler.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Stable stage identifiers are shared with the board-owned lifecycle runner
 * so diagnostics retain one mask across chip and board work.
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

/* Execute one configured SoC stage.  The board lifecycle owns cross-layer
 * ordering and calls this function only for chip stages.  Storage topology
 * and GPIO wiring are explicit immutable inputs; chip code never discovers
 * the selected board through a global getter or callback table.
 */

int bk7258_cp_platform_run_stage(
  enum bk7258_cp_platform_stage_e stage,
  FAR const struct bk7258_storage_config_s *storage,
  FAR const struct bk7258_gpio_config_s *gpio);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_CP_PLATFORM_H */
