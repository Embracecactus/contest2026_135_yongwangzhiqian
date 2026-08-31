/****************************************************************************
 * chips/bk7258/ap/bk7258_timer.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 general-purpose timer — NuttX timer_lowerhalf_s wrapper.
 *
 * Wraps the Beken SDK bk_timer_* driver as a NuttX timer lower half and
 * publishes it at /dev/timerN.  The BK7258 has 6 hardware timer channels
 * (TIMER_ID0..TIMER_ID5); TIMER_ID0 is reserved by the SDK for its
 * microsecond timer (CONFIG_TIMER_US), so this wrapper drives a channel in
 * 3..5 (default TIMER_ID3).  The v3.1.1.9 AP bundle advertises only those
 * channels through CONFIG_TIMER_SUPPORT_ID_BITS=0x38.
 *
 * SDK call mapping:
 *   start()       -> bk_timer_start(chan, timeout_ms, sdk_isr)
 *   stop()        -> bk_timer_stop(chan)
 *   settimeout()  -> cache us; arm on next start()
 *   setcallback() -> cache the NuttX tccb_t callback + arg
 *   getstatus()   -> bk_timer_get_enable_status() | bk_timer_get_cnt()
 *   maxtimeout()  -> UINT32_MAX ms (32-bit ms period)
 *
 * Periodicity: the SDK reprograms the hardware period on every expiry, so
 * the sdk_isr fires periodically.  We forward to the NuttX tccb_t callback;
 * if it returns false we stop the channel.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_TIMER

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/timers/timer.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_timer.h>

#include <driver/timer.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_TIMER_BUS
#  define CONFIG_BK7258_TIMER_BUS      0
#endif

#ifndef CONFIG_BK7258_TIMER_CHAN
#  define CONFIG_BK7258_TIMER_CHAN     3
#endif

#if defined(CONFIG_TIMER_SUPPORT_ID_BITS)
_Static_assert((CONFIG_TIMER_SUPPORT_ID_BITS &
                (1u << CONFIG_BK7258_TIMER_CHAN)) != 0,
               "selected timer channel is unsupported by the SDK bundle");
#endif

/* SDK period is in milliseconds; the NuttX interface works in microseconds. */

#define BK7258_TIMER_US_PER_MS         1000u

/* The SDK's 32-bit ms period. */

#define BK7258_TIMER_MAX_MS            UINT32_MAX

#ifdef CONFIG_BK7258_TIMER_FAULT_INJECTION
#  define BK7258_TIMER_FAULT_MAGIC     0x46544d42u /* "BMTF" */
#  define BK7258_TIMER_FAULT_VERSION   1u
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_timer_priv_s
{
  struct timer_lowerhalf_s dev;   /* NuttX timer lower-half anchor */
  uint8_t chan;                   /* BK7258 timer channel (TIMER_IDx) */
  uint32_t timeout_us;            /* NuttX timeout in microseconds */
  tccb_t callback;                /* NuttX timeout callback (tccb_t) */
  FAR void *callback_arg;         /* NuttX callback argument */
  bool driver_inited;             /* bk_timer_driver_init() done */
  bool running;                   /* hardware channel armed */
};

#ifdef CONFIG_BK7258_TIMER_FAULT_INJECTION
struct bk7258_timer_fault_diag_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t state;
  int32_t result;
  int32_t settimeout;
  int32_t start;
  uint32_t active_before_stop;
  uint32_t hardware_before_stop;
  int32_t failed_stop;
  uint32_t active_after_failed_stop;
  uint32_t hardware_after_failed_stop;
  int32_t retry_stop;
  uint32_t active_after_retry_stop;
  uint32_t hardware_after_retry_stop;
};
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_timer_start(FAR struct timer_lowerhalf_s *lower);
static int bk7258_timer_stop(FAR struct timer_lowerhalf_s *lower);
static int bk7258_timer_getstatus(FAR struct timer_lowerhalf_s *lower,
                                  FAR struct timer_status_s *status);
static int bk7258_timer_settimeout(FAR struct timer_lowerhalf_s *lower,
                                   uint32_t timeout);
static void bk7258_timer_setcallback(FAR struct timer_lowerhalf_s *lower,
                                     tccb_t callback, FAR void *arg);
static int bk7258_timer_ioctl(FAR struct timer_lowerhalf_s *lower, int cmd,
                              unsigned long arg);
static int bk7258_timer_maxtimeout(FAR struct timer_lowerhalf_s *lower,
                                   FAR uint32_t *maxtimeout);
