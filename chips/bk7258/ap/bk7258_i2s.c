/****************************************************************************
 * board/bk7258/chip/ap/bk7258_i2s.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 I2S lower half over the official v3.1.1.9 AP SDK API.
 *
 * NuttX requires send()/receive() to enqueue and return immediately, with
 * completion callbacks running in a worker thread.  The SDK only exposes
 * polling FIFO access, so one CPU0-pinned worker serializes queued transfers,
 * applies the caller's tick timeout, and sleeps between FIFO polls.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_I2S

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/audio/audio.h>
#include <nuttx/audio/i2s.h>
#include <nuttx/clock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/queue.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_i2s.h>

#include <driver/i2s.h>

#ifndef CONFIG_BK7258_I2S_GPIO_GROUP
#  define CONFIG_BK7258_I2S_GPIO_GROUP       0
#endif
#ifndef CONFIG_BK7258_I2S_WORKER_PRIORITY
#  define CONFIG_BK7258_I2S_WORKER_PRIORITY  120
#endif
#ifndef CONFIG_BK7258_I2S_WORKER_STACKSIZE
#  define CONFIG_BK7258_I2S_WORKER_STACKSIZE 2048
#endif
#ifndef CONFIG_BK7258_I2S_QUEUE_DEPTH
#  define CONFIG_BK7258_I2S_QUEUE_DEPTH      8
#endif

#define BK7258_I2S_FIFO_POLL_US              50

enum bk7258_i2s_direction_e
{
  BK7258_I2S_RX = 0,
  BK7258_I2S_TX
};

struct bk7258_i2s_xfer_s
{
  sq_entry_t entry;
  FAR struct ap_buffer_s *apb;
  i2s_callback_t callback;
  FAR void *arg;
  uint32_t timeout;
  enum bk7258_i2s_direction_e direction;
};

struct bk7258_i2s_priv_s
{
  struct i2s_dev_s dev;
  mutex_t lock;
  sem_t queuesem;
  sq_queue_t queue;
  pthread_t worker;
  uint8_t gpio_group;
  uint8_t txchannels;
  uint8_t rxchannels;
  uint8_t queued;
  uint32_t samplerate;
  uint16_t datawidth;
  bool queue_ready;
  bool worker_ready;
  bool active;
  bool inited;
};

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

static const struct i2s_ops_s g_bk7258_i2s_ops =
{
  .i2s_rxchannels    = bk7258_i2s_rxchannels,
  .i2s_rxsamplerate  = bk7258_i2s_rxsamplerate,
  .i2s_rxdatawidth   = bk7258_i2s_rxdatawidth,
  .i2s_receive       = bk7258_i2s_receive,
  .i2s_txchannels    = bk7258_i2s_txchannels,
  .i2s_txsamplerate  = bk7258_i2s_txsamplerate,
  .i2s_txdatawidth   = bk7258_i2s_txdatawidth,
  .i2s_send          = bk7258_i2s_send,
  .i2s_ioctl         = bk7258_i2s_ioctl,
};

static struct bk7258_i2s_priv_s g_bk7258_i2s =
{
  .dev.ops      = &g_bk7258_i2s_ops,
  .lock         = NXMUTEX_INITIALIZER,
  .gpio_group   = CONFIG_BK7258_I2S_GPIO_GROUP,
  .txchannels   = 2,
  .rxchannels   = 2,
  .samplerate   = 16000,
  .datawidth    = 16,
};

static int bk7258_i2s_rate_to_enum(uint32_t rate,
                                   FAR i2s_samp_rate_t *value)
{
  switch (rate)
    {
      case 8000:
        *value = I2S_SAMP_RATE_8000;
        break;
      case 12000:
        *value = I2S_SAMP_RATE_12000;
        break;
      case 16000:
        *value = I2S_SAMP_RATE_16000;
        break;
      case 24000:
        *value = I2S_SAMP_RATE_24000;
        break;
      case 32000:
        *value = I2S_SAMP_RATE_32000;
        break;
      case 48000:
        *value = I2S_SAMP_RATE_48000;
        break;
      case 96000:
        *value = I2S_SAMP_RATE_96000;
        break;
      case 11025:
        *value = I2S_SAMP_RATE_11025;
        break;
      case 22050:
        *value = I2S_SAMP_RATE_22050;
        break;
      case 44100:
        *value = I2S_SAMP_RATE_44100;
        break;
      case 88200:
        *value = I2S_SAMP_RATE_88200;
        break;
      default:
        return -EINVAL;
    }

  return 0;
}

static int bk7258_i2s_ensure_init(FAR struct bk7258_i2s_priv_s *priv)
{
  i2s_config_t cfg = DEFAULT_I2S_CONFIG();
  i2s_samp_rate_t rate;

  if (priv->inited)
    {
      return 0;
    }

  if (bk7258_i2s_rate_to_enum(priv->samplerate, &rate) < 0)
    {
      return -EINVAL;
    }

  if (bk_i2s_driver_init() != BK_OK)
    {
      return -EIO;
    }

  cfg.samp_rate = rate;
  cfg.data_length = priv->datawidth;

  if (bk_i2s_init((i2s_gpio_group_id_t)priv->gpio_group, &cfg) != BK_OK)
    {
      (void)bk_i2s_driver_deinit();
      return -EIO;
    }

  if (bk_i2s_set_role(I2S_ROLE_MASTER) != BK_OK ||
      bk_i2s_enable(I2S_ENABLE) != BK_OK)
    {
      (void)bk_i2s_deinit();
      (void)bk_i2s_driver_deinit();
      return -EIO;
    }

  priv->inited = true;
  return 0;
}

static bool bk7258_i2s_timedout(clock_t start, uint32_t timeout)
{
  return timeout != 0 &&
         (clock_t)(clock_systime_ticks() - start) >= (clock_t)timeout;
}

static int bk7258_i2s_wait_ready(bool tx, clock_t start, uint32_t timeout)
{
  uint32_t ready;
  bk_err_t ret;

  for (; ; )
    {
      ready = 0;
      ret = tx ? bk_i2s_get_write_ready(&ready) :
                 bk_i2s_get_read_ready(&ready);
      if (ret != BK_OK)
        {
          return -EIO;
        }

      if (ready != 0)
        {
          return 0;
        }

      if (bk7258_i2s_timedout(start, timeout))
        {
          return -ETIMEDOUT;
        }

      (void)nxsig_usleep(BK7258_I2S_FIFO_POLL_US);
    }
}

static uint32_t bk7258_i2s_unpack(FAR const uint8_t *data, uint8_t width)
{
  uint32_t sample = 0;
  uint8_t bytes = (width + 7) / 8;
  uint8_t i;

  for (i = 0; i < bytes; i++)
    {
      sample |= (uint32_t)data[i] << (i * 8);
    }

  return sample;
}

static void bk7258_i2s_pack(FAR uint8_t *data, uint8_t width,
                            uint32_t sample)
{
  uint8_t bytes = (width + 7) / 8;
  uint8_t i;

  for (i = 0; i < bytes; i++)
    {
      data[i] = (uint8_t)(sample >> (i * 8));
    }
}

static int bk7258_i2s_process(FAR struct bk7258_i2s_priv_s *priv,
                              FAR struct bk7258_i2s_xfer_s *xfer)
{
  FAR struct ap_buffer_s *apb = xfer->apb;
  FAR uint8_t *data;
  uint32_t length;
  uint32_t sample;
  uint32_t offset;
  uint8_t sample_bytes;
  uint8_t channels;
  clock_t start;
  bool locked = true;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_i2s_ensure_init(priv);
  if (ret < 0)
    {
      goto out;
    }

  sample_bytes = (priv->datawidth + 7) / 8;
  channels = xfer->direction == BK7258_I2S_TX ?
             priv->txchannels : priv->rxchannels;
  if (sample_bytes == 0 || sample_bytes > sizeof(sample))
    {
      ret = -EINVAL;
      goto out;
    }

  if (xfer->direction == BK7258_I2S_TX)
    {
      if (apb->curbyte > apb->nbytes)
        {
          ret = -EINVAL;
          goto out;
        }

      data = &apb->samp[apb->curbyte];
      length = apb->nbytes - apb->curbyte;
    }
  else
    {
      data = apb->samp;
      length = apb->nmaxbytes;
    }

  if (length == 0 ||
      (length % ((uint32_t)sample_bytes * channels)) != 0)
    {
      ret = -EINVAL;
      goto out;
    }

  /* active is set before the worker enters this function.  Configuration
   * setters therefore reject changes until the transfer completes, while
   * the local snapshot lets producers enqueue behind this transfer instead
   * of blocking on every FIFO poll.
   */

  nxmutex_unlock(&priv->lock);
  locked = false;
  start = clock_systime_ticks();
  for (offset = 0; offset < length; offset += sample_bytes)
    {
      ret = bk7258_i2s_wait_ready(xfer->direction == BK7258_I2S_TX,
                                  start, xfer->timeout);
      if (ret < 0)
        {
          goto out;
        }

      if (xfer->direction == BK7258_I2S_TX)
        {
          sample = bk7258_i2s_unpack(&data[offset], priv->datawidth);
          if (bk_i2s_write_data(0, &sample, 1) != BK_OK)
            {
              ret = -EIO;
              goto out;
            }

          /* I2S_LRCOM_STORE_LRLR consumes one FIFO word per wire slot.
           * A NuttX mono buffer contains one sample per frame, so mirror it
           * into the second slot.  Stereo buffers already contain L/R in
           * sequence and need one write per sample.
           */

          if (channels == 1)
            {
              ret = bk7258_i2s_wait_ready(true, start, xfer->timeout);
              if (ret < 0)
                {
                  goto out;
                }

              if (bk_i2s_write_data(0, &sample, 1) != BK_OK)
                {
                  ret = -EIO;
                  goto out;
                }
            }
        }
      else
        {
          if (bk_i2s_read_data(&sample, 1) != BK_OK)
            {
              ret = -EIO;
              goto out;
            }

          bk7258_i2s_pack(&data[offset], priv->datawidth, sample);

          /* Keep the first (left) slot for a mono NuttX stream and consume
           * the second wire slot so the following sample begins on the next
           * frame boundary.
           */

          if (channels == 1)
            {
              ret = bk7258_i2s_wait_ready(false, start, xfer->timeout);
              if (ret < 0)
                {
                  goto out;
                }

              if (bk_i2s_read_data(&sample, 1) != BK_OK)
                {
                  ret = -EIO;
                  goto out;
                }
            }
        }
    }

  if (xfer->direction == BK7258_I2S_TX)
    {
      apb->curbyte = apb->nbytes;
    }
  else
    {
      apb->curbyte = 0;
      apb->nbytes = length;
    }

  ret = 0;

