/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_wdt.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 hardware watchdog NuttX lower-half driver — SDK wrapper.
 *
 * Calls bk_wdt_* / bk_aon_wdt_* SDK APIs.  Zero register access.
 *
 * The bootloader arms both APB + AON WDTs (~8 s).  The CP reset entry closes
 * both before nx_start(); this driver is registered after bounded AP
 * autostart and manages the APB WDT through the NuttX automonitor.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_WDT

#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/timers/watchdog.h>

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

#ifdef CONFIG_BK7258_WDT_FAULT_INJECTION
#  define BK7258_WDT_FAULT_MAGIC       0x46445742u /* "BWDF" */
#  define BK7258_WDT_FAULT_VERSION     1u
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_wdt_lowerhalf_s
{
  struct watchdog_lowerhalf_s wdt_lh;  /* Must be first */
  uint32_t timeout;                    /* Current timeout in ms */
  bool     started;                    /* WDT is armed */
};

#ifdef CONFIG_BK7258_WDT_FAULT_INJECTION
enum bk7258_wdt_fault_e
{
  BK7258_WDT_FAULT_NONE = 0,
  BK7258_WDT_FAULT_TIMER_STOP,
  BK7258_WDT_FAULT_AON_STOP,
  BK7258_WDT_FAULT_WDT_STOP,
  BK7258_WDT_FAULT_WDT_START,
};

struct bk7258_wdt_fault_diag_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t state;
  int32_t result;
  int32_t init_timer_stop;
  int32_t init_aon_stop;
  int32_t init_retry;
  int32_t failed_stop;
  uint32_t active_after_failed_stop;
  int32_t retry_stop;
  uint32_t active_after_retry_stop;
  int32_t restart;
  uint32_t active_after_failed_restore;
  int32_t recovery_start;
  uint32_t final_active;
};
#endif

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
static bool g_bk7258_wdt_pm_resume;

#ifdef CONFIG_BK7258_WDT_FAULT_INJECTION
volatile struct bk7258_wdt_fault_diag_s g_bk7258_wdt_fault_diag;
static volatile enum bk7258_wdt_fault_e g_bk7258_wdt_fault_next;

static bool bk7258_wdt_fault_take(enum bk7258_wdt_fault_e fault)
{
  irqstate_t flags;
  bool take;

  flags = up_irq_save();
  take = g_bk7258_wdt_fault_next == fault;
  if (take)
    {
      g_bk7258_wdt_fault_next = BK7258_WDT_FAULT_NONE;
    }

  up_irq_restore(flags);
  return take;
}

static bk_err_t bk7258_wdt_sdk_timer_stop(timer_id_t timer_id)
{
  if (bk7258_wdt_fault_take(BK7258_WDT_FAULT_TIMER_STOP))
    {
      return (bk_err_t)-1;
    }

  return bk_timer_stop(timer_id);
}

static bk_err_t bk7258_wdt_sdk_aon_stop(void)
{
  if (bk7258_wdt_fault_take(BK7258_WDT_FAULT_AON_STOP))
    {
      return (bk_err_t)-1;
    }

  return bk_aon_wdt_stop();
}

static bk_err_t bk7258_wdt_sdk_stop(void)
{
  if (bk7258_wdt_fault_take(BK7258_WDT_FAULT_WDT_STOP))
    {
      return (bk_err_t)-1;
    }

  return bk_wdt_stop();
}

static bk_err_t bk7258_wdt_sdk_start(uint32_t timeout)
{
  if (bk7258_wdt_fault_take(BK7258_WDT_FAULT_WDT_START))
    {
      return (bk_err_t)-1;
    }

  return bk_wdt_start(timeout);
}
#else
#  define bk7258_wdt_sdk_timer_stop bk_timer_stop
#  define bk7258_wdt_sdk_aon_stop   bk_aon_wdt_stop
#  define bk7258_wdt_sdk_stop       bk_wdt_stop
#  define bk7258_wdt_sdk_start      bk_wdt_start
#endif

/****************************************************************************
 * Private: lower-half operations
 ****************************************************************************/

