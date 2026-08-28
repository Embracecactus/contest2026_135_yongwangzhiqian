/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/ap/bk7258_saradc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SARADC - NuttX adc_dev_s lower-half wrapper.
 *
 * Wraps the official Beken bk_adc_* SDK API in a NuttX ADC lower half.
 * The BK7258 SARADC is a successive-approximation converter with channels
 * ADC_0..ADC_15.  This AP lower half talks to the CP-owned ADC controller
 * through the official v3.1.1.9 SARADC mailbox service.
 *
 * SDK call mapping (synchronous triggered conversion on the AP side):
 *   ao_bind()    -> stash the upper-half adc_callback_s
 *   ao_setup()   -> bk_adc_chan_init_gpio(chan)
 *   ao_shutdown()-> bk_adc_chan_deinit_gpio(chan)
 *   ao_rxint()   -> no-op (the CP SDK owns the conversion IRQ/semaphore)
 *   ao_ioctl()   -> ANIOC_TRIGGER:
 *                   acquire/init/config/start/read/stop/deinit/release, then
 *                   cb->au_receive(dev, chan, raw)
 *
 * SDK semantics (verified in saradc_client.c):
 *   - Every AP bk_adc_* API performs a mailbox IPC round trip against the CP
 *     SARADC server.  The matching CP profile owns server bring-up.
 *   - External-channel GPIO mapping is separate from bk_adc_init().
 *   - The AP bk_adc_single_read() request is absent from the CP server's
 *     command dispatcher.  The supported bk_adc_read() path waits for the
 *     SDK's 32-sample buffer and returns the average of its latter half.
 *   - CP ownership is held only during a conversion, allowing temperature
 *     sampling and other clients to make progress between triggers.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SARADC

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/cache.h>
#include <nuttx/mutex.h>

#include <arch/chip/bk7258_saradc.h>

#include <driver/adc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_SARADC_BUS
#  define CONFIG_BK7258_SARADC_BUS      0
#endif

#ifndef CONFIG_BK7258_SARADC_CHAN
#  define CONFIG_BK7258_SARADC_CHAN     0
#endif

#ifndef CONFIG_BK7258_SARADC_READ_TIMEOUT_MS
#  define CONFIG_BK7258_SARADC_READ_TIMEOUT_MS 1000
#endif

#define BK7258_SARADC_REFERENCE_CLK       0x0030a0c5u
#define BK7258_SARADC_REFERENCE_STEADY    7u

/* adc_config_t crosses the AP/CP SDK archive boundary.  Both v3.1.1.9
 * archives use the ARM EABI small-enum layout.  Fail the build instead of
 * silently sending a differently laid-out command if toolchain flags or the
 * imported SDK contract ever change.
 */

_Static_assert(sizeof(adc_config_t) == 36,
               "unexpected v3.1.1.9 adc_config_t size");
_Static_assert(offsetof(adc_config_t, adc_mode) == 16,
               "unexpected adc_config_t.adc_mode offset");
_Static_assert(offsetof(adc_config_t, src_clk) == 17,
               "unexpected adc_config_t.src_clk offset");
_Static_assert(offsetof(adc_config_t, chan) == 18,
               "unexpected adc_config_t.chan offset");
_Static_assert(offsetof(adc_config_t, saturate_mode) == 19,
               "unexpected adc_config_t.saturate_mode offset");
