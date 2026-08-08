/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/bk7258_aud.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) Audio front-end (AUD) — NuttX audio_lowerhalf_s wrapper.
 *
 * Wraps the Beken bk_aud_* SDK (DAC playback + ADC capture) as a NuttX
 * audio lower half.  The 54 bk_aud_* symbols live exclusively in the AP
 * libdriver.a, so this driver is AP-only.
 *
 * Architecture (modelled on drivers/audio/audio_null.c):
 *   - Full audio_ops_s vtable; allocbuffer/freebuffer left NULL for the
 *     upper half's default buffer pool.
 *   - enqueuebuffer() posts AUDIO_MSG_ENQUEUE on a per-device mqueue; a
 *     worker thread drains it and transfers PCM to/from the AUD FIFO, then
 *     reports completion via dev->upper(AUDIO_CALLBACK_COMPLETE).
 *   - Playback: bk_aud_dac_get_fifo_addr() -> write 32-bit samples.
 *   - Capture:  bk_aud_adc_get_fifo_data() -> read one sample per call.
 *
 * The SDK FIFO ready conditions are polled by the worker; this is a
 * synchronous-in-thread transfer (no ISR-driven DMA).  Sample rate and
 * channel follow configure().
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AUD

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/audio/audio.h>
#include <nuttx/fs/fs.h>
#include <nuttx/irq.h>
#include <nuttx/mqueue.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>

#include <fcntl.h>
#include <pthread.h>

#include <arch/chip/bk7258_aud.h>

/* aud.h is intentionally NOT included: it pulls aud_types.h which clashes
 * with aud_dac_types.h / aud_adc_types.h (duplicate aud_isr_id_t etc).
 * The common driver_init/deinit live in aud_common.h, and the DAC/ADC
 * entry points in aud_dac.h / aud_adc.h — these three coexist cleanly.
 */

#include <driver/aud_common.h>
#include <driver/aud_dac.h>
#include <driver/aud_adc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_AUD_DEVNAME
#  define CONFIG_BK7258_AUD_DEVNAME    "audio0"
#endif

/* Worker thread stack / priority. */

#define BK7258_AUD_WORKER_PRIO         SCHED_PRIORITY_DEFAULT
#define BK7258_AUD_WORKER_STACK        2048

/* AUDIO message queue name (per /dev/audioN minor). */

#define BK7258_AUD_MQNAME              "/bk7258_aud_mq"

/* FIFO ready retry budget. */

#define BK7258_AUD_FIFO_RETRIES        1000u
#define BK7258_AUD_FIFO_POLL_US        100u

/* The board wrapper exposes mono, signed 16-bit PCM. */

#define BK7258_AUD_SAMPLE_BYTES        2u

/* Message queue depth (also the max number of concurrently cancelled
 * buffers we need to track).
 */

#define BK7258_AUD_MQ_DEPTH            16u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_aud_priv_s
{
  struct audio_lowerhalf_s dev;     /* NuttX audio lower-half anchor */
  struct file mq;                   /* audio message queue */
  char mqname[16];                  /* mq name storage */
  pthread_t threadid;               /* worker thread */
  bool running;                     /* worker thread running */
  bool started;                     /* audio started */
  bool dac_inited;                  /* bk_aud_dac_init() done */
  bool adc_inited;                  /* bk_aud_adc_init() done */
  bool capture;                     /* current direction is input */
  bool configured;                  /* PCM parameters validated */
  bool registered;                  /* audio_register() completed */
  bool reserved;                    /* single hardware session owned */
  uint32_t samplerate;              /* configured sample rate (Hz) */
  uint8_t channels;                 /* configured channel count */

  /* Cancelled-buffer set.  The POSIX mqueue used for audio_msg_s cannot
   * remove a specific message, so a buffer that was cancelled (via
   * cancelbuffer) while still queued is recorded here; the worker checks
   * this set before touching the buffer and reports it as dequeued.
   */

  FAR struct ap_buffer_s *cancelled[BK7258_AUD_MQ_DEPTH];
  uint8_t ncancelled;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_aud_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                              FAR struct audio_caps_s *caps);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_configure(FAR struct audio_lowerhalf_s *dev,
                                FAR void *session,
                                FAR const struct audio_caps_s *caps);