out:
  if (locked)
    {
      nxmutex_unlock(&priv->lock);
    }

  return ret;
}

static FAR void *bk7258_i2s_worker(FAR void *arg)
{
  FAR struct bk7258_i2s_priv_s *priv = arg;

  for (; ; )
    {
      FAR struct bk7258_i2s_xfer_s *xfer;
      int ret;

      (void)nxsem_wait_uninterruptible(&priv->queuesem);

      ret = nxmutex_lock(&priv->lock);
      if (ret < 0)
        {
          continue;
        }

      xfer = (FAR struct bk7258_i2s_xfer_s *)sq_remfirst(&priv->queue);
      if (xfer != NULL && priv->queued > 0)
        {
          priv->queued--;
          priv->active = true;
        }

      nxmutex_unlock(&priv->lock);
      if (xfer == NULL)
        {
          continue;
        }

      ret = bk7258_i2s_process(priv, xfer);

      if (nxmutex_lock(&priv->lock) >= 0)
        {
          priv->active = false;
          nxmutex_unlock(&priv->lock);
        }

      if (xfer->callback != NULL)
        {
          xfer->callback(&priv->dev, xfer->apb, xfer->arg, ret);
        }

      apb_free(xfer->apb);
      kmm_free(xfer);
    }

  return NULL;
}

