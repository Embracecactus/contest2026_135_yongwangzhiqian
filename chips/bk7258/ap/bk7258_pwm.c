/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/ap/bk7258_pwm.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 PWM — NuttX pwm_lowerhalf_s lower-half wrapper.
 *
 * Wraps the Beken armino SDK bk_pwm_* driver (AP core only).  The
 * peripheral-complete v3.1.1.9 AP bundle exports the required controller
 * implementation while NuttX owns registration and run-time policy.
 *
 * SDK call mapping:
 *   setup()       -> bk_pwm_driver_init()
 *   shutdown()    -> bk_pwm_deinit(chan)
 *   first start   -> bk_pwm_init(chan, &cfg); bk_pwm_start(chan)
 *   update(info)  -> bk_pwm_set_period_duty(chan, &pd)
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

#include <arch/chip/bk7258_pwm.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_PWM_BUS
#  define CONFIG_BK7258_PWM_BUS        0
#endif

#ifndef CONFIG_BK7258_PWM_CHAN
#  define CONFIG_BK7258_PWM_CHAN       BK7258_PWM_CHAN_DEFAULT
#endif

/* NuttX pwm_info_s duty is unsigned 16.16 fixed point.  The largest value
 * accepted by the upper half is 65535/65536, not exactly 100 percent.
 */

#define BK7258_PWM_DUTY_MAX            65535u
#define BK7258_PWM_DUTY_SCALE          65536u

#define BK7258_PWM_STRINGIFY_(value)   #value
#define BK7258_PWM_STRINGIFY(value)    BK7258_PWM_STRINGIFY_(value)
#define BK7258_PWM_DEVPATH             \
  "/dev/pwm" BK7258_PWM_STRINGIFY(CONFIG_BK7258_PWM_BUS)

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

static int bk7258_pwm_map_error(bk_err_t error)
{
  if (error == BK_OK)
    {
      return OK;
    }

  switch (error)
    {
      case BK_ERR_PWM_CHAN_ID:
      case BK_ERR_PWM_PERIOD_DUTY:
      case BK_ERR_PWM_INVALID_GPIO_MODE:
      case BK_ERR_PARAM:
      case BK_ERR_NULL_PARAM:
        return -EINVAL;

      case BK_ERR_PWM_NOT_INIT:
      case BK_ERR_PWM_CHAN_NOT_INIT:
      case BK_ERR_PWM_CHAN_NOT_START:
      case BK_ERR_NOT_INIT:
      case BK_ERR_TRY_AGAIN:
        return -EAGAIN;

      case BK_ERR_BUSY:
        return -EBUSY;

      case BK_ERR_IN_PROGRESS:
        return -EINPROGRESS;

      case BK_ERR_TIMEOUT:
        return -ETIMEDOUT;

      case BK_ERR_NO_MEM:
        return -ENOMEM;

      case BK_ERR_NOT_SUPPORT:
        return -ENOTSUP;

      default:
        return -EIO;
    }
}

/****************************************************************************
 * Name: bk7258_pwm_setup
 *
 * Initialize the global SDK PWM subsystem.  Channel initialization is
 * deferred until start(), when the caller's real period and duty are known.
 * This matches the official SDK flow and avoids entering the v1px driver's
 * special zero-duty GPIO mode before the first PWM waveform is configured.
 ****************************************************************************/

static int bk7258_pwm_setup(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct bk7258_pwm_priv_s *priv =
    (FAR struct bk7258_pwm_priv_s *)dev;
  bk_err_t ret;
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
          return bk7258_pwm_map_error(ret);
        }

      priv->driver_inited = true;
    }

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
  int result = OK;
  int mapped;
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
      mapped = bk7258_pwm_map_error(
        bk_pwm_stop((pwm_chan_t)priv->chan));
      if (mapped == OK)
        {
          priv->running = false;
        }
      else
        {
          result = mapped;
        }
    }

  if (priv->chan_inited)
    {
      mapped = bk7258_pwm_map_error(
        bk_pwm_deinit((pwm_chan_t)priv->chan));
      if (mapped == OK)
        {
          priv->chan_inited = false;
          priv->running = false;
        }
      else if (result == OK)
        {
          result = mapped;
        }
    }

  /* The SDK driver init state and interrupt registrations are global to all
   * twelve channels, and the public API provides no ownership query or
   * reference count.  Deinitializing it from one NuttX device could stop an
   * unrelated SDK consumer, so shutdown releases only this channel.
   */

  if (priv->driver_inited)
    {
      priv->driver_inited = false;
    }

  nxmutex_unlock(&priv->lock);
  return result;
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
  pwm_init_config_t init_cfg;
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

  if (info->frequency > BK7258_PWM_CLK_HZ)
    {
      nxmutex_unlock(&priv->lock);
      return -ERANGE;
    }

  if (info->cpol != PWM_CPOL_NDEF || info->dcpol != PWM_DCPOL_NDEF)
    {
      nxmutex_unlock(&priv->lock);
      return -ENOTSUP;
    }

  /* period_cycle = CLK_HZ / frequency. */

  period = BK7258_PWM_CLK_HZ / info->frequency;

  /* Match NuttX's unsigned 16.16 convention and the rounding used by its
   * in-tree PWM lower halves.  Rounding may select period ticks for a very
   * high duty on a low-resolution period; the SDK deliberately represents
   * that result as a constant-high GPIO output.
   */

  on_ticks = (uint64_t)period * info->duty;
  on_ticks = (on_ticks + (BK7258_PWM_DUTY_SCALE / 2u)) /
             BK7258_PWM_DUTY_SCALE;

  if (!priv->chan_inited)
    {
      memset(&init_cfg, 0, sizeof(init_cfg));
      init_cfg.period_cycle = period;
      init_cfg.duty_cycle   = (uint32_t)on_ticks;
      init_cfg.psc          = 0;

      ret = bk_pwm_init((pwm_chan_t)priv->chan, &init_cfg);
      if (ret != BK_OK)
        {
          nxmutex_unlock(&priv->lock);
          return bk7258_pwm_map_error(ret);
        }

      priv->chan_inited = true;
    }
  else
    {
      memset(&pd, 0, sizeof(pd));
      pd.period_cycle = period;
      pd.duty_cycle   = (uint32_t)on_ticks;
      pd.psc          = 0;

      ret = bk_pwm_set_period_duty((pwm_chan_t)priv->chan, &pd);
      if (ret != BK_OK)
        {
          nxmutex_unlock(&priv->lock);
          return bk7258_pwm_map_error(ret);
        }
    }

  /* bk_pwm_set_period_duty() uses the hardware's load-new-config path, so
   * an already running channel is updated without an avoidable stop/start
   * glitch, as required by the NuttX upper-half contract.
   */

  if (!priv->running)
    {
      ret = bk_pwm_start((pwm_chan_t)priv->chan);
      if (ret != BK_OK)
        {
          nxmutex_unlock(&priv->lock);
          return bk7258_pwm_map_error(ret);
        }

      priv->running = true;
    }

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
      rc = bk7258_pwm_map_error(
        bk_pwm_stop((pwm_chan_t)priv->chan));
      if (rc < 0)
        {
          nxmutex_unlock(&priv->lock);
          return rc;
        }

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
  return pwm_register(BK7258_PWM_DEVPATH, &g_bk7258_pwm.dev);
}

#endif /* CONFIG_BK7258_PWM */
