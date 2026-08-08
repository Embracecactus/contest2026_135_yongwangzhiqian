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
#include <string.h>
#include <time.h>

#include <nuttx/audio/audio.h>
#include <nuttx/fs/fs.h>
#include <nuttx/irq.h>
#include <nuttx/mqueue.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

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

#define BK7258_AUD_FIFO_RETRIES        1000000u

/* 32-bit samples per PCM word. */

#define BK7258_AUD_SAMPLE_BYTES        4u

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
  uint32_t samplerate;              /* configured sample rate (Hz) */
  uint8_t channels;                 /* configured channel count */
  uint32_t *dac_fifo;               /* DAC FIFO register address */

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
static int bk7258_aud_configure(FAR struct audio_lowerhalf_s *dev,
                                FAR const struct audio_caps_s *caps);
static int bk7258_aud_shutdown(FAR struct audio_lowerhalf_s *dev);
static int bk7258_aud_start(FAR struct audio_lowerhalf_s *dev);
static int bk7258_aud_stop(FAR struct audio_lowerhalf_s *dev);
static int bk7258_aud_pause(FAR struct audio_lowerhalf_s *dev);
static int bk7258_aud_resume(FAR struct audio_lowerhalf_s *dev);
static int bk7258_aud_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                    FAR struct ap_buffer_s *apb);
static int bk7258_aud_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                                   FAR struct ap_buffer_s *apb);
static int bk7258_aud_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                            unsigned long arg);
static int bk7258_aud_reserve(FAR struct audio_lowerhalf_s *dev);
static int bk7258_aud_release(FAR struct audio_lowerhalf_s *dev);

static FAR void *bk7258_aud_worker(FAR void *arg);
static int bk7258_aud_ensure_init(FAR struct bk7258_aud_priv_s *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct audio_ops_s g_bk7258_aud_ops =
{
  .getcaps        = bk7258_aud_getcaps,
  .configure      = bk7258_aud_configure,
  .shutdown       = bk7258_aud_shutdown,
  .start          = bk7258_aud_start,
  .stop           = bk7258_aud_stop,
  .pause          = bk7258_aud_pause,
  .resume         = bk7258_aud_resume,
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
  .samplerate   = 16000,
  .channels     = 1,
  .dac_fifo     = NULL,
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

  if (priv->dac_inited && priv->adc_inited)
    {
      return OK;
    }

  ret = bk_aud_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  if (!priv->dac_inited)
    {
      ret = bk_aud_dac_init(&dac_cfg);
      if (ret != BK_OK)
        {
          return -EIO;
        }

      (void)bk_aud_dac_set_samp_rate(priv->samplerate);
      priv->dac_inited = true;
    }

  if (!priv->adc_inited)
    {
      ret = bk_aud_adc_init(&adc_cfg);
      if (ret != BK_OK)
        {
          return -EIO;
        }

      (void)bk_aud_adc_set_samp_rate(priv->samplerate);
      priv->adc_inited = true;
    }

  (void)bk_aud_dac_get_fifo_addr((uint32_t *)&priv->dac_fifo);
  return OK;
}

/****************************************************************************
 * Name: bk7258_aud_getcaps
 ****************************************************************************/

static int bk7258_aud_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                              FAR struct audio_caps_s *caps)
{
  if (caps == NULL)
    {
      return -EINVAL;
    }

  caps->ac_len = sizeof(*caps);
  caps->ac_type = type;
  caps->ac_channels = 1 << 4 | 1;      /* 1..1 channel */
  caps->ac_chmap = 0;
  caps->ac_format.hw = 0;

  switch (type)
    {
      case AUDIO_TYPE_QUERY:
        caps->ac_controls.w = AUDIO_TYPE_OUTPUT | AUDIO_TYPE_INPUT;
        break;

      case AUDIO_TYPE_OUTPUT:
      case AUDIO_TYPE_INPUT:
        caps->ac_format.hw = AUDIO_FMT_PCM;
        caps->ac_controls.b[0] = 16;    /* 16-bit */
        break;

      default:
        return -ENOTTY;
    }

  return OK;
}

/****************************************************************************
 * Name: bk7258_aud_configure
 ****************************************************************************/

static int bk7258_aud_configure(FAR struct audio_lowerhalf_s *dev,
                                FAR const struct audio_caps_s *caps)
{
  FAR struct bk7258_aud_priv_s *priv =
    (FAR struct bk7258_aud_priv_s *)dev;

  if (caps == NULL)
    {
      return -EINVAL;
    }

  if (caps->ac_channels > 0)
    {
      priv->channels = caps->ac_channels & 0x0f;
    }

  if (caps->ac_controls.hw[0] > 0)
    {
      priv->samplerate = caps->ac_controls.hw[0];
    }

  return OK;
}

/****************************************************************************
 * Name: bk7258_aud_start
 ****************************************************************************/

