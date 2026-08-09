/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/ap/bk7258_pwm.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) PWM — NuttX pwm_lowerhalf_s lower-half wrapper.
 *
 * Wraps the Beken armino SDK bk_pwm_* driver (AP core only).  See
 * PWM_BLOCKED_ROOT_CAUSE.md in this directory for the bundle-export defect
 * (CONFIG_PWM missing from the exported sdkconfig.h) that keeps bk_pwm_*
 * out of libdriver.a today; re-export the bundle with CONFIG_PWM=1 and this
 * wrapper links and runs as-is.
 *
 * SDK call mapping:
 *   setup()       -> bk_pwm_driver_init(); bk_pwm_init(chan, &init_cfg)
 *   shutdown()    -> bk_pwm_deinit(chan); bk_pwm_driver_deinit()
 *   start(info)   -> bk_pwm_set_period_duty(chan, &pd); bk_pwm_start(chan)
 *   stop()        -> bk_pwm_stop(chan)
 *
 * SDK clock / duty semantics (verified in armino source):
 *   - Input clock is XTAL 26 MHz (SDK selects PWM_SCLK_XTAL in
 *     pwm_driver.c), so period_cycle unit == 1/26MHz.
 *   - Both bk_pwm_init() and bk_pwm_set_period_duty() INVERT duty
 *     internally: duty_cycle = period_cycle - on_ticks (pwm_driver.c).
 *     We therefore pass on-ticks (high-time) as duty_cycle, and the SDK
 *     turns it into the low-time the hardware CCR expects.
 *   - bk_pwm_init()/bk_pwm_set_period_duty() reject period_cycle == 0 and
 *     duty > period, so we clamp period to >= 1.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_PWM

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <nuttx/mutex.h>
#include <nuttx/timers/pwm.h>

#include <driver/pwm.h>

#include "bk7258_pwm.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_PWM_BUS
#  define CONFIG_BK7258_PWM_BUS        0
#endif

#ifndef CONFIG_BK7258_PWM_CHAN
#  define CONFIG_BK7258_PWM_CHAN       BK7258_PWM_CHAN_DEFAULT
#endif

/* NuttX pwm_info_s duty is ub16 (0..65535 => 0..100%). */

#define BK7258_PWM_DUTY_MAX            65535u

/* Minimum legal period_cycle for the SDK (rejects period == 0). */

#define BK7258_PWM_PERIOD_MIN          1u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_pwm_priv_s
{
  struct pwm_lowerhalf_s dev;   /* NuttX lower-half vtable anchor */
  uint8_t chan;                 /* BK7258 PWM channel (PWM_ID_x) */
  bool driver_inited;           /* bk_pwm_driver_init() done */
  bool chan_inited;             /* bk_pwm_init() done for this chan */
  bool running;                 /* bk_pwm_start() issued, not yet stopped */
  mutex_t lock;                 /* serialises setup/start/stop/shutdown */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_pwm_setup(FAR struct pwm_lowerhalf_s *dev);
static int bk7258_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev);
static int bk7258_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                            FAR const struct pwm_info_s *info);
static int bk7258_pwm_stop(FAR struct pwm_lowerhalf_s *dev);
static int bk7258_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev,
                            int cmd, unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct pwm_ops_s g_bk7258_pwm_ops =
{
  .setup     = bk7258_pwm_setup,
  .shutdown  = bk7258_pwm_shutdown,
  .start     = bk7258_pwm_start,
  .stop      = bk7258_pwm_stop,
  .ioctl     = bk7258_pwm_ioctl,
};

static struct bk7258_pwm_priv_s g_bk7258_pwm =
{
  .dev.ops      = &g_bk7258_pwm_ops,
  .chan         = (uint8_t)CONFIG_BK7258_PWM_CHAN,
  .driver_inited = false,
  .chan_inited  = false,
  .running      = false,
  .lock         = NXMUTEX_INITIALIZER,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_pwm_setup
 *
 * Initialize the SDK PWM subsystem and configure this channel with a
 * minimal (period = 1) configuration.  The real period/duty are applied
 * in start() via bk_pwm_set_period_duty().
 ****************************************************************************/

static int bk7258_pwm_setup(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct bk7258_pwm_priv_s *priv =
    (FAR struct bk7258_pwm_priv_s *)dev;
  bk_err_t ret;
  pwm_init_config_t cfg;
  int rc;

  rc = nxmutex_lock(&priv->lock);
  if (rc < 0)
    {
      return rc;
    }

  if (!priv->driver_inited)
    {
      ret = bk_pwm_driver_init();
      if (ret != BK_OK)
        {
          nxmutex_unlock(&priv->lock);
          return -EIO;
        }

      priv->driver_inited = true;
    }

  if (priv->chan_inited)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  memset(&cfg, 0, sizeof(cfg));
  cfg.period_cycle = BK7258_PWM_PERIOD_MIN;
  cfg.duty_cycle   = 0;
  cfg.duty2_cycle  = 0;
  cfg.duty3_cycle  = 0;
  cfg.psc          = 0;

  ret = bk_pwm_init((pwm_chan_t)priv->chan, &cfg);
  if (ret != BK_OK)
    {
      /* Roll back driver_init on channel-init failure. */
      bk_pwm_driver_deinit();
      priv->driver_inited = false;
      nxmutex_unlock(&priv->lock);
      return -EIO;
    }

  priv->chan_inited = true;
  nxmutex_unlock(&priv->lock);
  return OK;
}

/****************************************************************************
 * Name: bk7258_pwm_shutdown
 ****************************************************************************/

static int bk7258_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct bk7258_pwm_priv_s *priv =
    (FAR struct bk7258_pwm_priv_s *)dev;
  int rc;