static void bk7258_timer_sdk_isr(timer_id_t timer_id);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct timer_ops_s g_bk7258_timer_ops =
{
  .start           = bk7258_timer_start,
  .stop            = bk7258_timer_stop,
  .getstatus       = bk7258_timer_getstatus,
  .settimeout      = bk7258_timer_settimeout,
  .setcallback     = bk7258_timer_setcallback,
  .ioctl           = bk7258_timer_ioctl,
  .maxtimeout      = bk7258_timer_maxtimeout,
  .tick_getstatus  = NULL,
  .tick_settimeout = NULL,
  .tick_maxtimeout = NULL,
};

static struct bk7258_timer_priv_s g_bk7258_timer =
{
  .dev.ops      = &g_bk7258_timer_ops,
  .chan         = (uint8_t)CONFIG_BK7258_TIMER_CHAN,
  .timeout_us   = 0,
  .callback     = NULL,
  .callback_arg = NULL,
  .driver_inited = false,
  .running      = false,
};
static bool g_bk7258_timer_registered;

#ifdef CONFIG_BK7258_TIMER_FAULT_INJECTION
volatile struct bk7258_timer_fault_diag_s g_bk7258_timer_fault_diag;
static volatile bool g_bk7258_timer_fail_next_stop;

static bk_err_t bk7258_timer_sdk_stop(timer_id_t timer_id)
{
  irqstate_t flags;
  bool fail;

  flags = enter_critical_section();
  fail = g_bk7258_timer_fail_next_stop;
  g_bk7258_timer_fail_next_stop = false;
  leave_critical_section(flags);

  if (fail)
    {
      return (bk_err_t)-1;
    }

  return bk_timer_stop(timer_id);
}
#else
#  define bk7258_timer_sdk_stop bk_timer_stop
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_timer_start
 *
 * Arm the SDK hardware timer for the cached timeout.  bk_timer_start() is
 * called with the timeout in ms and the per-channel SDK ISR.
 ****************************************************************************/

static int bk7258_timer_start(FAR struct timer_lowerhalf_s *lower)
{
  FAR struct bk7258_timer_priv_s *priv =
    (FAR struct bk7258_timer_priv_s *)lower;
  uint32_t timeout_ms;
  irqstate_t flags;
  bk_err_t ret;

  if (priv->timeout_us == 0 || priv->running)
    {
      return priv->running ? -EBUSY : -EINVAL;
    }

  if (!priv->driver_inited)
    {
      ret = bk_timer_driver_init();
      if (ret != BK_OK)
        {
          return -EIO;
        }

      priv->driver_inited = true;
    }

  timeout_ms = priv->timeout_us / BK7258_TIMER_US_PER_MS;
  timeout_ms += (priv->timeout_us % BK7258_TIMER_US_PER_MS) != 0;

  /* Set the software state before enabling hardware so an immediate expiry
   * cannot be mistaken for a stale interrupt.  Roll it back on failure.
   */

  flags = enter_critical_section();
  priv->running = true;
  leave_critical_section(flags);

  ret = bk_timer_start((timer_id_t)priv->chan, timeout_ms,
                       bk7258_timer_sdk_isr);
  if (ret != BK_OK)
    {
      flags = enter_critical_section();
      priv->running = false;
      leave_critical_section(flags);
      return -EIO;
    }
  return OK;
}

/****************************************************************************
 * Name: bk7258_timer_stop
 ****************************************************************************/

static int bk7258_timer_stop(FAR struct timer_lowerhalf_s *lower)
{
  FAR struct bk7258_timer_priv_s *priv =
    (FAR struct bk7258_timer_priv_s *)lower;
  irqstate_t flags;
  bool was_running;
  bk_err_t ret;

  /* Clear running under a critical section BEFORE stopping the hardware:
   * the SDK ISR checks running and will not re-arm once it is false, so
   * this ordering prevents an ISR in flight from restarting a timer the
   * upper half just stopped.
   */

  flags = enter_critical_section();
  was_running = priv->running;
  priv->running = false;
  leave_critical_section(flags);

  ret = bk7258_timer_sdk_stop((timer_id_t)priv->chan);
  if (ret != BK_OK)
    {
      /* Stop did not commit in hardware.  Restore the previous software
       * state so getstatus() and the ISR still describe the live channel.
       */

      flags = enter_critical_section();
      priv->running = was_running;
      leave_critical_section(flags);
      return -EIO;
    }

  return OK;
}

/****************************************************************************
 * Name: bk7258_timer_getstatus
 ****************************************************************************/