static int bk7258_aud_start(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct bk7258_aud_priv_s *priv =
    (FAR struct bk7258_aud_priv_s *)dev;
  int ret;

  ret = bk7258_aud_ensure_init(priv);
  if (ret < 0)
    {
      return ret;
    }

  (void)bk_aud_dac_start();
  (void)bk_aud_adc_start();
  priv->started = true;
  return OK;
}

/****************************************************************************
 * Name: bk7258_aud_stop
 ****************************************************************************/

static int bk7258_aud_stop(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct bk7258_aud_priv_s *priv =
    (FAR struct bk7258_aud_priv_s *)dev;

  (void)bk_aud_dac_stop();
  (void)bk_aud_adc_stop();
  priv->started = false;
  return OK;
}

/****************************************************************************
 * Name: bk7258_aud_pause / resume
 ****************************************************************************/

static int bk7258_aud_pause(FAR struct audio_lowerhalf_s *dev)
{
  return bk7258_aud_stop(dev);
}

static int bk7258_aud_resume(FAR struct audio_lowerhalf_s *dev)
{
  return bk7258_aud_start(dev);
}

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

  if (apb == NULL || priv->mqname[0] == '\0')
    {
      return -EINVAL;
    }

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

  /* The POSIX mqueue cannot remove a specific queued message, so record
   * the buffer in the cancelled set instead.  The worker checks this set
   * before touching the buffer and reports it as dequeued without ever
   * accessing the (possibly released) buffer contents.
   */

  flags = enter_critical_section();

  if (apb != NULL && priv->ncancelled < BK7258_AUD_MQ_DEPTH)
    {
      priv->cancelled[priv->ncancelled] = apb;
      priv->ncancelled++;
    }

  leave_critical_section(flags);
  return OK;
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

static int bk7258_aud_reserve(FAR struct audio_lowerhalf_s *dev)
{
  return OK;
}

static int bk7258_aud_release(FAR struct audio_lowerhalf_s *dev)
{
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
          FAR uint32_t *samp;
          uint32_t nsamples;
          uint32_t i;
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
                              apb, OK, NULL);
#else
              priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE,
                              apb, OK);
#endif

              continue;
            }

          leave_critical_section(flags);

          samp = (FAR uint32_t *)apb->samp;
          nsamples = apb->nbytes / BK7258_AUD_SAMPLE_BYTES;

          if (priv->started && samp != NULL)
            {
              /* Playback: write each 32-bit sample via the SDK's
               * single-sample DAC write (the SDK manages the FIFO).
               */

              for (i = 0; i < nsamples; i++)
                {
                  if (bk_aud_dac_write(samp[i]) != BK_OK)
                    {
                      result = -EIO;
                      break;
                    }
                }
            }
          else
            {
              result = -EAGAIN;
            }

          /* Report completion to the upper half. */

#ifdef CONFIG_AUDIO_MULTI_SESSION
          priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE,
                          apb, result, NULL);
#else
          priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE,
                          apb, result);
#endif
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
          struct timespec ts;

          term_msg.msg_id = AUDIO_MSG_STOP;
          term_msg.u.ptr = NULL;

          /* Use a bounded send so a full queue cannot block shutdown
           * forever; the worker keeps draining and will reach the STOP.
           */

          clock_gettime(CLOCK_REALTIME, &ts);
          ts.tv_nsec += 100 * 1000 * 1000;   /* +100 ms */
          if (ts.tv_nsec >= 1000 * 1000 * 1000)
            {
              ts.tv_sec++;
              ts.tv_nsec -= 1000 * 1000 * 1000;
            }

          (void)file_mq_timedsend(&priv->mq,
                                  (FAR const char *)&term_msg,
                                  sizeof(term_msg), 1, &ts);
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
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_aud_initialize(void)
{
  FAR struct bk7258_aud_priv_s *priv = &g_bk7258_aud;
  struct mq_attr attr;
  pthread_attr_t tattr;
  int ret;

  if (priv->mqname[0] != '\0')
    {
      return OK;      /* already initialized */
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
      file_mq_close(&priv->mq);
      file_mq_unlink(priv->mqname);
      priv->mqname[0] = '\0';
      priv->running = false;
      return -ret;
    }

  ret = pthread_attr_setstacksize(&tattr, BK7258_AUD_WORKER_STACK);
  if (ret != OK)
    {
      pthread_attr_destroy(&tattr);
      file_mq_close(&priv->mq);
      file_mq_unlink(priv->mqname);
      priv->mqname[0] = '\0';
      priv->running = false;
      return -ret;
    }

  ret = pthread_create(&priv->threadid, &tattr, bk7258_aud_worker, priv);
  pthread_attr_destroy(&tattr);
  if (ret != OK)
    {
      file_mq_close(&priv->mq);
      file_mq_unlink(priv->mqname);
      priv->mqname[0] = '\0';
      priv->running = false;
      return -ret;
    }

  ret = audio_register(CONFIG_BK7258_AUD_DEVNAME, &priv->dev);
  if (ret < 0)
    {
      (void)bk7258_aud_shutdown(&priv->dev);
      return ret;
    }

  return OK;
}

#endif /* CONFIG_BK7258_AUD */
