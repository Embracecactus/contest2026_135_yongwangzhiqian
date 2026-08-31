/****************************************************************************
 * chips/bk7258/ap/bk7258_usbmode.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 USB0 dynamic CDC/MSC mode manager.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_USBMODE

#include <errno.h>
#include <syslog.h>

#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_usbcdc.h>
#include <arch/chip/bk7258_usbmsc.h>
#include <arch/chip/bk7258_usbmode.h>

static mutex_t g_bk7258_usbmode_lock = NXMUTEX_INITIALIZER;
static enum bk7258_usbmode_e g_bk7258_usbmode = BK7258_USBMODE_NONE;

const char *bk7258_usbmode_name(enum bk7258_usbmode_e mode)
{
  switch (mode)
    {
      case BK7258_USBMODE_CDC:
        return "cdc";
      case BK7258_USBMODE_MSC:
        return "msc";
      default:
        return "none";
    }
}

static int bk7258_usbmode_start(enum bk7258_usbmode_e mode)
{
  switch (mode)
    {
      case BK7258_USBMODE_CDC:
        return bk7258_usbcdc_initialize();
      case BK7258_USBMODE_MSC:
        return bk7258_usbmsc_initialize(CONFIG_BK7258_USBMODE_BLOCKDEV);
      default:
        return -EINVAL;
    }
}

static int bk7258_usbmode_stop(enum bk7258_usbmode_e mode)
{
  switch (mode)
    {
      case BK7258_USBMODE_CDC:
        return bk7258_usbcdc_uninitialize();
      case BK7258_USBMODE_MSC:
        return bk7258_usbmsc_uninitialize();
      case BK7258_USBMODE_NONE:
        return OK;
      default:
        return -EINVAL;
    }
}

int bk7258_usbmode_initialize(void)
{
  int ret;

  ret = nxmutex_lock(&g_bk7258_usbmode_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_usbmode != BK7258_USBMODE_NONE)
    {
      nxmutex_unlock(&g_bk7258_usbmode_lock);
      return -EALREADY;
    }

  ret = bk7258_usbmode_start(BK7258_USBMODE_CDC);
  if (ret >= 0)
    {
      g_bk7258_usbmode = BK7258_USBMODE_CDC;
    }

  nxmutex_unlock(&g_bk7258_usbmode_lock);
  return ret;
}

int bk7258_usbmode_set(enum bk7258_usbmode_e mode)
{
  enum bk7258_usbmode_e previous;
  int rollback;
  int ret;

  if (mode != BK7258_USBMODE_CDC && mode != BK7258_USBMODE_MSC)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_usbmode_lock);
  if (ret < 0)
    {
      return ret;
    }

  previous = g_bk7258_usbmode;
  if (previous == mode)
    {
      nxmutex_unlock(&g_bk7258_usbmode_lock);
      return OK;
    }

  ret = bk7258_usbmode_stop(previous);
  if (ret < 0)
    {
      nxmutex_unlock(&g_bk7258_usbmode_lock);
      return ret;
    }

  g_bk7258_usbmode = BK7258_USBMODE_NONE;
  (void)nxsig_usleep(CONFIG_BK7258_USBMODE_REENUM_DELAY_MS * 1000u);
  ret = bk7258_usbmode_start(mode);
  if (ret >= 0)
    {
      g_bk7258_usbmode = mode;
      syslog(LOG_INFO, "BK7258 USBMODE: %s -> %s\n",
             bk7258_usbmode_name(previous), bk7258_usbmode_name(mode));
      nxmutex_unlock(&g_bk7258_usbmode_lock);
      return OK;
    }

  syslog(LOG_ERR, "BK7258 USBMODE: start %s failed: %d\n",
         bk7258_usbmode_name(mode), ret);
  if (previous != BK7258_USBMODE_NONE)
    {
      (void)nxsig_usleep(CONFIG_BK7258_USBMODE_REENUM_DELAY_MS * 1000u);
      rollback = bk7258_usbmode_start(previous);
      if (rollback >= 0)
        {
          g_bk7258_usbmode = previous;
        }
      else
        {
          syslog(LOG_ERR, "BK7258 USBMODE: rollback %s failed: %d\n",
                 bk7258_usbmode_name(previous), rollback);
        }
    }

  nxmutex_unlock(&g_bk7258_usbmode_lock);
  return ret;
}

enum bk7258_usbmode_e bk7258_usbmode_get(void)
{
  enum bk7258_usbmode_e mode;

  if (nxmutex_lock(&g_bk7258_usbmode_lock) < 0)
    {
      return BK7258_USBMODE_NONE;
    }

  mode = g_bk7258_usbmode;
  nxmutex_unlock(&g_bk7258_usbmode_lock);
  return mode;
}

#endif /* CONFIG_BK7258_USBMODE */