static int bk7258_i2s_enqueue(FAR struct bk7258_i2s_priv_s *priv,
                              FAR struct ap_buffer_s *apb,
                              i2s_callback_t callback, FAR void *arg,
                              uint32_t timeout,
                              enum bk7258_i2s_direction_e direction)
{
  FAR struct bk7258_i2s_xfer_s *xfer;
  int ret;

  if (apb == NULL || apb->samp == NULL)
    {
      return -EINVAL;
    }

  xfer = kmm_zalloc(sizeof(*xfer));
  if (xfer == NULL)
    {
      return -ENOMEM;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      kmm_free(xfer);
      return ret;
    }

  if (!priv->worker_ready)
    {
      ret = -ENODEV;
      goto out;
    }

  if (priv->queued >= CONFIG_BK7258_I2S_QUEUE_DEPTH)
    {
      ret = -EAGAIN;
      goto out;
    }

  apb_reference(apb);
  xfer->apb = apb;
  xfer->callback = callback;
  xfer->arg = arg;
  xfer->timeout = timeout;
  xfer->direction = direction;
  sq_addlast(&xfer->entry, &priv->queue);
  priv->queued++;
  nxsem_post(&priv->queuesem);
  nxmutex_unlock(&priv->lock);
  return 0;

out:
  nxmutex_unlock(&priv->lock);
  kmm_free(xfer);
  return ret;
}

