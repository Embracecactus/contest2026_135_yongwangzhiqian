/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/bk7258_sdmadc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) SDMADC — NuttX adc_dev_s lower-half wrapper.
 *
 * Wraps the official Beken bk_sdmadc_* SDK API in a NuttX ADC lower half.
 * The BK7258 SDMADC is a 16-bit sigma-delta converter; bk_sdmadc_single_read()
 * performs a full single-shot conversion and returns the averaged sample.
 * Only the AP core runs this wrapper (consistent with the other drivers);
 * the 18 bk_sdmadc_* symbols exist in both AP and CP libdriver.a.
 *
 * SDK call mapping (polling single-shot mode, no interrupts):
 *   ao_bind()     -> stash the upper-half adc_callback_s
 *   ao_setup()    -> bk_sdmadc_driver_init(); bk_sdmadc_init()
 *   ao_shutdown() -> bk_sdmadc_deinit()
 *   ao_rxint()    -> no-op (SDK SDMADC path is blocking)
 *   ao_ioctl()    -> ANIOC_TRIGGER: bk_sdmadc_single_read(&val, chan)
 *                    then cb->au_receive(dev, chan, val)
 *
 * SDK semantics (verified in sdmadc_driver.c):
 *   - bk_sdmadc_init() returns BK_FAIL unless bk_sdmadc_driver_init() ran
 *     first (it checks s_sdmadc_driver_is_init), so setup() calls both.
 *   - bk_sdmadc_single_read(&val, chan) internally sets the config, starts
 *     the conversion, waits on a semaphore and averages the samples.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SDMADC

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/mutex.h>

#include <arch/chip/bk7258_sdmadc.h>

#include <driver/sdmadc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_SDMADC_BUS
#  define CONFIG_BK7258_SDMADC_BUS      0
#endif

#ifndef CONFIG_BK7258_SDMADC_CHAN
#  define CONFIG_BK7258_SDMADC_CHAN     1
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_sdmadc_priv_s
{
  struct adc_dev_s dev;                 /* NuttX ADC lower-half anchor */
  FAR const struct adc_callback_s *cb;  /* Upper-half callbacks (ao_bind) */
  mutex_t lock;                         /* Serialize TRIGGER ioctls */
  uint8_t chan;                         /* SDMADC channel */
  bool bound;                           /* cb set? */
  bool inited;                          /* bk_sdmadc_init() done? */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_sdmadc_bind(FAR struct adc_dev_s *dev,
                              FAR const struct adc_callback_s *callback);
static void bk7258_sdmadc_reset(FAR struct adc_dev_s *dev);
static int bk7258_sdmadc_setup(FAR struct adc_dev_s *dev);
static void bk7258_sdmadc_shutdown(FAR struct adc_dev_s *dev);
static void bk7258_sdmadc_rxint(FAR struct adc_dev_s *dev, bool enable);
static int bk7258_sdmadc_ioctl(FAR struct adc_dev_s *dev, int cmd,
                               unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct adc_ops_s g_bk7258_sdmadc_ops =
{
  .ao_bind      = bk7258_sdmadc_bind,
  .ao_reset     = bk7258_sdmadc_reset,
  .ao_setup     = bk7258_sdmadc_setup,
  .ao_shutdown  = bk7258_sdmadc_shutdown,
  .ao_rxint     = bk7258_sdmadc_rxint,
  .ao_ioctl     = bk7258_sdmadc_ioctl,
};

static struct bk7258_sdmadc_priv_s g_bk7258_sdmadc =
{
  .dev.ad_ops = &g_bk7258_sdmadc_ops,
  .lock       = NXMUTEX_INITIALIZER,
  .chan       = (uint8_t)CONFIG_BK7258_SDMADC_CHAN,
  .bound      = false,
  .inited     = false,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_sdmadc_bind
 ****************************************************************************/

static int bk7258_sdmadc_bind(FAR struct adc_dev_s *dev,
                              FAR const struct adc_callback_s *callback)
{
  FAR struct bk7258_sdmadc_priv_s *priv =
    (FAR struct bk7258_sdmadc_priv_s *)dev;

  if (callback == NULL)
    {
      return -EINVAL;
    }

  priv->cb = callback;
  priv->bound = true;
  return OK;
}

/****************************************************************************
 * Name: bk7258_sdmadc_reset
 *
 * Nothing to reset: the SDK owns all SDMADC hardware state.
 ****************************************************************************/

static void bk7258_sdmadc_reset(FAR struct adc_dev_s *dev)
{
  (void)dev;
}

/****************************************************************************
 * Name: bk7258_sdmadc_setup
 *
 * Initialize the SDK SDMADC.  bk_sdmadc_init() requires the driver to be
 * initialised first (it checks an internal flag), so both are called here.
 ****************************************************************************/

static int bk7258_sdmadc_setup(FAR struct adc_dev_s *dev)
{
  FAR struct bk7258_sdmadc_priv_s *priv =
    (FAR struct bk7258_sdmadc_priv_s *)dev;
  bk_err_t ret;

  if (priv->inited)
    {
      return OK;
    }

  ret = bk_sdmadc_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  ret = bk_sdmadc_init();
  if (ret != BK_OK)
    {
      bk_sdmadc_driver_deinit();
      return -EIO;
    }

  priv->inited = true;
  return OK;
}

/****************************************************************************
 * Name: bk7258_sdmadc_shutdown
 ****************************************************************************/

static void bk7258_sdmadc_shutdown(FAR struct adc_dev_s *dev)
{
  FAR struct bk7258_sdmadc_priv_s *priv =
    (FAR struct bk7258_sdmadc_priv_s *)dev;

  if (priv->inited)
    {
      bk_sdmadc_deinit();
      bk_sdmadc_driver_deinit();
      priv->inited = false;
    }
}

/****************************************************************************
 * Name: bk7258_sdmadc_rxint
 *
 * No-op: the SDK SDMADC path is blocking, there is no interrupt to
 * enable or disable from this lower half.
 ****************************************************************************/

static void bk7258_sdmadc_rxint(FAR struct adc_dev_s *dev, bool enable)
{
  (void)dev;
  (void)enable;
}

/****************************************************************************
 * Name: bk7258_sdmadc_ioctl
 *
 * ANIOC_TRIGGER performs a single-shot conversion and pushes the averaged
 * sample to the upper half via au_receive().
 ****************************************************************************/

static int bk7258_sdmadc_ioctl(FAR struct adc_dev_s *dev, int cmd,
                               unsigned long arg)
{
  FAR struct bk7258_sdmadc_priv_s *priv =
    (FAR struct bk7258_sdmadc_priv_s *)dev;
  int16_t raw;
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

          ret = bk_sdmadc_single_read(&raw, (uint16_t)priv->chan);
          if (ret != BK_OK)
            {
              rc = -EIO;
            }
          else
            {
              priv->cb->au_receive(dev, (uint8_t)priv->chan,
                                   (int32_t)raw);
            }

          nxmutex_unlock(&priv->lock);
        }
        break;

      default:
        rc = -ENOTTY;
        break;
    }

  return rc;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_sdmadc_initialize(void)
{
  return adc_register(CONFIG_BK7258_SDMADC_DEVNAME, &g_bk7258_sdmadc.dev);
}

#endif /* CONFIG_BK7258_SDMADC */