static int bk7258_aud_start(FAR struct audio_lowerhalf_s *dev,
                            FAR void *session);
static int bk7258_aud_stop(FAR struct audio_lowerhalf_s *dev,
                           FAR void *session);
static int bk7258_aud_pause(FAR struct audio_lowerhalf_s *dev,
                            FAR void *session);
static int bk7258_aud_resume(FAR struct audio_lowerhalf_s *dev,
                             FAR void *session);
static int bk7258_aud_reserve(FAR struct audio_lowerhalf_s *dev,
                              FAR void **session);
static int bk7258_aud_release(FAR struct audio_lowerhalf_s *dev,
                              FAR void *session);
#else
static int bk7258_aud_configure(FAR struct audio_lowerhalf_s *dev,
                                FAR const struct audio_caps_s *caps);
static int bk7258_aud_start(FAR struct audio_lowerhalf_s *dev);
static int bk7258_aud_stop(FAR struct audio_lowerhalf_s *dev);
static int bk7258_aud_pause(FAR struct audio_lowerhalf_s *dev);
static int bk7258_aud_resume(FAR struct audio_lowerhalf_s *dev);
static int bk7258_aud_reserve(FAR struct audio_lowerhalf_s *dev);
static int bk7258_aud_release(FAR struct audio_lowerhalf_s *dev);
#endif
static int bk7258_aud_shutdown(FAR struct audio_lowerhalf_s *dev);
static int bk7258_aud_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                    FAR struct ap_buffer_s *apb);
static int bk7258_aud_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                                   FAR struct ap_buffer_s *apb);
static int bk7258_aud_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                            unsigned long arg);