static int bk7258_i2s_channels(FAR struct i2s_dev_s *dev,
                               uint8_t channels, bool tx)
{
  FAR struct bk7258_i2s_priv_s *priv =
    (FAR struct bk7258_i2s_priv_s *)dev;
  int ret;

  if (channels != 1 && channels != 2)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->active || priv->queued != 0)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  if (tx)
    {
      priv->txchannels = channels;
    }
  else
    {
      priv->rxchannels = channels;
    }

  nxmutex_unlock(&priv->lock);
  return 0;
}

static int bk7258_i2s_rxchannels(FAR struct i2s_dev_s *dev,
                                 uint8_t channels)
{
  return bk7258_i2s_channels(dev, channels, false);
}

static int bk7258_i2s_txchannels(FAR struct i2s_dev_s *dev,
                                 uint8_t channels)
{
  return bk7258_i2s_channels(dev, channels, true);
}

static uint32_t bk7258_i2s_rxsamplerate(FAR struct i2s_dev_s *dev,
                                        uint32_t rate)
{
  FAR struct bk7258_i2s_priv_s *priv =
    (FAR struct bk7258_i2s_priv_s *)dev;
  i2s_samp_rate_t value;
  int ret;

  if (bk7258_i2s_rate_to_enum(rate, &value) < 0)
    {
      return 0;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return 0;
    }

  if (priv->active || priv->queued != 0)
    {
      nxmutex_unlock(&priv->lock);
      return 0;
    }

  if (priv->inited && bk_i2s_set_samp_rate(value) != BK_OK)
    {
      nxmutex_unlock(&priv->lock);
      return 0;
    }

  priv->samplerate = rate;
  nxmutex_unlock(&priv->lock);
  return rate;
}

static uint32_t bk7258_i2s_txsamplerate(FAR struct i2s_dev_s *dev,
                                        uint32_t rate)
{
  return bk7258_i2s_rxsamplerate(dev, rate);
}

