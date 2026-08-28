/****************************************************************************
 * boards/bk7258/common/src/bk7258_cp_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-owned CP lifecycle.  This is the single place that orders chip
 * services with board storage, product policy and physical-device work.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef CONFIG_BK7258_AP_CORE

#include <errno.h>
#include <stdint.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_cp_platform.h>
#include <arch/chip/bk7258_stage_runner.h>

#include "bk7258_internal.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_cp_bringup_context_s
{
  FAR const struct bk7258_storage_config_s *storage;
  FAR const struct bk7258_gpio_config_s *gpio;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_cp_bringup_stage_execute(
  FAR void *arg, FAR const struct bk7258_stage_desc_s *stage)
{
  FAR const struct bk7258_cp_bringup_context_s *context = arg;

  switch ((enum bk7258_cp_platform_stage_e)stage->id)
    {
#ifdef CONFIG_BK7258_OTA
      case BK7258_CP_STAGE_OTA_TRIAL:
#  ifdef CONFIG_BK7258_OTA_AUTO_CONFIRM
        return bk7258_ota_trial_initialize();
#  else
        return OK;
#  endif
#endif

#ifdef CONFIG_BK7258_TOUCH
      case BK7258_CP_STAGE_BOARD_DEVICES:
        return bk7258_board_cp_devices_initialize();
#endif

      default:
        return bk7258_cp_platform_run_stage(
                 (enum bk7258_cp_platform_stage_e)stage->id,
                 context->storage, context->gpio);
    }
}

/****************************************************************************
 * Private Data
 ****************************************************************************/

#define BK7258_CP_STAGE_BIT(_id) (UINT32_C(1) << (_id))
#define BK7258_CP_STAGE(_id, _class, _flags, _requires) \
  {(_requires), (_id), (_class), (_flags)}
#define BK7258_CP_STAGE_COUNT_VALUE \
  (sizeof(g_bk7258_cp_stages) / sizeof(g_bk7258_cp_stages[0]))

#ifdef CONFIG_BK7258_WDT_PRETIMEOUT_PANIC
#  define BK7258_CP_WDT_REQUIRES \
  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_RESET_MARKER_POLICY)
#else
#  define BK7258_CP_WDT_REQUIRES \
  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)
#endif

#if defined(CONFIG_BK7258_AP_CONTROL) && \
    defined(CONFIG_BK7258_AP_AUTOSTART)
#  define BK7258_CP_PM_COORD_AP_REQUIRES \
  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_AP_START)
#elif defined(CONFIG_BK7258_AP_CONTROL)
#  define BK7258_CP_PM_COORD_AP_REQUIRES \
  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_AP_CONTROL)
#else
#  define BK7258_CP_PM_COORD_AP_REQUIRES 0
#endif

#ifdef CONFIG_BK7258_PM_CLOCK
#  define BK7258_CP_PM_COORD_REQUIRES \
  (BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_PM_SERVER) | \
   BK7258_CP_PM_COORD_AP_REQUIRES)
#else
#  define BK7258_CP_PM_COORD_REQUIRES BK7258_CP_PM_COORD_AP_REQUIRES
#endif

