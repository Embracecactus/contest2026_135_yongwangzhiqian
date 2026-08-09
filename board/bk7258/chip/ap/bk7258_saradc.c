/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/ap/bk7258_saradc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) SARADC — NuttX adc_dev_s lower-half wrapper.
 *
 * Wraps the official Beken bk_adc_* SDK API in a NuttX ADC lower half.
 * The BK7258 SARADC is a 12-bit successive-approximation converter with
 * channels ADC_0..ADC_15.  Only the AP core has the bk_adc_* symbols
 * (verified: 18 T symbols in AP libdriver.a, 0 in CP), so this driver is
 * AP-only.
 *
 * SDK call mapping (polling single-shot mode, no interrupts):
 *   ao_bind()    -> stash the upper-half adc_callback_s
 *   ao_setup()   -> bk_adc_init(chan); bk_adc_set_channel(chan)
 *   ao_shutdown()-> bk_adc_deinit(chan)
 *   ao_rxint()   -> no-op (SDK SARADC path is IPC/blocking, not IRQ)
 *   ao_ioctl()   -> ANIOC_TRIGGER: bk_adc_single_read() then
 *                   cb->au_receive(dev, chan, raw)
 *
 * SDK semantics (verified in saradc_client.c):
 *   - Every bk_adc_* API internally re-runs bk_saradc_driver_init() and
 *     performs a mailbox IPC round-trip (mb_ipc_send/recv) against the
 *     SARADC service on the partner core.  The service must already be up
 *     (SDK owns that bring-up); this wrapper does not start it.
 *   - bk_adc_single_read() does a single-shot conversion of the selected
 *     channel and returns the calibrated 16-bit raw sample.
 *   - bk_adc_init(chan) selects the channel; bk_adc_set_channel(chan)
 *     re-selects later without a full re-init.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SARADC

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
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

/* Negative errno mapping for SDK failures. */

#define BK7258_SARADC_ERR_BASE          2000

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
  bool inited;                          /* bk_adc_init() done? */
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
  .inited     = false,
};
static bool g_bk7258_saradc_registered;

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

/****************************************************************************
 * Name: bk7258_saradc_setup
 *
 * Initialize the SDK SARADC and select the channel.  bk_adc_init() already
 * runs bk_saradc_driver_init() internally, so no explicit driver_init is
 * needed (the header-declared bk_adc_driver_init symbol is not even
 * present in the AP lib).
 ****************************************************************************/

static int bk7258_saradc_setup(FAR struct adc_dev_s *dev)
{
  FAR struct bk7258_saradc_priv_s *priv =
    (FAR struct bk7258_saradc_priv_s *)dev;
  bk_err_t ret;

  if (priv->inited)
    {
      return OK;
    }

  ret = bk_adc_init((adc_chan_t)priv->chan);
  if (ret != BK_OK)
    {
      return -EIO;
    }

  ret = bk_adc_set_channel((adc_chan_t)priv->chan);
  if (ret != BK_OK)
    {
      bk_adc_deinit((adc_chan_t)priv->chan);
      return -EIO;
    }

  priv->inited = true;
  return OK;
}

/****************************************************************************
 * Name: bk7258_saradc_shutdown
 ****************************************************************************/

static void bk7258_saradc_shutdown(FAR struct adc_dev_s *dev)
{
  FAR struct bk7258_saradc_priv_s *priv =
    (FAR struct bk7258_saradc_priv_s *)dev;

  if (priv->inited)
    {
      bk_adc_deinit((adc_chan_t)priv->chan);
      priv->inited = false;
    }
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
  bk_err_t ret;
  int rc = OK;

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

          ret = bk_adc_single_read(&raw);
          if (ret != BK_OK)
            {
              rc = -EIO;
            }
          else
            {
              deliver = true;
            }

          nxmutex_unlock(&priv->lock);

          /* The upper-half callback may wake readers or re-enter control
           * paths.  Never invoke it while holding the conversion mutex.
           */

          if (deliver)
            {
              priv->cb->au_receive(dev, (uint8_t)priv->chan,
                                   (int32_t)raw);
            }
        }
        break;

      default:
        rc = -ENOTTY;
        break;
    }

  (void)arg;
  return rc;
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

  ret = adc_register(CONFIG_BK7258_SARADC_DEVNAME, &g_bk7258_saradc.dev);
  if (ret >= 0)
    {
      g_bk7258_saradc_registered = true;
    }

  return ret;
}

#endif /* CONFIG_BK7258_SARADC */