static int bk7258_wdt_start(struct watchdog_lowerhalf_s *lower)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;

  if (bk7258_wdt_sdk_start(priv->timeout) != BK_OK)
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

  if (bk7258_wdt_sdk_stop() != BK_OK)
    {
      wderr("ERROR: bk_wdt_stop failed\n");
      return -EIO;
    }

  /* The NuttX lower half owns the APB watchdog.  Commit its stopped state
   * after that hardware transition succeeds, even if the independent AON
   * watchdog subsequently reports an error.
   */

  priv->started = false;
  if (bk7258_wdt_sdk_aon_stop() != BK_OK)
    {
      wderr("ERROR: bk_aon_wdt_stop failed\n");
      return -EIO;
    }

  wdinfo("stopped\n");
  return OK;
}

static int bk7258_wdt_keepalive(struct watchdog_lowerhalf_s *lower)
{
  return (bk_wdt_feed() == BK_OK) ? OK : -EIO;
}

static int bk7258_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                struct watchdog_status_s *status)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;

  if (status == NULL)
    {
      return -EINVAL;
    }

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
  uint32_t previous;

  if (timeout == 0)
    {
      return -EINVAL;
    }

  if (timeout > BK7258_WDT_MAX_TIMEOUT_MS)
    {
      timeout = BK7258_WDT_MAX_TIMEOUT_MS;
    }

  previous = priv->timeout;
  priv->timeout = timeout;

  /* If already running, re-arm with new period.
   * bk_wdt_start() internally does soft_reset + key sequence. */

  if (priv->started)
    {
      if (bk7258_wdt_sdk_start(priv->timeout) != BK_OK)
        {
          priv->timeout = previous;
          return -EIO;
        }
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
 *   Keep AON WDT disabled and register the APB WDT for NuttX automonitor.
 *   The CP reset entry has already closed both bootloader watchdogs before
 *   nx_start(), so registration establishes a fresh OS-owned timeout.
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
  bk_err_t err;

  if (s_inited)
    {
      return OK;
    }

  /* Initialize the SDK WDT state, then stop its TIMER_ID2 feeder.  NuttX
   * automonitor owns periodic keepalive through bk_wdt_feed(). */

  err = bk_timer_driver_init();
  if (err != BK_OK)
    {
      wderr("ERROR: bk_timer_driver_init failed\n");
      return -EIO;
    }

  if (!bk_wdt_is_driver_inited())
    {
      err = bk_wdt_driver_init();
      if (err != BK_OK)
        {
          wderr("ERROR: bk_wdt_driver_init failed\n");
          return -EIO;
        }
    }

  err = bk7258_wdt_sdk_timer_stop(TIMER_ID2);
  if (err != BK_OK)
    {
      wderr("ERROR: failed to stop SDK WDT feeder timer\n");
      return -EIO;
    }

  /* Stop the bootloader's AON WDT before registering the NuttX lower-half.
   * AON WDT is not managed by the NuttX watchdog framework. */

  err = bk7258_wdt_sdk_aon_stop();
  if (err != BK_OK)
    {
      wderr("ERROR: failed to stop boot AON watchdog\n");
      return -EIO;
    }

  priv->wdt_lh.ops = &g_bk7258_wdt_ops;
  priv->timeout    = BK7258_WDT_DEFAULT_TIMEOUT_MS;
  priv->started    = false;

  handle = watchdog_register("/dev/watchdog0",
                             (struct watchdog_lowerhalf_s *)priv);
  if (handle == NULL)
    {
      wderr("ERROR: watchdog_register failed\n");
      return -ENOMEM;
    }

  s_inited = true;

  wdinfo("BK7258 WDT registered, default timeout=%" PRIu32 " ms\n",
         priv->timeout);
  return OK;
}

int bk7258_wdt_service(void)
{
  if (!g_bk7258_wdt.started)
    {
      return OK;
    }

  return bk_wdt_feed() == BK_OK ? OK : -EIO;
}

void bk7258_wdt_pm_prepare(void)
{
  struct bk7258_wdt_lowerhalf_s *priv = &g_bk7258_wdt;

  g_bk7258_wdt_pm_resume = priv->started;
  if (priv->started)
    {
      /* Feed immediately before the immutable low-voltage leaf closes the
       * APB watchdog behind the NuttX lower half.
       */

      if (bk_wdt_feed() != BK_OK)
        {
          wderr("ERROR: watchdog feed before PM transition failed\n");
        }
    }
}