_Static_assert(offsetof(adc_config_t, vol_div) == 34,
               "unexpected adc_config_t.vol_div offset");

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_saradc_priv_s
{
  struct adc_dev_s dev;                 /* NuttX ADC lower-half anchor */
  FAR const struct adc_callback_s *cb;  /* Upper-half callbacks (ao_bind) */
  mutex_t lock;                         /* Serialize TRIGGER ioctls */
  uint8_t chan;                         /* SARADC channel (ADC_x) */
  bool bound;                           /* cb set? */
  bool gpio_mapped;                     /* Channel owns its analog pin */
  bool faulted;                         /* Cleanup failed; reboot required */
  bool release_uncertain;               /* Never retry an ambiguous unlock */
  int setup_error;                      /* Deferred ao_setup() failure */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_saradc_bind(FAR struct adc_dev_s *dev,
                              FAR const struct adc_callback_s *callback);
static void bk7258_saradc_reset(FAR struct adc_dev_s *dev);
static int bk7258_saradc_setup(FAR struct adc_dev_s *dev);
static void bk7258_saradc_shutdown(FAR struct adc_dev_s *dev);
static void bk7258_saradc_rxint(FAR struct adc_dev_s *dev, bool enable);
static int bk7258_saradc_ioctl(FAR struct adc_dev_s *dev, int cmd,
                               unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct adc_ops_s g_bk7258_saradc_ops =
{
  .ao_bind      = bk7258_saradc_bind,
  .ao_reset     = bk7258_saradc_reset,
  .ao_setup     = bk7258_saradc_setup,
  .ao_shutdown  = bk7258_saradc_shutdown,
  .ao_rxint     = bk7258_saradc_rxint,
  .ao_ioctl     = bk7258_saradc_ioctl,
};

static struct bk7258_saradc_priv_s g_bk7258_saradc =
{
  .dev.ad_ops = &g_bk7258_saradc_ops,
  .lock       = NXMUTEX_INITIALIZER,
  .chan       = (uint8_t)CONFIG_BK7258_SARADC_CHAN,
  .bound      = false,
  .gpio_mapped = false,
  .faulted    = false,
  .release_uncertain = false,
  .setup_error = OK,
};
static bool g_bk7258_saradc_registered;

volatile struct bk7258_saradc_diag_s g_bk7258_saradc_diag;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_saradc_bind
 *
 * Bind the upper-half callbacks.  Called before the device is opened.
 ****************************************************************************/

static int bk7258_saradc_bind(FAR struct adc_dev_s *dev,
                              FAR const struct adc_callback_s *callback)
{
  FAR struct bk7258_saradc_priv_s *priv =
    (FAR struct bk7258_saradc_priv_s *)dev;

  if (callback == NULL)
    {
      return -EINVAL;
    }

  priv->cb = callback;
  priv->bound = true;
  return OK;
}

/****************************************************************************
 * Name: bk7258_saradc_reset
 *
 * Nothing to reset: the SDK owns all SARADC hardware state and the IPC
 * service.  Kept as a no-op to satisfy the lower-half contract.
 ****************************************************************************/

static void bk7258_saradc_reset(FAR struct adc_dev_s *dev)
{
  (void)dev;
}

static void bk7258_saradc_diag_publish(void)
{
  __asm volatile ("dmb sy" ::: "memory");
  up_clean_dcache((uintptr_t)&g_bk7258_saradc_diag,
                  (uintptr_t)&g_bk7258_saradc_diag +
                  sizeof(g_bk7258_saradc_diag));
}

static int bk7258_saradc_map_error(bk_err_t error)
{
  /* The v3.1.1.9 AP mailbox client collapses every non-zero CP response and
   * every transport failure to BK_FAIL.  Do not claim a precision errno
   * which this side of the immutable wire contract cannot observe.
   */

  return error == BK_OK ? OK : -EIO;
}

static int bk7258_saradc_record_error(enum bk7258_saradc_stage_e stage,
                                      bk_err_t sdk_error)
{
  int error = bk7258_saradc_map_error(sdk_error);

  g_bk7258_saradc_diag.last_stage = stage;
  g_bk7258_saradc_diag.last_error = error;
  g_bk7258_saradc_diag.last_sdk_error = sdk_error;
  if (g_bk7258_saradc_diag.first_error == OK)
    {
      g_bk7258_saradc_diag.first_error = error;
      g_bk7258_saradc_diag.first_sdk_error = sdk_error;
    }
  return error;
}

static void bk7258_saradc_mark_fault(
  FAR struct bk7258_saradc_priv_s *priv)
{
  priv->faulted = true;
  g_bk7258_saradc_diag.state = BK7258_SARADC_STATE_FAULT;
}

static int bk7258_saradc_map_gpio_locked(
  FAR struct bk7258_saradc_priv_s *priv)
{
  bk_err_t cleanup_error;
  bk_err_t sdk_error;
  int ret;

  if (priv->faulted)
    {
      priv->setup_error = -EIO;
      return priv->setup_error;
    }

  if (priv->gpio_mapped)
    {
      priv->setup_error = OK;
      return OK;
    }

  g_bk7258_saradc_diag.resources |= BK7258_SARADC_RESOURCE_GPIO;
  sdk_error = bk_adc_chan_init_gpio((adc_chan_t)priv->chan);
  if (sdk_error != BK_OK)
    {
      ret = bk7258_saradc_record_error(BK7258_SARADC_STAGE_GPIO_MAP,
                                       sdk_error);
      cleanup_error = bk_adc_chan_deinit_gpio((adc_chan_t)priv->chan);
      if (cleanup_error == BK_OK)
        {
          g_bk7258_saradc_diag.resources &=
            ~BK7258_SARADC_RESOURCE_GPIO;
          g_bk7258_saradc_diag.gpio_unmap_count++;
        }
      else
        {
          (void)bk7258_saradc_record_error(
                  BK7258_SARADC_STAGE_GPIO_UNMAP, cleanup_error);
          bk7258_saradc_mark_fault(priv);
        }

      priv->setup_error = ret;
      return ret;
    }

  priv->gpio_mapped = true;
  priv->setup_error = OK;
  g_bk7258_saradc_diag.gpio_map_count++;
  g_bk7258_saradc_diag.state = BK7258_SARADC_STATE_READY;
  g_bk7258_saradc_diag.last_stage = BK7258_SARADC_STAGE_GPIO_MAP;
  return OK;
}

static int bk7258_saradc_cleanup_conversion(
  FAR struct bk7258_saradc_priv_s *priv)
{
  bk_err_t sdk_error;
  int error = OK;
  int ret;

  if ((g_bk7258_saradc_diag.resources &
       BK7258_SARADC_RESOURCE_STARTED) != 0)
    {
      sdk_error = bk_adc_stop();
      if (sdk_error == BK_OK)
        {
          g_bk7258_saradc_diag.resources &=
            ~BK7258_SARADC_RESOURCE_STARTED;
          g_bk7258_saradc_diag.stop_count++;
        }
      else
        {
          ret = bk7258_saradc_record_error(BK7258_SARADC_STAGE_STOP,
                                           sdk_error);
          if (error == OK)
            {
              error = ret;
            }
        }
    }

  /* Keep the CP ownership lock until every controller resource below it has
   * been released.  Retrying cleanup on last-close is safe only while this
   * client still owns the shared ADC mutex.
   */

  if ((g_bk7258_saradc_diag.resources &
       BK7258_SARADC_RESOURCE_STARTED) == 0 &&
      (g_bk7258_saradc_diag.resources &
       BK7258_SARADC_RESOURCE_INITED) != 0)
    {
      sdk_error = bk_adc_deinit((adc_chan_t)priv->chan);
      if (sdk_error == BK_OK)
        {
          g_bk7258_saradc_diag.resources &=
            ~BK7258_SARADC_RESOURCE_INITED;
          g_bk7258_saradc_diag.deinit_count++;
        }
      else
        {
          ret = bk7258_saradc_record_error(BK7258_SARADC_STAGE_DEINIT,
                                           sdk_error);
          if (error == OK)
            {
              error = ret;
            }
        }
    }

  if ((g_bk7258_saradc_diag.resources &
       (BK7258_SARADC_RESOURCE_STARTED |
        BK7258_SARADC_RESOURCE_INITED)) == 0 &&
      (g_bk7258_saradc_diag.resources &
       BK7258_SARADC_RESOURCE_ACQUIRED) != 0 &&
      !priv->release_uncertain)
    {
      sdk_error = bk_adc_release();
      if (sdk_error == BK_OK)
        {
          g_bk7258_saradc_diag.resources &=
            ~BK7258_SARADC_RESOURCE_ACQUIRED;
          g_bk7258_saradc_diag.release_count++;
        }
      else
        {
          /* The AP stub collapses both a CP-side failure and a lost reply
           * into BK_FAIL.  Reissuing release after a lost success could
           * unlock a later owner, so retain the ownership obligation and
           * require a CP/server reset rather than retrying it.
           */

          priv->release_uncertain = true;
          ret = bk7258_saradc_record_error(BK7258_SARADC_STAGE_RELEASE,
                                           sdk_error);
          if (error == OK)
            {
              error = ret;
            }
        }
    }

  if ((g_bk7258_saradc_diag.resources &
       (BK7258_SARADC_RESOURCE_ACQUIRED |
        BK7258_SARADC_RESOURCE_INITED |
        BK7258_SARADC_RESOURCE_STARTED)) != 0)
    {
      bk7258_saradc_mark_fault(priv);
    }
  else if (!priv->faulted)
    {
      g_bk7258_saradc_diag.state = priv->gpio_mapped ?
        BK7258_SARADC_STATE_READY : BK7258_SARADC_STATE_RESET;
    }

  return error;
}

/****************************************************************************
 * Name: bk7258_saradc_setup
 *
 * Claim the configured channel's analog pin for the duration of the open
 * session.  Controller ownership is deliberately deferred to each trigger.
 ****************************************************************************/

static int bk7258_saradc_setup(FAR struct adc_dev_s *dev)
{
  FAR struct bk7258_saradc_priv_s *priv =
    (FAR struct bk7258_saradc_priv_s *)dev;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      /* adc_open() currently commits its open count even when ao_setup()
       * fails.  Never return a negative value from this lower callback;
       * ANIOC_TRIGGER will still fail closed if the mutex remains unusable.
       */

      priv->setup_error = ret;
      return OK;
    }

  g_bk7258_saradc_diag.setup_count++;
  (void)bk7258_saradc_map_gpio_locked(priv);
  bk7258_saradc_diag_publish();
  nxmutex_unlock(&priv->lock);

  /* NuttX adc_open() currently commits ad_ocount even when ao_setup()
   * returns an error.  Keep the VFS/open lifecycle coherent and defer any
   * recorded map failure to the first ANIOC_TRIGGER, which retries the
   * mapping before it starts a conversion.
   */

  return OK;
}

/****************************************************************************
 * Name: bk7258_saradc_shutdown
 ****************************************************************************/

static void bk7258_saradc_shutdown(FAR struct adc_dev_s *dev)
{
  FAR struct bk7258_saradc_priv_s *priv =
    (FAR struct bk7258_saradc_priv_s *)dev;
  bk_err_t sdk_error;

  if (nxmutex_lock(&priv->lock) < 0)
    {
      bk7258_saradc_mark_fault(priv);
      bk7258_saradc_diag_publish();
      return;
    }

  g_bk7258_saradc_diag.shutdown_count++;
  (void)bk7258_saradc_cleanup_conversion(priv);

  if (priv->gpio_mapped &&
      (g_bk7258_saradc_diag.resources &
       (BK7258_SARADC_RESOURCE_ACQUIRED |
        BK7258_SARADC_RESOURCE_INITED |
        BK7258_SARADC_RESOURCE_STARTED)) == 0)
    {
      sdk_error = bk_adc_chan_deinit_gpio((adc_chan_t)priv->chan);
      if (sdk_error == BK_OK)
        {
          priv->gpio_mapped = false;
          g_bk7258_saradc_diag.resources &=
            ~BK7258_SARADC_RESOURCE_GPIO;
          g_bk7258_saradc_diag.gpio_unmap_count++;
          g_bk7258_saradc_diag.last_stage =
            BK7258_SARADC_STAGE_GPIO_UNMAP;
        }
      else
        {
          (void)bk7258_saradc_record_error(BK7258_SARADC_STAGE_GPIO_UNMAP,
                                           sdk_error);
          bk7258_saradc_mark_fault(priv);
        }
    }

  if (g_bk7258_saradc_diag.resources == 0 && !priv->faulted)
    {
      priv->setup_error = OK;
      g_bk7258_saradc_diag.state = BK7258_SARADC_STATE_RESET;
    }

  bk7258_saradc_diag_publish();
  nxmutex_unlock(&priv->lock);
}

/****************************************************************************
 * Name: bk7258_saradc_rxint
 *
 * No-op: the SDK SARADC path is mailbox IPC / blocking, there is no
 * interrupt to enable or disable from this lower half.
 ****************************************************************************/

static void bk7258_saradc_rxint(FAR struct adc_dev_s *dev, bool enable)
{
  (void)dev;
  (void)enable;
}

/****************************************************************************
 * Name: bk7258_saradc_ioctl
 *
 * ANIOC_TRIGGER performs a single-shot conversion and pushes the raw
 * sample to the upper half via au_receive().
 ****************************************************************************/

static int bk7258_saradc_ioctl(FAR struct adc_dev_s *dev, int cmd,
                               unsigned long arg)
{
  FAR struct bk7258_saradc_priv_s *priv =
    (FAR struct bk7258_saradc_priv_s *)dev;
  uint16_t raw;
  bool deliver = false;
  adc_config_t config;
  bk_err_t sdk_error;
  int cleanup_error;
  int ret;
  irqstate_t flags;

  switch (cmd)
    {
      case ANIOC_TRIGGER:
        {
          if (priv->cb == NULL || !priv->bound)
            {
              return -EAGAIN;
            }

          ret = nxmutex_lock(&priv->lock);
          if (ret < 0)
            {
              return ret;
            }

          if (!priv->gpio_mapped)
            {
              ret = bk7258_saradc_map_gpio_locked(priv);
              if (ret < 0)
                {
                  goto trigger_out;
                }
            }

          if (priv->faulted)
            {
              ret = -EIO;
              goto trigger_out;
            }

          if (!priv->gpio_mapped)
            {
              ret = -EPIPE;
              goto trigger_out;
            }

          g_bk7258_saradc_diag.trigger_count++;
          g_bk7258_saradc_diag.state = BK7258_SARADC_STATE_CONVERTING;

          sdk_error = bk_adc_acquire();
          if (sdk_error != BK_OK)
            {
              ret = bk7258_saradc_record_error(BK7258_SARADC_STAGE_ACQUIRE,
                                               sdk_error);
              bk7258_saradc_mark_fault(priv);
              goto trigger_cleanup;
            }

          g_bk7258_saradc_diag.resources |=
            BK7258_SARADC_RESOURCE_ACQUIRED;
          g_bk7258_saradc_diag.acquire_count++;

          /* Mark cleanup obligations before the init/start RPCs.  Their AP
           * stubs report the same BK_FAIL when the request failed and when
           * CP committed it but the reply was lost.  Both inverse calls are
           * safe while this client still owns the ADC mutex.
           */

          g_bk7258_saradc_diag.resources |=
            BK7258_SARADC_RESOURCE_INITED;
          sdk_error = bk_adc_init((adc_chan_t)priv->chan);
          if (sdk_error != BK_OK)
            {
              ret = bk7258_saradc_record_error(BK7258_SARADC_STAGE_INIT,
                                               sdk_error);
              goto trigger_cleanup;
            }

          g_bk7258_saradc_diag.init_count++;

          /* bk_adc_init() resets the hardware control registers, including
           * the conversion mode.  Reapply the complete, zero-initialized
           * v3.1.1.9 reference configuration on every trigger because other
           * CP-side SARADC clients may have used a different channel/config
           * between conversions.
           */

          memset(&config, 0, sizeof(config));
          config.clk = BK7258_SARADC_REFERENCE_CLK;
          config.sample_rate = 0;
          config.adc_filter = 0;
          config.steady_ctrl = BK7258_SARADC_REFERENCE_STEADY;
          config.adc_mode = ADC_CONTINUOUS_MODE;
          config.src_clk = ADC_SCLK_XTAL_26M;
          config.chan = (adc_chan_t)priv->chan;
          config.saturate_mode = ADC_SATURATE_MODE_3;
          config.vol_div = ADC_VOL_DIV_NONE;

          sdk_error = bk_adc_set_config(&config);
          if (sdk_error != BK_OK)
            {
              ret = bk7258_saradc_record_error(
                      BK7258_SARADC_STAGE_CONFIG, sdk_error);
              goto trigger_cleanup;
            }

          g_bk7258_saradc_diag.config_count++;

          /* NuttX publishes raw ADC samples.  Match the official reference
           * path and bypass the hardware offset-calibration result.
           * bk_adc_init() resets this bit before every conversion.
           */

          sdk_error = bk_adc_enable_bypass_clalibration();
          if (sdk_error != BK_OK)
            {
              ret = bk7258_saradc_record_error(
                      BK7258_SARADC_STAGE_BYPASS_CALIBRATION, sdk_error);
              goto trigger_cleanup;
            }

          g_bk7258_saradc_diag.bypass_count++;

          g_bk7258_saradc_diag.resources |=
            BK7258_SARADC_RESOURCE_STARTED;
          sdk_error = bk_adc_start();
          if (sdk_error != BK_OK)
            {
              ret = bk7258_saradc_record_error(BK7258_SARADC_STAGE_START,
                                               sdk_error);
              goto trigger_cleanup;
            }

          g_bk7258_saradc_diag.start_count++;

          sdk_error = bk_adc_read(&raw,
                        CONFIG_BK7258_SARADC_READ_TIMEOUT_MS);
          if (sdk_error != BK_OK)
            {
              ret = bk7258_saradc_record_error(BK7258_SARADC_STAGE_READ,
                                               sdk_error);
              goto trigger_cleanup;
            }

          g_bk7258_saradc_diag.sample_count++;
          g_bk7258_saradc_diag.last_raw = raw;
          if (g_bk7258_saradc_diag.sample_count == 1 ||
              raw < g_bk7258_saradc_diag.minimum_raw)
            {
              g_bk7258_saradc_diag.minimum_raw = raw;
            }

          if (g_bk7258_saradc_diag.sample_count == 1 ||
              raw > g_bk7258_saradc_diag.maximum_raw)
            {
              g_bk7258_saradc_diag.maximum_raw = raw;
            }

          deliver = true;
          ret = OK;

trigger_cleanup:
          cleanup_error = bk7258_saradc_cleanup_conversion(priv);
          if (ret == OK && cleanup_error < 0)
            {
              ret = cleanup_error;
              deliver = false;
            }

          if (ret == OK)
            {
              g_bk7258_saradc_diag.last_stage =
                BK7258_SARADC_STAGE_COMPLETE;
              g_bk7258_saradc_diag.last_error = OK;
              g_bk7258_saradc_diag.last_sdk_error = BK_OK;
            }

trigger_out:
          bk7258_saradc_diag_publish();
          nxmutex_unlock(&priv->lock);

          if (deliver)
            {
              int callback_error;

              /* This is the fixed NuttX ADC upper callback, not an
               * application callback.  Its FIFO producer path omits its own
               * spin lock, so take the public adc_dev_s lock to serialize
               * all synchronous producers against SMP readers and
               * poll/reset operations.  Do not retain the private mutex
               * across adc_notify(): poll notification and semaphore wakeup
               * are external upper-half paths and may schedule a caller that
               * immediately issues another ioctl.
               */

              flags = spin_lock_irqsave(&dev->ad_spinlock);
              callback_error =
                priv->cb->au_receive(dev, (uint8_t)priv->chan,
                                     (int32_t)raw);
              spin_unlock_irqrestore(&dev->ad_spinlock, flags);
              if (callback_error < 0)
                {
                  if (nxmutex_lock(&priv->lock) >= 0)
                    {
                      g_bk7258_saradc_diag.callback_error_count++;
                      g_bk7258_saradc_diag.last_stage =
                        BK7258_SARADC_STAGE_DELIVER;
                      g_bk7258_saradc_diag.last_error = callback_error;
                      g_bk7258_saradc_diag.last_sdk_error = BK_OK;
                      if (g_bk7258_saradc_diag.first_error == OK)
                        {
                          g_bk7258_saradc_diag.first_error = callback_error;
                          g_bk7258_saradc_diag.first_sdk_error = BK_OK;
                        }

                      bk7258_saradc_diag_publish();
                      nxmutex_unlock(&priv->lock);
                    }

                  return callback_error;
                }

              if (nxmutex_lock(&priv->lock) >= 0)
                {
                  g_bk7258_saradc_diag.deliver_count++;
                  bk7258_saradc_diag_publish();
                  nxmutex_unlock(&priv->lock);
                }
            }
        }
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  (void)arg;
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_saradc_initialize(void)
{
  int ret;

  if (g_bk7258_saradc_registered)
    {
      return OK;
    }

  memset((FAR void *)&g_bk7258_saradc_diag, 0,
         sizeof(g_bk7258_saradc_diag));
  g_bk7258_saradc_diag.magic = BK7258_SARADC_DIAG_MAGIC;
  g_bk7258_saradc_diag.version = BK7258_SARADC_DIAG_VERSION;
  g_bk7258_saradc_diag.size = sizeof(g_bk7258_saradc_diag);
  g_bk7258_saradc_diag.state = BK7258_SARADC_STATE_RESET;
  g_bk7258_saradc_diag.channel = CONFIG_BK7258_SARADC_CHAN;
  bk7258_saradc_diag_publish();

  ret = adc_register(CONFIG_BK7258_SARADC_DEVNAME, &g_bk7258_saradc.dev);
  if (ret >= 0)
    {
      g_bk7258_saradc_registered = true;
    }

  return ret;
}

int bk7258_saradc_get_diag(struct bk7258_saradc_diag_s *diag)
{
  int ret;

  if (diag == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_saradc.lock);
  if (ret < 0)
    {
      return ret;
    }

  memcpy(diag, (FAR const void *)&g_bk7258_saradc_diag, sizeof(*diag));
  nxmutex_unlock(&g_bk7258_saradc.lock);
  return OK;
}

#endif /* CONFIG_BK7258_SARADC */
