/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_wdt.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) hardware watchdog NuttX lower-half driver — SDK wrapper.
 *
 * Calls bk_wdt_* / bk_aon_wdt_* SDK APIs.  Zero register access.
 *
 * Fixes: AON WDT orphaned by bootloader → infinite reboot (F-01).
 * The bootloader arms both APB + AON WDTs (~8 s); this driver stops
 * AON WDT immediately on init and manages APB WDT via NuttX automonitor.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_WDT

#include <stdint.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/timers/watchdog.h>
#include <nuttx/wdog.h>

#include "bk7258_wdt.h"

/* SDK API headers */

#include <driver/wdt.h>
#include <driver/aon_wdt.h>
#include <driver/timer.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_WDT_DEFAULT_TIMEOUT_MS  8000u
#define BK7258_WDT_MAX_TIMEOUT_MS      0xFFFFu

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_wdt_lowerhalf_s
{
  struct watchdog_lowerhalf_s wdt_lh;  /* Must be first */
  uint32_t timeout;                    /* Current timeout in ms */
  bool     started;                    /* WDT is armed */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_wdt_start(struct watchdog_lowerhalf_s *lower);
static int bk7258_wdt_stop(struct watchdog_lowerhalf_s *lower);
static int bk7258_wdt_keepalive(struct watchdog_lowerhalf_s *lower);
static int bk7258_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                struct watchdog_status_s *status);
static int bk7258_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                                 uint32_t timeout);
static void bk7258_wdt_tick_probe(wdparm_t arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct watchdog_ops_s g_bk7258_wdt_ops =
{
  .start      = bk7258_wdt_start,
  .stop       = bk7258_wdt_stop,
  .keepalive  = bk7258_wdt_keepalive,
  .getstatus  = bk7258_wdt_getstatus,
  .settimeout = bk7258_wdt_settimeout,
};

static struct bk7258_wdt_lowerhalf_s g_bk7258_wdt;
static struct wdog_s g_bk7258_wdt_probe;

/****************************************************************************
 * Private: lower-half operations
 ****************************************************************************/

static void bk7258_wdt_tick_probe(wdparm_t arg)
{
  (void)arg;
  up_putc('T');
}

static int bk7258_wdt_start(struct watchdog_lowerhalf_s *lower)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;

  if (bk_wdt_start(priv->timeout) != BK_OK)
    {
      wderr("ERROR: bk_wdt_start failed\n");
      return -EIO;
    }

  priv->started = true;
  wdinfo("started, timeout=%" PRIu32 " ms\n", priv->timeout);
  return OK;
}

static int bk7258_wdt_stop(struct watchdog_lowerhalf_s *lower)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;

  bk_wdt_stop();
  bk_aon_wdt_stop();  /* Stop AON WDT (fixes F-01 reboot root cause) */

  priv->started = false;
  wdinfo("stopped\n");
  return OK;
}

static int bk7258_wdt_keepalive(struct watchdog_lowerhalf_s *lower)
{
  static int marker_printed;

  if (marker_printed == 0)
    {
      marker_printed = 1;
      up_putc('K');
    }

  return (bk_wdt_feed() == BK_OK) ? OK : -EIO;
}

static int bk7258_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                struct watchdog_status_s *status)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;

  status->flags = WDFLAGS_RESET;
  if (priv->started)
    {
      status->flags |= WDFLAGS_ACTIVE;
    }

  status->timeout = priv->timeout;

  /* timeleft: we cannot read the current counter from the BK7258 APB WDT
   * (no readable counter register).  Return timeout as an upper bound. */

  status->timeleft = priv->timeout;
  return OK;
}

static int bk7258_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                                 uint32_t timeout)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;

  if (timeout == 0)
    {
      return -EINVAL;
    }

  if (timeout > BK7258_WDT_MAX_TIMEOUT_MS)
    {
      timeout = BK7258_WDT_MAX_TIMEOUT_MS;
    }

  priv->timeout = timeout;

  /* If already running, re-arm with new period.
   * bk_wdt_start() internally does soft_reset + key sequence. */

  if (priv->started)
    {
      bk_wdt_start(priv->timeout);
    }

  wdinfo("timeout set to %" PRIu32 " ms\n", priv->timeout);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_wdt_initialize
 *
 * Description:
 *   Initialize the BK7258 WDT and register as /dev/watchdog0.
 *
 *   ★ Root cause fix: stops AON WDT immediately.  Bootloader arms both
 *   APB + AON WDTs; AON is not managed by NuttX watchdog framework and
 *   would expire ~8 s after boot → reboot loop.
 *
 *   APB WDT is managed by NuttX automonitor (CONFIG_WATCHDOG_AUTOMONITOR):
 *   register triggers start + periodic keepalive via work queue.
 *
 ****************************************************************************/

int bk7258_wdt_initialize(void)
{
  static bool s_inited;
  struct bk7258_wdt_lowerhalf_s *priv = &g_bk7258_wdt;
  void *handle;
  int ret;

  if (s_inited)
    {
      return OK;
    }

  s_inited = true;

  /* Initialize the SDK WDT state, then stop its TIMER_ID2 feeder.  NuttX
   * automonitor owns periodic keepalive through bk_wdt_feed(). */

  bk_timer_driver_init();

  if (!bk_wdt_is_driver_inited())
    {
      bk_wdt_driver_init();
    }

  bk_timer_stop(TIMER_ID2);

  /* Stop the bootloader's AON WDT before registering the NuttX lower-half.
   * AON WDT is not managed by the NuttX watchdog framework. */

  bk_aon_wdt_stop();

  priv->wdt_lh.ops = &g_bk7258_wdt_ops;
  priv->timeout    = BK7258_WDT_DEFAULT_TIMEOUT_MS;
  priv->started    = false;

  up_putc('A');
  handle = watchdog_register("/dev/watchdog0",
                             (struct watchdog_lowerhalf_s *)priv);
  if (handle == NULL)
    {
      wderr("ERROR: watchdog_register failed\n");
      return -ENOMEM;
    }

  up_putc('B');

  ret = wd_start(&g_bk7258_wdt_probe, 1,
                 bk7258_wdt_tick_probe, 0);
  up_putc(ret == OK ? 'W' : 'E');

  wdinfo("BK7258 WDT registered, default timeout=%" PRIu32 " ms\n",
         priv->timeout);
  return OK;
}

#endif /* CONFIG_BK7258_WDT */