static uint32_t bk7258_i2s_rxdatawidth(FAR struct i2s_dev_s *dev, int bits)
{
  FAR struct bk7258_i2s_priv_s *priv =
    (FAR struct bk7258_i2s_priv_s *)dev;
  int ret;

  if (bits < 8 || bits > 32)
    {
      return 0;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return 0;
    }

  if (priv->active || priv->queued != 0)
    {
      nxmutex_unlock(&priv->lock);
      return 0;
    }

  if (priv->inited && bk_i2s_set_data_len((uint32_t)bits) != BK_OK)
    {
      nxmutex_unlock(&priv->lock);
      return 0;
    }

  priv->datawidth = bits;
  nxmutex_unlock(&priv->lock);
  return bits;
}

static uint32_t bk7258_i2s_txdatawidth(FAR struct i2s_dev_s *dev, int bits)
{
  return bk7258_i2s_rxdatawidth(dev, bits);
}

static int bk7258_i2s_receive(FAR struct i2s_dev_s *dev,
                              FAR struct ap_buffer_s *apb,
                              i2s_callback_t callback, FAR void *arg,
                              uint32_t timeout)
{
  return bk7258_i2s_enqueue((FAR struct bk7258_i2s_priv_s *)dev, apb,
                            callback, arg, timeout, BK7258_I2S_RX);
}

static int bk7258_i2s_send(FAR struct i2s_dev_s *dev,
                           FAR struct ap_buffer_s *apb,
                           i2s_callback_t callback, FAR void *arg,
                           uint32_t timeout)
{
  return bk7258_i2s_enqueue((FAR struct bk7258_i2s_priv_s *)dev, apb,
                            callback, arg, timeout, BK7258_I2S_TX);
}

static int bk7258_i2s_ioctl(FAR struct i2s_dev_s *dev, int cmd,
                            unsigned long arg)
{
  (void)dev;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

FAR struct i2s_dev_s *bk7258_i2s_initialize(void)
{
  FAR struct bk7258_i2s_priv_s *priv = &g_bk7258_i2s;
  pthread_attr_t attr;
  struct sched_param param;
  cpu_set_t cpuset;
  bool attr_ready = false;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return NULL;
    }

  if (priv->worker_ready)
    {
      nxmutex_unlock(&priv->lock);
      return &priv->dev;
    }

  if (!priv->queue_ready)
    {
      sq_init(&priv->queue);
      ret = nxsem_init(&priv->queuesem, 0, 0);
      if (ret < 0)
        {
          nxmutex_unlock(&priv->lock);
          return NULL;
        }

      priv->queue_ready = true;
    }

  CPU_ZERO(&cpuset);
  CPU_SET(0, &cpuset);
  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      attr_ready = true;
      ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(&attr,
                                      CONFIG_BK7258_I2S_WORKER_STACKSIZE);
    }

#ifdef CONFIG_SMP
  if (ret == 0)
    {
      ret = pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
    }
#endif

  if (ret == 0)
    {
      ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    }

  if (ret == 0)
    {
      memset(&param, 0, sizeof(param));
      param.sched_priority = CONFIG_BK7258_I2S_WORKER_PRIORITY;
      ret = pthread_attr_setschedparam(&attr, &param);
    }

  if (ret == 0)
    {
      ret = pthread_create(&priv->worker, &attr, bk7258_i2s_worker, priv);
    }

  if (attr_ready)
    {
      (void)pthread_attr_destroy(&attr);
    }

  if (ret != 0)
    {
      (void)nxsem_destroy(&priv->queuesem);
      priv->queue_ready = false;
      nxmutex_unlock(&priv->lock);
      return NULL;
    }

  (void)pthread_setname_np(priv->worker, "bk-i2s");
  priv->worker_ready = true;
  nxmutex_unlock(&priv->lock);
  return &priv->dev;
}

#endif /* CONFIG_BK7258_I2S */