  rc = nxmutex_lock(&priv->lock);
  if (rc < 0)
    {
      return rc;
    }

  /* Stop the channel first so we never leave a running PWM behind if
   * bk_pwm_deinit() were to fault.
   */

  if (priv->running)
    {
      (void)bk_pwm_stop((pwm_chan_t)priv->chan);
      priv->running = false;
    }

  if (priv->chan_inited)
    {
      bk_pwm_deinit((pwm_chan_t)priv->chan);
      priv->chan_inited = false;
    }

  if (priv->driver_inited)
    {
      bk_pwm_driver_deinit();
      priv->driver_inited = false;
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}

/****************************************************************************
 * Name: bk7258_pwm_start
 *
 * Convert NuttX pwm_info_s (frequency in Hz, duty as ub16 0..65535) into
 * SDK period_cycle / on-ticks and apply.  The SDK inverts duty internally,
 * so we pass on-ticks as duty_cycle.
 ****************************************************************************/

static int bk7258_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                            FAR const struct pwm_info_s *info)
{
  FAR struct bk7258_pwm_priv_s *priv =
    (FAR struct bk7258_pwm_priv_s *)dev;
  bk_err_t ret;
  uint32_t period;
  uint64_t on_ticks;
  pwm_period_duty_config_t pd;
  int rc;

  if (info == NULL || info->frequency == 0 ||
      info->duty > BK7258_PWM_DUTY_MAX)
    {
      return -EINVAL;
    }

  rc = nxmutex_lock(&priv->lock);
  if (rc < 0)
    {
      return rc;
    }

  if (!priv->chan_inited)
    {
      nxmutex_unlock(&priv->lock);
      return -EACCES;
    }

  /* period_cycle = CLK_HZ / frequency, clamped to >= 1. */
  period = BK7258_PWM_CLK_HZ / info->frequency;
  if (period < BK7258_PWM_PERIOD_MIN)
    {
      period = BK7258_PWM_PERIOD_MIN;
    }

  /* on_ticks = period * duty / 65535 (duty is ub16). */
  on_ticks = (uint64_t)period * info->duty;
  on_ticks /= BK7258_PWM_DUTY_MAX;

  /* If a previous start is still running, stop first so the new
   * period/duty take effect atomically; bk_pwm_set_period_duty on a
   * running channel can otherwise race the counter.
   */

  if (priv->running)
    {
      (void)bk_pwm_stop((pwm_chan_t)priv->chan);
    }

  memset(&pd, 0, sizeof(pd));
  pd.period_cycle = period;
  pd.duty_cycle   = (uint32_t)on_ticks;   /* SDK inverts this internally */
  pd.duty2_cycle  = 0;                     /* single-channel mode */
  pd.duty3_cycle  = 0;
  pd.psc          = 0;

  ret = bk_pwm_set_period_duty((pwm_chan_t)priv->chan, &pd);
  if (ret != BK_OK)
    {
      nxmutex_unlock(&priv->lock);
      return -EIO;
    }

  ret = bk_pwm_start((pwm_chan_t)priv->chan);
  if (ret != BK_OK)
    {
      nxmutex_unlock(&priv->lock);
      return -EIO;
    }

  priv->running = true;
  nxmutex_unlock(&priv->lock);
  return OK;
}

/****************************************************************************
 * Name: bk7258_pwm_stop
 ****************************************************************************/

static int bk7258_pwm_stop(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct bk7258_pwm_priv_s *priv =
    (FAR struct bk7258_pwm_priv_s *)dev;
  int rc;

  rc = nxmutex_lock(&priv->lock);
  if (rc < 0)
    {
      return rc;
    }

  if (priv->running)
    {
      bk_pwm_stop((pwm_chan_t)priv->chan);
      priv->running = false;
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}

/****************************************************************************
 * Name: bk7258_pwm_ioctl
 *
 * No optional PWM ioctls are wired.  Capture / phase-shift / init-signal
 * control would map onto bk_pwm_capture_* and bk_pwm_set_init_signal_*.
 ****************************************************************************/

static int bk7258_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev,
                            int cmd, unsigned long arg)
{
  (void)dev;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_pwm_initialize(void)
{
  return pwm_register(CONFIG_BK7258_PWM_DEVNAME, &g_bk7258_pwm.dev);
}

#endif /* CONFIG_BK7258_PWM */
