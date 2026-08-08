/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/bk7258_i2s.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) I2S — NuttX i2s_dev_s lower-half wrapper.
 *
 * Wraps the official Beken bk_i2s_* SDK API as a NuttX I2S lower half.
 * The 33 bk_i2s_* symbols live exclusively in the AP libdriver.a (CP
 * exports zero), so this driver is AP-only.
 *
 * The SDK's bk_i2s_write_data()/read_data() are raw register pushes/pulls
 * without FIFO waits, so this wrapper implements the NuttX i2s_send()/
 * i2s_receive() as synchronous polled loops: each 32-bit sample waits for
 * the FIFO ready flag (bk_i2s_get_write_ready()/get_read_ready()) before
 * transferring, with a bounded retry count as a watchdog.  The completion
 * callback runs in the caller's context once the whole buffer is done.
 *
 * NuttX -> SDK mapping:
 *   i2s_txsamplerate/rxsamplerate -> bk_i2s_set_samp_rate()
 *   i2s_txdatawidth/rxdatawidth   -> bk_i2s_set_data_len()
 *   i2s_send()                    -> polled write of apb->samp
 *   i2s_receive()                 -> polled read into apb->samp
 *   i2s_ioctl()                   -> -ENOTTY (no I2S-specific ioctls)
 *
 * The hardware block is brought up lazily on first use (driver_init +
 * init with the configured GPIO group + enable).
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_I2S

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/audio/i2s.h>
#include <nuttx/audio/audio.h>
#include <nuttx/mutex.h>

#include <arch/chip/bk7258_i2s.h>

#include <driver/i2s.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_I2S_GPIO_GROUP
#  define CONFIG_BK7258_I2S_GPIO_GROUP  0
#endif

/* Retry budget for polling a single FIFO ready flag before giving up. */

#define BK7258_I2S_FIFO_RETRIES         1000000u

/* Data is 32-bit per sample in the SDK API. */