static const struct bk7258_stage_desc_s g_bk7258_cp_stages[] =
{
#ifdef CONFIG_BK7258_SWD_DEBUG
  BK7258_CP_STAGE(BK7258_CP_STAGE_TRACE_ENTRY,
                  BK7258_STAGE_BEST_EFFORT,
                  BK7258_STAGE_FLAG_ALWAYS_RUN, 0),
#endif
  BK7258_CP_STAGE(BK7258_CP_STAGE_STORAGE_CONFIG,
                  BK7258_STAGE_MANDATORY, 0, 0),
#ifdef CONFIG_BK7258_OTA
  BK7258_CP_STAGE(BK7258_CP_STAGE_OTA_LAYOUT,
                  BK7258_STAGE_MANDATORY, 0,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_WDT_PRETIMEOUT_PANIC
  BK7258_CP_STAGE(BK7258_CP_STAGE_RESET_MARKER_POLICY,
                  BK7258_STAGE_MANDATORY,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_SDK_IPC_RUNTIME
  BK7258_CP_STAGE(BK7258_CP_STAGE_SDK_RUNTIME,
                  BK7258_STAGE_MANDATORY, 0,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_SWD_DEBUG
  BK7258_CP_STAGE(BK7258_CP_STAGE_DEBUG_ROUTE_AFTER_SDK,
                  BK7258_STAGE_BEST_EFFORT,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_SARADC_SERVER
  BK7258_CP_STAGE(BK7258_CP_STAGE_SARADC_SERVER,
                  BK7258_STAGE_MANDATORY, 0, 0),
#endif
#ifdef CONFIG_BK7258_PM_CLOCK
  BK7258_CP_STAGE(BK7258_CP_STAGE_PM_SERVER,
                  BK7258_STAGE_MANDATORY, 0, 0),
#endif
#ifdef CONFIG_BK7258_TEMPERATURE
  BK7258_CP_STAGE(BK7258_CP_STAGE_TEMPERATURE_SERVER,
                  BK7258_STAGE_MANDATORY, 0, 0),
#endif
#if defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
  BK7258_CP_STAGE(BK7258_CP_STAGE_RADIO_STORAGE,
                  BK7258_STAGE_MANDATORY, 0,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
  BK7258_CP_STAGE(BK7258_CP_STAGE_WIFI_CONTROLLER,
                  BK7258_STAGE_MANDATORY, 0,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_RADIO_STORAGE)),
#endif
#ifdef CONFIG_BK7258_BT_IPC
  BK7258_CP_STAGE(BK7258_CP_STAGE_BT_CONTROLLER,
                  BK7258_STAGE_MANDATORY, 0,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_RADIO_STORAGE)),
#endif
#ifdef CONFIG_BK7258_AP_CONTROL
  BK7258_CP_STAGE(BK7258_CP_STAGE_AP_CONTROL,
                  BK7258_STAGE_MANDATORY, 0, 0),
#  ifdef CONFIG_BK7258_GPIO_LOWERHALF
  BK7258_CP_STAGE(BK7258_CP_STAGE_GPIO_SERVER,
                  BK7258_STAGE_MANDATORY, 0, 0),
#  endif
#endif
#ifdef CONFIG_BK7258_PSRAM
  BK7258_CP_STAGE(BK7258_CP_STAGE_PSRAM,
                  BK7258_STAGE_MANDATORY,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_AP_SUPERVISOR
  BK7258_CP_STAGE(BK7258_CP_STAGE_AP_SUPERVISOR,
                  BK7258_STAGE_MANDATORY, 0, 0),
#endif
#if defined(CONFIG_BK7258_AP_CONTROL) && \
    defined(CONFIG_BK7258_AP_AUTOSTART)
  BK7258_CP_STAGE(BK7258_CP_STAGE_AP_START,
                  BK7258_STAGE_MANDATORY, 0, 0),
#endif
#ifdef CONFIG_BK7258_OTA
  BK7258_CP_STAGE(BK7258_CP_STAGE_OTA_TRIAL,
                  BK7258_STAGE_MANDATORY,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_OTA_LAYOUT)),
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
  BK7258_CP_STAGE(BK7258_CP_STAGE_WIFI_CONTROL,
                  BK7258_STAGE_MANDATORY, 0, 0),
#endif
#ifdef CONFIG_BK7258_WDT
  BK7258_CP_STAGE(BK7258_CP_STAGE_WDT,
                  BK7258_STAGE_MANDATORY,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_WDT_REQUIRES),
#endif
#ifdef CONFIG_BK7258_IRDA
  BK7258_CP_STAGE(BK7258_CP_STAGE_IRDA,
                  BK7258_STAGE_MANDATORY,
                  BK7258_STAGE_FLAG_ALWAYS_RUN, 0),
#endif
#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
  BK7258_CP_STAGE(BK7258_CP_STAGE_PM_COORD,
                  BK7258_STAGE_BEST_EFFORT,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_PM_COORD_REQUIRES),
#endif
#if defined(CONFIG_BK7258_GPIO_LOWERHALF) && \
    !defined(CONFIG_BK7258_AP_CONTROL)
  BK7258_CP_STAGE(BK7258_CP_STAGE_GPIO_FALLBACK,
                  BK7258_STAGE_BEST_EFFORT,
                  BK7258_STAGE_FLAG_ALWAYS_RUN, 0),
#endif
#ifdef CONFIG_BK7258_TOUCH
  BK7258_CP_STAGE(BK7258_CP_STAGE_BOARD_DEVICES,
                  BK7258_STAGE_BEST_EFFORT,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_SWD_DEBUG
  BK7258_CP_STAGE(BK7258_CP_STAGE_DEBUG_ROUTE_FINAL,
                  BK7258_STAGE_BEST_EFFORT,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
};

static const struct bk7258_cp_bringup_context_s g_bk7258_cp_context =
{
#if defined(CONFIG_BK7258_OTA) || \
    defined(CONFIG_BK7258_WDT_PRETIMEOUT_PANIC) || \
    defined(CONFIG_BK7258_FLASH_MTD) || \
    defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
  .storage = &g_bk7258_board_storage_config,
#else
  .storage = NULL,
#endif
  .gpio = &g_bk7258_board_gpio_config,
};

static struct bk7258_stage_runner_s g_bk7258_cp_runner =
{
  .lock = NXMUTEX_INITIALIZER,
  .stages = g_bk7258_cp_stages,
  .execute = bk7258_cp_bringup_stage_execute,
  .stage_count = BK7258_CP_STAGE_COUNT_VALUE,
  .state = BK7258_PLATFORM_NEW,
  .first_error_stage = BK7258_STAGE_ID_INVALID,
};

_Static_assert(BK7258_CP_STAGE_COUNT <= BK7258_STAGE_ID_LIMIT,
               "CP platform stage ID exceeds the status mask");
_Static_assert(BK7258_CP_STAGE_COUNT_VALUE > 0 &&
               BK7258_CP_STAGE_COUNT_VALUE <= BK7258_STAGE_ID_LIMIT,
               "invalid CP platform stage table size");

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_cp_bringup_initialize(void)
{
  return bk7258_stage_runner_run(&g_bk7258_cp_runner,
                                 (FAR void *)&g_bk7258_cp_context);
}

int bk7258_cp_bringup_result(void)
{
  return bk7258_stage_runner_result(&g_bk7258_cp_runner);
}

int bk7258_cp_bringup_get_status(
  FAR struct bk7258_platform_status_s *status)
{
  return bk7258_stage_runner_snapshot(&g_bk7258_cp_runner, status);
}

#endif /* !CONFIG_BK7258_AP_CORE */
