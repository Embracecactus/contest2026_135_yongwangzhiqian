/****************************************************************************
 * chips/bk7258/ap/bk7258_radio_mode.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-owned arbitration for radio modes which the pinned SDK does not prove
 * can run concurrently.  Normal Wi-Fi STA/BLE coexistence remains owned by
 * the SDK controller; only monitor and active scan sessions are serialized.
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BK7258_AP_CORE) && \
    (defined(CONFIG_BK7258_WIFI_VNET) || \
     defined(CONFIG_BK7258_BT_IPC_TEST))

#include <errno.h>
#include <string.h>

#include <nuttx/mutex.h>

#include <arch/chip/bk7258_radio_mode.h>

static mutex_t g_bk7258_radio_mode_lock = NXMUTEX_INITIALIZER;
static struct bk7258_radio_mode_status_s g_bk7258_radio_mode;

int bk7258_radio_mode_acquire(enum bk7258_radio_mode_e mode)
{
  int ret;

  if (mode <= BK7258_RADIO_MODE_IDLE || mode > BK7258_RADIO_MODE_BLE_SCAN)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_radio_mode_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_radio_mode.mode != BK7258_RADIO_MODE_IDLE)
    {
      g_bk7258_radio_mode.conflicts++;
      ret = -EBUSY;
    }
  else
    {
      if (++g_bk7258_radio_mode.generation == 0)
        {
          g_bk7258_radio_mode.generation++;
        }

      g_bk7258_radio_mode.mode = (uint32_t)mode;
      ret = OK;
    }

  nxmutex_unlock(&g_bk7258_radio_mode_lock);
  return ret;
}

int bk7258_radio_mode_release(enum bk7258_radio_mode_e mode)
{
  int ret;

  if (mode <= BK7258_RADIO_MODE_IDLE || mode > BK7258_RADIO_MODE_BLE_SCAN)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_radio_mode_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_radio_mode.mode != (uint32_t)mode)
    {
      ret = -EPERM;
    }
  else
    {
      g_bk7258_radio_mode.mode = BK7258_RADIO_MODE_IDLE;
      ret = OK;
    }

  nxmutex_unlock(&g_bk7258_radio_mode_lock);
  return ret;
}

int bk7258_radio_mode_status(struct bk7258_radio_mode_status_s *status)
{
  int ret;

  if (status == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_radio_mode_lock);
  if (ret < 0)
    {
      return ret;
    }

  memcpy(status, &g_bk7258_radio_mode, sizeof(*status));
  nxmutex_unlock(&g_bk7258_radio_mode_lock);
  return OK;
}

#endif