#define BK7258_I2S_SAMPLE_BYTES         4u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_i2s_priv_s
{
  struct i2s_dev_s dev;             /* NuttX I2S lower-half anchor */
  mutex_t lock;                     /* serialize send/receive/init */
  uint8_t gpio_group;               /* I2S GPIO group (0..2) */
  uint8_t txchannels;               /* cached TX channel count */
  uint8_t rxchannels;               /* cached RX channel count */
  uint32_t samplerate;              /* cached sample rate in Hz */
  uint16_t datawidth;               /* cached data width in bits */
  bool inited;                      /* bk_i2s_init() done */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_i2s_rxchannels(FAR struct i2s_dev_s *dev,
                                 uint8_t channels);
static uint32_t bk7258_i2s_rxsamplerate(FAR struct i2s_dev_s *dev,
                                        uint32_t rate);
static uint32_t bk7258_i2s_rxdatawidth(FAR struct i2s_dev_s *dev, int bits);
static int bk7258_i2s_receive(FAR struct i2s_dev_s *dev,
                              FAR struct ap_buffer_s *apb,
                              i2s_callback_t callback, FAR void *arg,
                              uint32_t timeout);
static int bk7258_i2s_txchannels(FAR struct i2s_dev_s *dev,
                                 uint8_t channels);
static uint32_t bk7258_i2s_txsamplerate(FAR struct i2s_dev_s *dev,
                                        uint32_t rate);
static uint32_t bk7258_i2s_txdatawidth(FAR struct i2s_dev_s *dev, int bits);
static int bk7258_i2s_send(FAR struct i2s_dev_s *dev,
                           FAR struct ap_buffer_s *apb,
                           i2s_callback_t callback, FAR void *arg,
                           uint32_t timeout);
static int bk7258_i2s_ioctl(FAR struct i2s_dev_s *dev, int cmd,
                            unsigned long arg);

static int bk7258_i2s_ensure_init(FAR struct bk7258_i2s_priv_s *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct i2s_ops_s g_bk7258_i2s_ops =
{
  .i2s_rxchannels   = bk7258_i2s_rxchannels,
  .i2s_rxsamplerate = bk7258_i2s_rxsamplerate,
  .i2s_rxdatawidth  = bk7258_i2s_rxdatawidth,
  .i2s_receive      = bk7258_i2s_receive,
  .i2s_txchannels   = bk7258_i2s_txchannels,
  .i2s_txsamplerate = bk7258_i2s_txsamplerate,
  .i2s_txdatawidth  = bk7258_i2s_txdatawidth,
  .i2s_send         = bk7258_i2s_send,
  .i2s_ioctl        = bk7258_i2s_ioctl,
};

static struct bk7258_i2s_priv_s g_bk7258_i2s =
{
  .dev.ops      = &g_bk7258_i2s_ops,
  .lock         = NXMUTEX_INITIALIZER,
  .gpio_group   = (uint8_t)CONFIG_BK7258_I2S_GPIO_GROUP,
  .txchannels   = 2,
  .rxchannels   = 2,
  .samplerate   = 16000,
  .datawidth    = 16,
  .inited       = false,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_i2s_rate_to_enum
 *
 * Map a NuttX sample rate in Hz onto the nearest SDK i2s_samp_rate_t.
 ****************************************************************************/

static i2s_samp_rate_t bk7258_i2s_rate_to_enum(uint32_t rate)
{
  switch (rate)
    {
      case 8000:
        return I2S_SAMP_RATE_8000;
      case 12000:
        return I2S_SAMP_RATE_12000;
      case 16000:
        return I2S_SAMP_RATE_16000;
      case 24000:
        return I2S_SAMP_RATE_24000;
      case 32000:
        return I2S_SAMP_RATE_32000;
      case 48000:
        return I2S_SAMP_RATE_48000;
      case 96000:
        return I2S_SAMP_RATE_96000;
      case 11025:
        return I2S_SAMP_RATE_11025;
      case 22050:
        return I2S_SAMP_RATE_22050;
      case 44100:
        return I2S_SAMP_RATE_44100;
      case 88200:
        return I2S_SAMP_RATE_88200;
      default:
        return I2S_SAMP_RATE_16000;
    }
}

/****************************************************************************
 * Name: bk7258_i2s_ensure_init
 *
 * Bring up the I2S block lazily: driver_init + init(group) + enable.
 ****************************************************************************/

static int bk7258_i2s_ensure_init(FAR struct bk7258_i2s_priv_s *priv)
{
  i2s_config_t cfg = DEFAULT_I2S_CONFIG();
  bk_err_t ret;

  if (priv->inited)
    {
      return OK;
    }

  ret = bk_i2s_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  cfg.samp_rate = bk7258_i2s_rate_to_enum(priv->samplerate);
  cfg.data_length = priv->datawidth;

  ret = bk_i2s_init((i2s_gpio_group_id_t)priv->gpio_group, &cfg);
  if (ret != BK_OK)
    {
      bk_i2s_driver_deinit();
      return -EIO;
    }

  (void)bk_i2s_set_role(I2S_ROLE_MASTER);

  ret = bk_i2s_enable(I2S_ENABLE);
  if (ret != BK_OK)
    {
      bk_i2s_deinit();
      bk_i2s_driver_deinit();
      return -EIO;
    }

  priv->inited = true;
  return OK;
}

/****************************************************************************
 * Name: bk7258_i2s_rxchannels / txchannels
 ****************************************************************************/

static int bk7258_i2s_rxchannels(FAR struct i2s_dev_s *dev,
                                 uint8_t channels)
{
  FAR struct bk7258_i2s_priv_s *priv =
    (FAR struct bk7258_i2s_priv_s *)dev;

  priv->rxchannels = channels;
  return OK;
}

static int bk7258_i2s_txchannels(FAR struct i2s_dev_s *dev,
                                 uint8_t channels)
{
  FAR struct bk7258_i2s_priv_s *priv =
    (FAR struct bk7258_i2s_priv_s *)dev;

  priv->txchannels = channels;
  return OK;
}

/****************************************************************************
 * Name: bk7258_i2s_rxsamplerate / txsamplerate
 ****************************************************************************/

static uint32_t bk7258_i2s_rxsamplerate(FAR struct i2s_dev_s *dev,
                                        uint32_t rate)
{
  FAR struct bk7258_i2s_priv_s *priv =
    (FAR struct bk7258_i2s_priv_s *)dev;

  (void)bk_i2s_set_samp_rate(bk7258_i2s_rate_to_enum(rate));
  priv->samplerate = rate;
  return rate;
}

static uint32_t bk7258_i2s_txsamplerate(FAR struct i2s_dev_s *dev,
                                        uint32_t rate)
{
  return bk7258_i2s_rxsamplerate(dev, rate);
}

/****************************************************************************
 * Name: bk7258_i2s_rxdatawidth / txdatawidth
 ****************************************************************************/

static uint32_t bk7258_i2s_rxdatawidth(FAR struct i2s_dev_s *dev, int bits)
{
  FAR struct bk7258_i2s_priv_s *priv =
    (FAR struct bk7258_i2s_priv_s *)dev;

  (void)bk_i2s_set_data_len((uint32_t)bits);
  priv->datawidth = (uint16_t)bits;
  return (uint32_t)bits;
}

static uint32_t bk7258_i2s_txdatawidth(FAR struct i2s_dev_s *dev, int bits)
{
  return bk7258_i2s_rxdatawidth(dev, bits);
}

/****************************************************************************
 * Name: bk7258_i2s_receive
 *
 * Poll the RX FIFO ready flag and read the whole buffer synchronously,
 * then invoke the completion callback.
 ****************************************************************************/

static int bk7258_i2s_receive(FAR struct i2s_dev_s *dev,
                              FAR struct ap_buffer_s *apb,
                              i2s_callback_t callback, FAR void *arg,
                              uint32_t timeout)
{
  FAR struct bk7258_i2s_priv_s *priv =
    (FAR struct bk7258_i2s_priv_s *)dev;
  uint32_t *buf;
  uint32_t nsamples;
  uint32_t i;
  int rc = OK;

  if (apb == NULL)
    {
      return -EINVAL;
    }

  rc = nxmutex_lock(&priv->lock);
  if (rc < 0)
    {
      return rc;
    }

  rc = bk7258_i2s_ensure_init(priv);
  if (rc < 0)
    {
      goto out;
    }

  buf = (FAR uint32_t *)apb->samp;
  nsamples = apb->nbytes / BK7258_I2S_SAMPLE_BYTES;

  for (i = 0; i < nsamples; i++)
    {
      uint32_t flag;
      uint32_t retries;
      bk_err_t ret;

      for (retries = 0; retries < BK7258_I2S_FIFO_RETRIES; retries++)
        {
          ret = bk_i2s_get_read_ready(&flag);
          if (ret == BK_OK && flag != 0)
            {
              break;
            }
        }

      if (retries >= BK7258_I2S_FIFO_RETRIES)
        {
          rc = -ETIMEDOUT;
          goto out;
        }

      if (bk_i2s_read_data(&buf[i], 1) != BK_OK)
        {
          rc = -EIO;
          goto out;
        }
    }

out:
  nxmutex_unlock(&priv->lock);

  if (callback != NULL)
    {
      callback(dev, apb, arg, rc);
    }

  return rc;
}

/****************************************************************************
 * Name: bk7258_i2s_send
 *
 * Poll the TX FIFO ready flag and write the whole buffer synchronously,
 * then invoke the completion callback.
 ****************************************************************************/

static int bk7258_i2s_send(FAR struct i2s_dev_s *dev,
                           FAR struct ap_buffer_s *apb,
                           i2s_callback_t callback, FAR void *arg,
                           uint32_t timeout)
{
  FAR struct bk7258_i2s_priv_s *priv =
    (FAR struct bk7258_i2s_priv_s *)dev;
  FAR uint32_t *buf;
  uint32_t nsamples;
  uint32_t i;
  int rc = OK;

  if (apb == NULL)
    {
      return -EINVAL;
    }

  rc = nxmutex_lock(&priv->lock);
  if (rc < 0)
    {
      return rc;
    }

  rc = bk7258_i2s_ensure_init(priv);
  if (rc < 0)
    {
      goto out;
    }

  buf = (FAR uint32_t *)apb->samp;
  nsamples = apb->nbytes / BK7258_I2S_SAMPLE_BYTES;

  for (i = 0; i < nsamples; i++)
    {
      uint32_t flag;
      uint32_t retries;
      bk_err_t ret;

      for (retries = 0; retries < BK7258_I2S_FIFO_RETRIES; retries++)
        {
          ret = bk_i2s_get_write_ready(&flag);
          if (ret == BK_OK && flag != 0)
            {
              break;
            }
        }

      if (retries >= BK7258_I2S_FIFO_RETRIES)
        {
          rc = -ETIMEDOUT;
          goto out;
        }

      if (bk_i2s_write_data(0, &buf[i], 1) != BK_OK)
        {
          rc = -EIO;
          goto out;
        }
    }

out:
  nxmutex_unlock(&priv->lock);

  if (callback != NULL)
    {
      callback(dev, apb, arg, rc);
    }

  return rc;
}

/****************************************************************************
 * Name: bk7258_i2s_ioctl
 ****************************************************************************/

static int bk7258_i2s_ioctl(FAR struct i2s_dev_s *dev, int cmd,
                            unsigned long arg)
{
  (void)dev;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct i2s_dev_s *bk7258_i2s_initialize(void)
{
  return &g_bk7258_i2s.dev;
}

#endif /* CONFIG_BK7258_I2S */