static int bk7258_timer_getstatus(FAR struct timer_lowerhalf_s *lower,
                                  FAR struct timer_status_s *status)
{
  FAR struct bk7258_timer_priv_s *priv =
    (FAR struct bk7258_timer_priv_s *)lower;
  uint32_t count;
  uint32_t period;
  bool driver_inited;
  bool running;
  irqstate_t flags;

  if (status == NULL)
    {
      return -EINVAL;
    }

  memset(status, 0, sizeof(*status));

  flags = enter_critical_section();
  running = priv->running;
  driver_inited = priv->driver_inited;
  if (running)
    {
      status->flags |= TCFLAGS_ACTIVE;
    }

  if (priv->callback != NULL)
    {
      status->flags |= TCFLAGS_HANDLER;
    }

  status->timeout = priv->timeout_us;
  leave_critical_section(flags);

  /* The SDK exposes raw increasing hardware count and end-count values,
   * not milliseconds.  Scale the remaining count fraction by the requested
   * timeout instead of treating raw clock cycles as milliseconds.
   */

  if (driver_inited && running)
    {
      count = bk_timer_get_cnt((timer_id_t)priv->chan);
      period = bk_timer_get_period((timer_id_t)priv->chan);
      if (period != 0 && count < period)
        {
          status->timeleft = (uint32_t)
            (((uint64_t)(period - count) * status->timeout) / period);
        }
    }

  return OK;
}

/****************************************************************************
 * Name: bk7258_timer_settimeout
 *
 * Cache the timeout in microseconds; the channel is not re-armed here
 * (the NuttX upper half calls start() afterwards).
 ****************************************************************************/