static FAR void *bk7258_aud_worker(FAR void *arg);
static int bk7258_aud_ensure_init(FAR struct bk7258_aud_priv_s *priv);
static int bk7258_aud_start_worker(FAR struct bk7258_aud_priv_s *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct audio_ops_s g_bk7258_aud_ops =
{
  .getcaps        = bk7258_aud_getcaps,
  .configure      = bk7258_aud_configure,
  .shutdown       = bk7258_aud_shutdown,
  .start          = bk7258_aud_start,
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  .stop           = bk7258_aud_stop,
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
  .pause          = bk7258_aud_pause,
  .resume         = bk7258_aud_resume,
#endif
  .allocbuffer    = NULL,           /* upper half default pool */
  .freebuffer     = NULL,
  .enqueuebuffer  = bk7258_aud_enqueuebuffer,
  .cancelbuffer   = bk7258_aud_cancelbuffer,
  .ioctl          = bk7258_aud_ioctl,
  .read           = NULL,
  .write          = NULL,
  .reserve        = bk7258_aud_reserve,
  .release        = bk7258_aud_release,
};

static struct bk7258_aud_priv_s g_bk7258_aud =
{
  .dev.ops      = &g_bk7258_aud_ops,
  .mqname       = { 0 },
  .threadid     = 0,
  .running      = false,
  .started      = false,
  .dac_inited   = false,
  .adc_inited   = false,
  .capture      = false,
  .configured   = false,
  .registered   = false,
  .reserved     = false,
  .samplerate   = 16000,
  .channels     = 1,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_aud_ensure_init
 *
 * Bring up the AUD block lazily: driver_init + dac_init + adc_init, and
 * cache the DAC FIFO register address.
 ****************************************************************************/

static int bk7258_aud_ensure_init(FAR struct bk7258_aud_priv_s *priv)
{
  aud_dac_config_t dac_cfg = DEFAULT_AUD_DAC_CONFIG();
  aud_adc_config_t adc_cfg = DEFAULT_AUD_ADC_CONFIG();
  bk_err_t ret;

  if ((priv->capture && priv->adc_inited) ||
      (!priv->capture && priv->dac_inited))
    {
      return OK;
    }

  ret = bk_aud_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  if (priv->capture)
    {
      ret = bk_aud_adc_init(&adc_cfg);
      if (ret != BK_OK)
        {
          return -EIO;
        }

      ret = bk_aud_adc_set_samp_rate(priv->samplerate);
      if (ret != BK_OK)
        {
          bk_aud_adc_deinit();
          return -ERANGE;
        }

      priv->adc_inited = true;
    }
  else
    {
      ret = bk_aud_dac_init(&dac_cfg);
      if (ret != BK_OK)
        {
          return -EIO;
        }

      ret = bk_aud_dac_set_samp_rate(priv->samplerate);
      if (ret != BK_OK)
        {
          bk_aud_dac_deinit();
          return -ERANGE;
        }

      priv->dac_inited = true;
    }

  return OK;
}

static int bk7258_aud_start_worker(FAR struct bk7258_aud_priv_s *priv)
{
  struct mq_attr attr;
  pthread_attr_t tattr;
  int ret;

  if (priv->running && priv->threadid != 0)
    {
      return OK;
    }

  snprintf(priv->mqname, sizeof(priv->mqname), "%s", BK7258_AUD_MQNAME);
  file_mq_unlink(priv->mqname);

  attr.mq_maxmsg  = BK7258_AUD_MQ_DEPTH;
  attr.mq_msgsize = sizeof(struct audio_msg_s);
  attr.mq_flags   = 0;

  ret = file_mq_open(&priv->mq, priv->mqname, O_RDWR | O_CREAT, 0644,
                     &attr);
  if (ret < 0)
    {
      priv->mqname[0] = '\0';
      return ret;
    }

  priv->running = true;
  ret = pthread_attr_init(&tattr);
  if (ret != OK)
    {
      goto errout_queue;
    }

  ret = pthread_attr_setstacksize(&tattr, BK7258_AUD_WORKER_STACK);
  if (ret != OK)
    {
      pthread_attr_destroy(&tattr);
      goto errout_queue;
    }

  ret = pthread_create(&priv->threadid, &tattr, bk7258_aud_worker, priv);
  pthread_attr_destroy(&tattr);
  if (ret != OK)
    {
      goto errout_queue;
    }

  return OK;

errout_queue:
  file_mq_close(&priv->mq);
  file_mq_unlink(priv->mqname);
  priv->mqname[0] = '\0';
  priv->running = false;
  priv->threadid = 0;
  return ret > 0 ? -ret : ret;
}

/****************************************************************************
 * Name: bk7258_aud_getcaps
 ****************************************************************************/

static int bk7258_aud_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                              FAR struct audio_caps_s *caps)
{
  if (caps == NULL || caps->ac_len < sizeof(*caps))
    {
      return -EINVAL;
    }

  caps->ac_channels = 1;
  caps->ac_chmap = 0;
  caps->ac_format.hw = 0;
  caps->ac_controls.w = 0;

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_QUERY:
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.b[0] = AUDIO_TYPE_OUTPUT | AUDIO_TYPE_INPUT;
            caps->ac_format.hw = 1 << (AUDIO_FMT_PCM - 1);
          }
        else
          {
            caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
          }
        break;

      case AUDIO_TYPE_OUTPUT:
      case AUDIO_TYPE_INPUT:
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.hw[0] = AUDIO_SAMP_RATE_8K |
                                      AUDIO_SAMP_RATE_12K |
                                      AUDIO_SAMP_RATE_16K |
                                      AUDIO_SAMP_RATE_22K |
                                      AUDIO_SAMP_RATE_24K |
                                      AUDIO_SAMP_RATE_32K |
                                      AUDIO_SAMP_RATE_44K |
                                      AUDIO_SAMP_RATE_48K;
          }
        break;

      default:
        return -ENOTTY;
    }

  (void)dev;
  (void)type;
  return caps->ac_len;
}

