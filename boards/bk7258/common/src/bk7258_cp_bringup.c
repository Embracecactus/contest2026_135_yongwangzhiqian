/****************************************************************************
 * boards/bk7258/common/src/bk7258_cp_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Thin CP board bridge.  Chip code owns the SoC sequence and cached status;
 * this file supplies immutable board bindings and executes board checkpoints.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef CONFIG_BK7258_AP_CORE

#include <stdbool.h>

#include <errno.h>

#include <nuttx/mutex.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_cp_platform.h>

#include "bk7258_internal.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_bk7258_cp_bringup_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_cp_bringup_run(void)
{
  FAR const struct bk7258_storage_config_s *storage = NULL;
#if defined(CONFIG_BK7258_OTA) || defined(CONFIG_BK7258_TOUCH)
  bool eligible;
#endif
  int ret;

#if defined(CONFIG_BK7258_OTA) || \
    defined(CONFIG_BK7258_WDT_PRETIMEOUT_PANIC) || \
    defined(CONFIG_BK7258_FLASH_MTD) || \
    defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
  storage = &g_bk7258_board_storage_config;
#endif

  ret = bk7258_cp_platform_begin(storage, &g_bk7258_board_gpio_config);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_OTA
  ret = bk7258_cp_platform_reach_ota_trial(&eligible);
  if (ret < 0)
    {
      return ret;
    }

  if (eligible)
    {
#  ifdef CONFIG_BK7258_OTA_AUTO_CONFIRM
      int stage_result = bk7258_ota_trial_initialize();
#  else
      int stage_result = OK;
#  endif

      ret = bk7258_cp_platform_complete_ota_trial(stage_result);
      if (ret < 0)
        {
          return ret;
        }
    }
#endif

#ifdef CONFIG_BK7258_TOUCH
  ret = bk7258_cp_platform_reach_board_devices(&eligible);
  if (ret < 0)
    {
      return ret;
    }

  if (eligible)
    {
      int stage_result = bk7258_board_cp_devices_initialize();

      ret = bk7258_cp_platform_complete_board_devices(stage_result);
      if (ret < 0)
        {
          return ret;
        }
    }
#endif

  return bk7258_cp_platform_finish();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_cp_bringup_initialize(void)
{
  struct bk7258_platform_status_s status;
  int ret = nxmutex_lock(&g_bk7258_cp_bringup_lock);

  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_cp_platform_get_status(&status);
  if (ret < 0)
    {
      goto out;
    }

  if (status.state == BK7258_PLATFORM_DONE)
    {
      ret = status.terminal_result;
    }
  else if (status.state != BK7258_PLATFORM_NEW)
    {
      ret = -EBUSY;
    }
  else
    {
      ret = bk7258_cp_bringup_run();
    }

out:
  nxmutex_unlock(&g_bk7258_cp_bringup_lock);
  return ret;
}

int bk7258_cp_bringup_result(void)
{
  return bk7258_cp_platform_result();
}

#endif /* !CONFIG_BK7258_AP_CORE */