void bk7258_wdt_pm_restore(void)
{
  struct bk7258_wdt_lowerhalf_s *priv = &g_bk7258_wdt;

  if (g_bk7258_wdt_pm_resume)
    {
      if (bk7258_wdt_sdk_start(priv->timeout) != BK_OK)
        {
          /* The low-voltage leaf closed the hardware watchdog.  Do not
           * continue advertising ACTIVE after a failed restore.
           */

          priv->started = false;
          wderr("ERROR: watchdog restore after PM transition failed\n");
        }
    }

  g_bk7258_wdt_pm_resume = false;
}

#ifdef CONFIG_BK7258_WDT_FAULT_INJECTION
int bk7258_wdt_fault_validate(void)
{
  volatile struct bk7258_wdt_fault_diag_s *diag =
    &g_bk7258_wdt_fault_diag;
  struct watchdog_status_s status;
  struct bk7258_wdt_lowerhalf_s *priv = &g_bk7258_wdt;
  int ret;

  memset((void *)diag, 0, sizeof(*diag));
  diag->magic = BK7258_WDT_FAULT_MAGIC;
  diag->version = BK7258_WDT_FAULT_VERSION;
  diag->size = sizeof(*diag);
  diag->state = 1;

  g_bk7258_wdt_fault_next = BK7258_WDT_FAULT_TIMER_STOP;
  ret = bk7258_wdt_initialize();
  diag->init_timer_stop = ret;
  if (ret != -EIO)
    {
      goto failed;
    }

  diag->state = 2;
  g_bk7258_wdt_fault_next = BK7258_WDT_FAULT_AON_STOP;
  ret = bk7258_wdt_initialize();
  diag->init_aon_stop = ret;
  if (ret != -EIO)
    {
      goto failed;
    }

  diag->state = 3;
  ret = bk7258_wdt_initialize();
  diag->init_retry = ret;
  if (ret < 0)
    {
      goto failed;
    }

  diag->state = 4;
  g_bk7258_wdt_fault_next = BK7258_WDT_FAULT_WDT_STOP;
  ret = bk7258_wdt_stop(&priv->wdt_lh);
  diag->failed_stop = ret;
  (void)bk7258_wdt_getstatus(&priv->wdt_lh, &status);
  diag->active_after_failed_stop =
    (status.flags & WDFLAGS_ACTIVE) != 0;
  if (ret != -EIO || diag->active_after_failed_stop == 0)
    {
      goto failed;
    }

  diag->state = 5;
  ret = bk7258_wdt_stop(&priv->wdt_lh);
  diag->retry_stop = ret;
  (void)bk7258_wdt_getstatus(&priv->wdt_lh, &status);
  diag->active_after_retry_stop =
    (status.flags & WDFLAGS_ACTIVE) != 0;
  if (ret < 0 || diag->active_after_retry_stop != 0)
    {
      goto failed;
    }

  diag->state = 6;
  ret = bk7258_wdt_start(&priv->wdt_lh);
  diag->restart = ret;
  if (ret < 0)
    {
      goto failed;
    }

  bk7258_wdt_pm_prepare();
  ret = bk_wdt_stop();
  if (ret != BK_OK)
    {
      ret = -EIO;
      goto failed;
    }

  g_bk7258_wdt_fault_next = BK7258_WDT_FAULT_WDT_START;
  bk7258_wdt_pm_restore();
  (void)bk7258_wdt_getstatus(&priv->wdt_lh, &status);
  diag->active_after_failed_restore =
    (status.flags & WDFLAGS_ACTIVE) != 0;
  if (diag->active_after_failed_restore != 0)
    {
      ret = -EIO;
      goto failed;
    }

  diag->state = 7;
  ret = bk7258_wdt_start(&priv->wdt_lh);
  diag->recovery_start = ret;
  (void)bk7258_wdt_getstatus(&priv->wdt_lh, &status);
  diag->final_active = (status.flags & WDFLAGS_ACTIVE) != 0;
  if (ret < 0 || diag->final_active == 0)
    {
      goto failed;
    }

  diag->result = OK;
  diag->state = 8;
  return OK;

failed:
  diag->result = ret < 0 ? ret : -EIO;
  diag->state |= 0x80000000u;
  return diag->result;
}
#endif

#endif /* CONFIG_BK7258_WDT */