/****************************************************************************
 * Name: bk7258_aud_configure
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_configure(FAR struct audio_lowerhalf_s *dev,
                                FAR void *session,
                                FAR const struct audio_caps_s *caps)
#else
static int bk7258_aud_configure(FAR struct audio_lowerhalf_s *dev,
                                FAR const struct audio_caps_s *caps)
#endif
{
  FAR struct bk7258_aud_priv_s *priv =
    (FAR struct bk7258_aud_priv_s *)dev;

  if (caps == NULL)
    {
      return -EINVAL;
    }

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (session != priv)
    {
      return -EINVAL;
    }
#endif

  if (priv->started)
    {
      return -EBUSY;
    }

  if (caps->ac_type != AUDIO_TYPE_OUTPUT &&
      caps->ac_type != AUDIO_TYPE_INPUT)
    {
      return -ENOTTY;
    }

  if (caps->ac_channels != 1 || caps->ac_controls.b[2] != 16)
    {
      return -ERANGE;
    }

  switch (caps->ac_controls.hw[0])
    {
      case 8000:
      case 12000:
      case 16000:
      case 22050:
      case 24000:
      case 32000:
      case 44100:
      case 48000:
        break;

      default:
        return -ERANGE;
    }

  priv->capture = caps->ac_type == AUDIO_TYPE_INPUT;
  priv->channels = 1;
  priv->samplerate = caps->ac_controls.hw[0];
  priv->configured = true;

  return OK;
}

/****************************************************************************
 * Name: bk7258_aud_start
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_start(FAR struct audio_lowerhalf_s *dev,
                            FAR void *session)
#else
static int bk7258_aud_start(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct bk7258_aud_priv_s *priv =
    (FAR struct bk7258_aud_priv_s *)dev;
  int ret;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (session != priv)
    {
      return -EINVAL;
    }
#endif

  if (priv->started)
    {
      return -EBUSY;
    }

  if (!priv->configured)
    {
      return -EAGAIN;
    }

  ret = bk7258_aud_start_worker(priv);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_aud_ensure_init(priv);
  if (ret < 0)
    {
      return ret;
    }

  ret = priv->capture ? bk_aud_adc_start() : bk_aud_dac_start();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  priv->started = true;
  return OK;
}

/****************************************************************************
 * Name: bk7258_aud_stop
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_stop(FAR struct audio_lowerhalf_s *dev,
                           FAR void *session)
#else
static int bk7258_aud_stop(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct bk7258_aud_priv_s *priv =
    (FAR struct bk7258_aud_priv_s *)dev;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (session != priv)
    {
      return -EINVAL;
    }
#endif

  if (priv->capture)
    {
      (void)bk_aud_adc_stop();
    }
  else
    {
      (void)bk_aud_dac_stop();
    }
  priv->started = false;
  return OK;
}

/****************************************************************************
 * Name: bk7258_aud_pause / resume
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_pause(FAR struct audio_lowerhalf_s *dev,
                            FAR void *session)
#else
static int bk7258_aud_pause(FAR struct audio_lowerhalf_s *dev)
#endif
{
#ifdef CONFIG_AUDIO_MULTI_SESSION
  return bk7258_aud_stop(dev, session);
#else
  return bk7258_aud_stop(dev);
#endif
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_resume(FAR struct audio_lowerhalf_s *dev,
                             FAR void *session)
#else
static int bk7258_aud_resume(FAR struct audio_lowerhalf_s *dev)
#endif
{
#ifdef CONFIG_AUDIO_MULTI_SESSION
  return bk7258_aud_start(dev, session);
#else
  return bk7258_aud_start(dev);
#endif
}
#endif

/****************************************************************************
 * Name: bk7258_aud_enqueuebuffer
 *
 * Post the buffer to the worker via the message queue.
 ****************************************************************************/