static int bk7258_timer_settimeout(FAR struct timer_lowerhalf_s *lower,
                                   uint32_t timeout)
{
  FAR struct bk7258_timer_priv_s *priv =
    (FAR struct bk7258_timer_priv_s *)lower;
  irqstate_t flags;

  if (timeout == 0)
    {
      return -EINVAL;
    }

  flags = enter_critical_section();
  if (priv->running)
    {
      leave_critical_section(flags);
      return -EPERM;
    }

  priv->timeout_us = timeout;
  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Name: bk7258_timer_setcallback
 ****************************************************************************/

static void bk7258_timer_setcallback(FAR struct timer_lowerhalf_s *lower,
                                     tccb_t callback, FAR void *arg)
{
  FAR struct bk7258_timer_priv_s *priv =
    (FAR struct bk7258_timer_priv_s *)lower;
  irqstate_t flags;

  /* ISR reads callback/callback_arg; write under a critical section. */

  flags = enter_critical_section();
  priv->callback = callback;
  priv->callback_arg = arg;
  leave_critical_section(flags);
}

/****************************************************************************
 * Name: bk7258_timer_sdk_isr
 *
 * SDK per-channel ISR.  The hardware reloads automatically, so this fires
 * periodically; forward to the NuttX tccb_t callback.  If the callback
 * returns false, stop the channel.
 ****************************************************************************/

static void bk7258_timer_sdk_isr(timer_id_t timer_id)
{
  FAR struct bk7258_timer_priv_s *priv = &g_bk7258_timer;
  tccb_t callback;
  FAR void *callback_arg;
  uint32_t timeout_us;
  bool running;
  bool keep_running = true;
  bool restart_failed = false;
  uint32_t prev_us;
  irqstate_t flags;

  /* Snapshot the shared state under a critical section.  running gates
   * re-arming: if the upper half stopped the timer (stop() clears running
   * before bk_timer_stop), a racing ISR must not restart it.
   */

  flags = enter_critical_section();
  running       = priv->running;
  callback      = priv->callback;
  callback_arg  = priv->callback_arg;
  timeout_us    = priv->timeout_us;
  leave_critical_section(flags);

  if (!running)
    {
      return;
    }

  if (callback != NULL)
    {
      prev_us = timeout_us;
      keep_running = callback(&timeout_us, callback_arg);

      /* Persist any timeout the callback chose so the next ISR snapshot
       * sees it.
       */

      if (timeout_us != prev_us)
        {
          flags = enter_critical_section();
          priv->timeout_us = timeout_us;
          leave_critical_section(flags);
        }

      /* If the callback changed the timeout, re-arm the SDK hardware with
       * the new value; otherwise the SDK auto-reload keeps the period.
       * Re-check running in case stop() ran during the callback.
       */

      if (keep_running && timeout_us != prev_us)
        {
          uint32_t timeout_ms;

          timeout_ms = timeout_us / BK7258_TIMER_US_PER_MS;
          timeout_ms += (timeout_us % BK7258_TIMER_US_PER_MS) != 0;

          flags = enter_critical_section();
          running = priv->running;
          leave_critical_section(flags);

          if (running)
            {
              if (bk_timer_start(timer_id, timeout_ms,
                                 bk7258_timer_sdk_isr) != BK_OK)
                {
                  restart_failed = true;
                  keep_running = false;
                }
            }
          else
            {
              /* A concurrent upper-half stop already committed both the
               * software and hardware transition.  Do not issue a second
               * stop from the ISR: some SDK implementations report that as
               * an error, which must not resurrect running=true.
               */

              return;
            }
        }
    }

  if (!keep_running)
    {
      bk_err_t stop_ret = bk7258_timer_sdk_stop(timer_id);

      flags = enter_critical_section();
      if (stop_ret == BK_OK)
        {
          priv->running = false;
        }
      else
        {
          /* Hardware is still armed.  Preserve ACTIVE; if reprogramming
           * failed, it is still running the previous period.
           */

          priv->running = true;
          if (restart_failed)
            {
              priv->timeout_us = prev_us;
            }
        }

      leave_critical_section(flags);
    }
}

/****************************************************************************
 * Name: bk7258_timer_ioctl
 *
 * No timer-specific ioctls are wired; the upper half handles TCIOC_*.
 ****************************************************************************/

static int bk7258_timer_ioctl(FAR struct timer_lowerhalf_s *lower, int cmd,
                              unsigned long arg)
{
  (void)lower;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

/****************************************************************************
 * Name: bk7258_timer_maxtimeout
 *
 * The SDK period field is a 32-bit ms value.
 ****************************************************************************/

static int bk7258_timer_maxtimeout(FAR struct timer_lowerhalf_s *lower,
                                   FAR uint32_t *maxtimeout)
{
  if (maxtimeout == NULL)
    {
      return -EINVAL;
    }

  /* The SDK period field is a 32-bit ms value, i.e. up to ~49.7 days.
   * Converted to microseconds that is > 32 bits, so report the largest
   * value the uint32_t *maxtimeout can carry.
   */

  *maxtimeout = UINT32_MAX;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_timer_initialize(void)
{
  FAR void *handle;

  if (g_bk7258_timer_registered)
    {
      return OK;
    }

  handle = timer_register(CONFIG_BK7258_TIMER_DEVNAME, &g_bk7258_timer.dev);
  if (handle == NULL)
    {
      return -ENODEV;
    }

  g_bk7258_timer_registered = true;
  return OK;
}

#ifdef CONFIG_BK7258_TIMER_FAULT_INJECTION
int bk7258_timer_fault_validate(void)
{
  volatile struct bk7258_timer_fault_diag_s *diag =
    &g_bk7258_timer_fault_diag;
  struct timer_status_s status;
  struct bk7258_timer_priv_s *priv = &g_bk7258_timer;
  uint32_t enable_mask;
  int ret;

  memset((void *)diag, 0, sizeof(*diag));
  diag->magic = BK7258_TIMER_FAULT_MAGIC;
  diag->version = BK7258_TIMER_FAULT_VERSION;
  diag->size = sizeof(*diag);
  diag->state = 1;
  enable_mask = 1u << priv->chan;

  ret = bk7258_timer_settimeout(&priv->dev, 60000000u);
  diag->settimeout = ret;
  if (ret < 0)
    {
      goto failed;
    }

  diag->state = 2;
  ret = bk7258_timer_start(&priv->dev);
  diag->start = ret;
  if (ret < 0)
    {
      goto failed;
    }

  (void)bk7258_timer_getstatus(&priv->dev, &status);
  diag->active_before_stop = (status.flags & TCFLAGS_ACTIVE) != 0;
  diag->hardware_before_stop =
    (bk_timer_get_enable_status() & enable_mask) != 0;
  if (diag->active_before_stop == 0 || diag->hardware_before_stop == 0)
    {
      ret = -EIO;
      goto failed;
    }

  diag->state = 3;
  g_bk7258_timer_fail_next_stop = true;
  ret = bk7258_timer_stop(&priv->dev);
  diag->failed_stop = ret;
  (void)bk7258_timer_getstatus(&priv->dev, &status);
  diag->active_after_failed_stop =
    (status.flags & TCFLAGS_ACTIVE) != 0;
  diag->hardware_after_failed_stop =
    (bk_timer_get_enable_status() & enable_mask) != 0;
  if (ret != -EIO || diag->active_after_failed_stop == 0 ||
      diag->hardware_after_failed_stop == 0)
    {
      goto failed;
    }

  diag->state = 4;
  ret = bk7258_timer_stop(&priv->dev);
  diag->retry_stop = ret;
  (void)bk7258_timer_getstatus(&priv->dev, &status);
  diag->active_after_retry_stop =
    (status.flags & TCFLAGS_ACTIVE) != 0;
  diag->hardware_after_retry_stop =
    (bk_timer_get_enable_status() & enable_mask) != 0;
  if (ret < 0 || diag->active_after_retry_stop != 0 ||
      diag->hardware_after_retry_stop != 0)
    {
      goto failed;
    }

  diag->result = OK;
  diag->state = 5;
  return OK;

failed:
  diag->result = ret < 0 ? ret : -EIO;
  diag->state |= 0x80000000u;
  return diag->result;
}
#endif

#endif /* CONFIG_BK7258_TIMER */