static int bk7258_aud_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                    FAR struct ap_buffer_s *apb)
{
  FAR struct bk7258_aud_priv_s *priv =
    (FAR struct bk7258_aud_priv_s *)dev;
  struct audio_msg_s msg;

  if (apb == NULL || apb->samp == NULL || priv->mqname[0] == '\0' ||
      !priv->running || !priv->reserved ||
      apb->curbyte > apb->nbytes ||
      ((apb->nbytes - apb->curbyte) & 1u) != 0)
    {
      return -EINVAL;
    }

  memset(&msg, 0, sizeof(msg));
  msg.msg_id = AUDIO_MSG_ENQUEUE;
  msg.u.ptr = apb;

  return file_mq_send(&priv->mq, (FAR const char *)&msg, sizeof(msg), 1);
}

/****************************************************************************
 * Name: bk7258_aud_cancelbuffer
 ****************************************************************************/

static int bk7258_aud_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                                   FAR struct ap_buffer_s *apb)
{
  FAR struct bk7258_aud_priv_s *priv =
    (FAR struct bk7258_aud_priv_s *)dev;
  irqstate_t flags;
  uint8_t i;
  int ret = OK;

  if (apb == NULL)
    {
      return -EINVAL;
    }

  /* The POSIX mqueue cannot remove a specific queued message, so record
   * the buffer in the cancelled set instead.  The worker checks this set
   * before touching the buffer and reports it as dequeued without ever
   * accessing the (possibly released) buffer contents.
   */

  flags = enter_critical_section();

  for (i = 0; i < priv->ncancelled; i++)
    {
      if (priv->cancelled[i] == apb)
        {
          leave_critical_section(flags);
          return OK;
        }
    }

  if (priv->ncancelled < BK7258_AUD_MQ_DEPTH)
    {
      priv->cancelled[priv->ncancelled++] = apb;
    }
  else
    {
      ret = -ENOMEM;
    }

  leave_critical_section(flags);
  return ret;
}

/****************************************************************************
 * Name: bk7258_aud_ioctl
 ****************************************************************************/

static int bk7258_aud_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                            unsigned long arg)
{
  switch (cmd)
    {
      case AUDIOIOC_HWRESET:
        return OK;

      default:
        return -ENOTTY;
    }
}

/****************************************************************************
 * Name: bk7258_aud_reserve / release
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_reserve(FAR struct audio_lowerhalf_s *dev,
                              FAR void **session)
#else
static int bk7258_aud_reserve(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct bk7258_aud_priv_s *priv =
    (FAR struct bk7258_aud_priv_s *)dev;
  irqstate_t flags;
  int ret;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (session == NULL)
    {
      return -EINVAL;
    }

  *session = NULL;
#endif

  flags = enter_critical_section();
  if (priv->reserved)
    {
      leave_critical_section(flags);
      return -EBUSY;
    }

  priv->reserved = true;
  leave_critical_section(flags);

  ret = bk7258_aud_start_worker(priv);
  if (ret < 0)
    {
      flags = enter_critical_section();
      priv->reserved = false;
      leave_critical_section(flags);
      return ret;
    }

#ifdef CONFIG_AUDIO_MULTI_SESSION
  *session = priv;
#endif

  return OK;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_release(FAR struct audio_lowerhalf_s *dev,
                              FAR void *session)
#else
static int bk7258_aud_release(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct bk7258_aud_priv_s *priv =
    (FAR struct bk7258_aud_priv_s *)dev;
  irqstate_t flags;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (session != priv)
    {
      return -EINVAL;
    }
#endif

  if (priv->threadid != 0 || priv->dac_inited || priv->adc_inited)
    {
      (void)bk7258_aud_shutdown(dev);
    }

  flags = enter_critical_section();
  priv->reserved = false;
  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Name: bk7258_aud_is_cancelled
 *
 * Check whether apb was cancelled (see cancelbuffer).  Called by the
 * worker before touching a buffer that may have been released.
 ****************************************************************************/

static bool bk7258_aud_is_cancelled(FAR struct bk7258_aud_priv_s *priv,
                                    FAR struct ap_buffer_s *apb)
{
  uint8_t i;

  for (i = 0; i < priv->ncancelled; i++)
    {
      if (priv->cancelled[i] == apb)
        {
          return true;
        }
    }

  return false;
}

static int bk7258_aud_wait_dac_space(void)
{
  uint32_t status;
  unsigned int retry;

  for (retry = 0; retry < BK7258_AUD_FIFO_RETRIES; retry++)
    {
      if (bk_aud_dac_get_status(&status) != BK_OK)
        {
          return -EIO;
        }

      if ((status & AUD_DACL_FIFO_FULL_MASK) == 0)
        {
          return OK;
        }

      nxsig_usleep(BK7258_AUD_FIFO_POLL_US);
    }

  return -ETIMEDOUT;
}

static int bk7258_aud_wait_adc_data(void)
{
  uint32_t status;
  unsigned int retry;

  for (retry = 0; retry < BK7258_AUD_FIFO_RETRIES; retry++)
    {
      if (bk_aud_adc_get_status(&status) != BK_OK)
        {
          return -EIO;
        }

      if ((status & AUD_ADCL_FIFO_EMPTY_MASK) == 0)
        {
          return OK;
        }

      nxsig_usleep(BK7258_AUD_FIFO_POLL_US);
    }

  return -ETIMEDOUT;
}

/****************************************************************************
 * Name: bk7258_aud_worker
 *
 * Drain the message queue.  For AUDIO_MSG_ENQUEUE the buffer is played or
 * captured to/from the AUD FIFO, then completion is reported.
 ****************************************************************************/

static FAR void *bk7258_aud_worker(FAR void *arg)
{
  FAR struct bk7258_aud_priv_s *priv =
    (FAR struct bk7258_aud_priv_s *)arg;
  struct audio_msg_s msg;
  irqstate_t flags;
  bool cancelled;

  while (priv->running)
    {
      ssize_t nbytes;

      nbytes = file_mq_receive(&priv->mq, (FAR char *)&msg, sizeof(msg),
                               NULL);
      if (nbytes < 0)
        {
          continue;
        }

      if (msg.msg_id == AUDIO_MSG_ENQUEUE)
        {
          FAR struct ap_buffer_s *apb = msg.u.ptr;
          FAR int16_t *samp;
          uint32_t nsamples;
          uint32_t i;
          uint32_t fifo;
          bool final;
          int result = OK;

          /* If this buffer was cancelled while queued, do not touch its
           * contents (the upper half may have released it); report it as
           * dequeued instead.
           */

          flags = enter_critical_section();
          cancelled = bk7258_aud_is_cancelled(priv, apb);

          if (apb == NULL || cancelled)
            {
              if (apb != NULL)
                {
                  uint8_t ci;

                  for (ci = 0; ci < priv->ncancelled; ci++)
                    {
                      if (priv->cancelled[ci] == apb)
                        {
                          priv->cancelled[ci] =
                            priv->cancelled[priv->ncancelled - 1];
                          priv->ncancelled--;
                          break;
                        }
                    }
                }

              leave_critical_section(flags);

#ifdef CONFIG_AUDIO_MULTI_SESSION
              priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE,
                              apb, OK, priv);
#else
              priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE,
                              apb, OK);
#endif

              continue;
            }

          leave_critical_section(flags);

          final = (apb->flags & AUDIO_APB_FINAL) != 0;

          if (priv->capture)
            {
              if (apb->curbyte > apb->nmaxbytes)
                {
                  result = -EINVAL;
                  nsamples = 0;
                  samp = NULL;
                }
              else
                {
                  samp = (FAR int16_t *)(apb->samp + apb->curbyte);
                  nsamples = (apb->nmaxbytes - apb->curbyte) /
                             BK7258_AUD_SAMPLE_BYTES;
                }
            }
          else if (apb->curbyte > apb->nbytes)
            {
              result = -EINVAL;
              nsamples = 0;
              samp = NULL;
            }
          else
            {
              samp = (FAR int16_t *)(apb->samp + apb->curbyte);
              nsamples = (apb->nbytes - apb->curbyte) /
                         BK7258_AUD_SAMPLE_BYTES;
            }

          if (result == OK && priv->started && samp != NULL)
            {
              for (i = 0; i < nsamples; i++)
                {
                  if (priv->capture)
                    {
                      result = bk7258_aud_wait_adc_data();
                      if (result < 0 ||
                          bk_aud_adc_get_fifo_data(&fifo) != BK_OK)
                        {
                          result = result < 0 ? result : -EIO;
                          break;
                        }

                      samp[i] = (int16_t)(fifo & 0xffffu);
                    }
                  else
                    {
                      result = bk7258_aud_wait_dac_space();
                      if (result < 0 ||
                          bk_aud_dac_write((uint16_t)samp[i]) != BK_OK)
                        {
                          result = result < 0 ? result : -EIO;
                          break;
                        }
                    }
                }

              apb->curbyte += i * BK7258_AUD_SAMPLE_BYTES;
              if (priv->capture)
                {
                  apb->nbytes = apb->curbyte;
                }
            }
          else if (result == OK)
            {
              result = -EAGAIN;
            }

          /* Ownership of every processed buffer returns with DEQUEUE.
           * COMPLETE denotes the end of a stream, not one buffer.
           */

#ifdef CONFIG_AUDIO_MULTI_SESSION
          priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE,
                          apb, result, priv);
#else
          priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE,
                          apb, result);
#endif

          if (final)
            {
#ifdef CONFIG_AUDIO_MULTI_SESSION
              priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE,
                              NULL, result, priv);
#else
              priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE,
                              NULL, result);
#endif
            }
        }
      else if (msg.msg_id == AUDIO_MSG_STOP)
        {
          priv->running = false;
          break;
        }
    }

  /* The worker owns the message queue teardown: it exits only after
   * receiving AUDIO_MSG_STOP (or priv->running is cleared), at which
   * point nobody else is using the queue, so it is safe to close here.
   */

  file_mq_close(&priv->mq);
  file_mq_unlink(priv->mqname);
  priv->mqname[0] = '\0';

  return NULL;
}

/****************************************************************************
 * Name: bk7258_aud_shutdown
 *
 * Stop the worker thread, tear down the mqueue and deinit the hardware.
 ****************************************************************************/

static int bk7258_aud_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct bk7258_aud_priv_s *priv =
    (FAR struct bk7258_aud_priv_s *)dev;
  struct audio_msg_s term_msg;

  /* Correct worker shutdown order:
   *   1. post AUDIO_MSG_STOP so a worker blocked in file_mq_receive()
   *      wakes up, sets running=false and breaks out of its loop;
   *   2. pthread_join() until the worker has exited AND closed/unlinked
   *      the message queue itself (the worker owns the queue teardown,
   *      mirroring NuttX audio_null);
   *   3. only then deinit the hardware.
   */

  if (priv->threadid != 0)
    {
      if (priv->mqname[0] != '\0')
        {
          memset(&term_msg, 0, sizeof(term_msg));
          term_msg.msg_id = AUDIO_MSG_STOP;
          term_msg.u.ptr = NULL;
          (void)file_mq_send(&priv->mq, (FAR const char *)&term_msg,
                             sizeof(term_msg), 1);
        }

      pthread_join(priv->threadid, NULL);
      priv->threadid = 0;
    }

  if (priv->dac_inited)
    {
      bk_aud_dac_deinit();
      priv->dac_inited = false;
    }

  if (priv->adc_inited)
    {
      bk_aud_adc_deinit();
      priv->adc_inited = false;
    }

  priv->ncancelled = 0;
  bk_aud_driver_deinit();
  priv->running = false;
  priv->started = false;
  priv->configured = false;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_aud_initialize(void)
{
  FAR struct bk7258_aud_priv_s *priv = &g_bk7258_aud;
  int ret;

  if (priv->registered)
    {
      return OK;
    }

  ret = audio_register(CONFIG_BK7258_AUD_DEVNAME, &priv->dev);
  if (ret < 0)
    {
      return ret;
    }

  priv->registered = true;
  return OK;
}

#endif /* CONFIG_BK7258_AUD */
