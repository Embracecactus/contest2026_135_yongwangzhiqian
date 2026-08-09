/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/ap/bk7258_mic.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) on-board analog microphone capture — NuttX audio
 * lower-half over the official Beken bk_aud_adc_* / bk_dma_* SDK APIs.
 * Zero register access.
 *
 * Board wiring: a single differential analog microphone on MICP1/MICN1,
 * i.e. hardware MIC1, which the AUD ADC maps onto the LEFT channel.
 *
 * Role ownership: AP only.  bk_aud_adc_*, ring_buffer_* and the audio
 * clock/power votes are compiled into the AP libdriver.a exclusively; the
 * CP bundle ships the headers but defines none of the symbols.
 *
 * Data path:
 *
 *   AUD ADC FIFO (0x47800044, L in [15:0] / R in [31:16])
 *     -> DMA_DEV_AUDIO_RX, DMA_WORK_MODE_REPEAT, 32-bit
 *       -> DMA ring buffer in DTCM (SDK RingBufferContext)
 *         -> DMA finish ISR posts a semaphore
 *           -> capture thread drains, de-interleaves, fills ap_buffer_s
 *             -> audio upper half via dev->upper(..., AUDIO_CALLBACK_DEQUEUE)
 *
 * Notes on SDK behaviour that shaped this driver (verified against the
 * v3.1.1.9 sources, not assumed):
 *
 *  1. bk_aud_adc_set_mic_mode() is a no-op on BK7258: sys_hal_aud_mic1_
 *     single_en() has its body commented out.  Differential mode is the
 *     ANA_REG19 reset default (MICSINGLEEN == 0) applied by
 *     bk_aud_driver_init(), so the call is kept only for readability.
 *  2. bk_aud_set_ana_mic0_gain() drives ANA_REG19 == hardware MIC1.  The
 *     confusingly named bk_aud_set_ana_mic1_gain() drives ANA_REG27 ==
 *     MIC2, whose path is "not support" on this SoC.
 *  3. The ADC FIFO is always L/R interleaved regardless of adc_chl, so a
 *     mono capture initialises with AUD_ADC_CHL_LR, transfers twice the
 *     payload and drops the right half in software.
 *  4. Start order is DMA then ADC; stop order is DMA then ADC.
 *  5. ring_buffer_read() must run in task context: it re-reads the DMA
 *     destination write pointer and rewrites the DMA pause address, which
 *     is the back-pressure mechanism that stops the DMA from lapping the
 *     reader.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_MIC

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <debug.h>
#include <syslog.h>
#include <assert.h>

#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/queue.h>
#include <nuttx/kthread.h>
#include <nuttx/audio/audio.h>

#include <arch/chip/bk7258_mic.h>

/* SDK API headers.
 *
 * Deliberately NOT <driver/aud.h>: that header is stale CLI-only baggage
 * and declares a three-argument bk_aud_adc_init() that contradicts the
 * real single-argument definition in aud_adc_driver.c.
 */

#include <driver/aud_adc.h>
#include <driver/aud_adc_types.h>
#include <driver/aud_common.h>
#include <driver/audio_ring_buff.h>
#include <driver/dma.h>
#include <driver/dma_types.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_MIC_DEVNAME
#  define CONFIG_BK7258_MIC_DEVNAME     "pcm0c"
#endif

#ifndef CONFIG_BK7258_MIC_SAMPLE_RATE
#  define CONFIG_BK7258_MIC_SAMPLE_RATE BK7258_MIC_RATE_16000
#endif

#ifndef CONFIG_BK7258_MIC_CHANNELS
#  define CONFIG_BK7258_MIC_CHANNELS    1
#endif

#ifndef CONFIG_BK7258_MIC_DIG_GAIN
#  define CONFIG_BK7258_MIC_DIG_GAIN    BK7258_MIC_DIG_GAIN_0DB
#endif

#ifndef CONFIG_BK7258_MIC_ANA_GAIN
#  define CONFIG_BK7258_MIC_ANA_GAIN    0
#endif

/* Frames of one channel carried per DMA completion.  The DMA transfer
 * length is derived from this and is always two channels wide because the
 * FIFO is interleaved.
 */

#ifndef CONFIG_BK7258_MIC_FRAME_SAMPLES
#  define CONFIG_BK7258_MIC_FRAME_SAMPLES 320
#endif

/* Number of frames the DMA ring can hold before the reader must catch up. */

#ifndef CONFIG_BK7258_MIC_RING_FRAMES
#  define CONFIG_BK7258_MIC_RING_FRAMES 4
#endif

#ifndef CONFIG_BK7258_MIC_PRIORITY
#  define CONFIG_BK7258_MIC_PRIORITY    150
#endif

#ifndef CONFIG_BK7258_MIC_STACKSIZE
#  define CONFIG_BK7258_MIC_STACKSIZE   2048
#endif

/* Always two 16-bit samples per FIFO word (left in the low half). */

#define BK7258_MIC_FIFO_CHANNELS        2u
#define BK7258_MIC_FIFO_WORD_BYTES      4u

/* One channel's worth of a frame, and the interleaved size the DMA moves. */

#define BK7258_MIC_FRAME_BYTES \
  (CONFIG_BK7258_MIC_FRAME_SAMPLES * BK7258_MIC_BYTES_PER_SAMPLE)

#define BK7258_MIC_DMA_FRAME_BYTES \
  (BK7258_MIC_FRAME_BYTES * BK7258_MIC_FIFO_CHANNELS)

/* The SDK requires the DMA pause address to differ from the loop end
 * address, so the ring is padded.  Mirrors DMA_CARRY_MIC_RINGBUF_SAFE_
 * INTERVAL in onboard_mic_record.c.
 */

#define BK7258_MIC_RING_GUARD_BYTES     8u

#define BK7258_MIC_RING_BYTES \
  ((BK7258_MIC_DMA_FRAME_BYTES * CONFIG_BK7258_MIC_RING_FRAMES) + \
   BK7258_MIC_RING_GUARD_BYTES)

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum bk7258_mic_state_e
{
  BK7258_MIC_STATE_RESET = 0,   /* Registered, hardware idle */
  BK7258_MIC_STATE_CONFIGURED,  /* configure() accepted a format */
  BK7258_MIC_STATE_RUNNING,     /* ADC + DMA streaming */
  BK7258_MIC_STATE_PAUSED       /* Streaming suspended, context retained */
};

struct bk7258_mic_dev_s
{
  struct audio_lowerhalf_s dev;        /* Must be first */

  /* Negotiated format */

  uint32_t samplerate;
  uint8_t  channels;
  uint8_t  dig_gain;
  uint8_t  ana_gain;

  /* Beken DMA / ring-buffer context */

  dma_id_t          dma_id;
  bool              dma_allocated;
  uint8_t          *ring_mem;          /* DMA destination, kernel heap */
  RingBufferContext ring;
  bool              ring_valid;

  /* De-interleave scratch: one interleaved DMA frame */

  uint8_t          *scratch;

  /* Capture thread */

  pid_t             pid;
  sem_t             dmasem;            /* Posted by the DMA finish ISR */
  sem_t             donesem;           /* Posted as the thread exits */
  volatile bool     terminate;
  volatile bool     streaming;

  enum bk7258_mic_state_e state;
  bool reserved;

  /* Buffers queued by the upper half awaiting capture payload */

  struct dq_queue_s pendq;
  mutex_t           lock;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Hardware core layer */

static int  bk7258_mic_hw_setup(struct bk7258_mic_dev_s *priv);
static void bk7258_mic_hw_teardown(struct bk7258_mic_dev_s *priv);
static int  bk7258_mic_hw_start(struct bk7258_mic_dev_s *priv);
static void bk7258_mic_hw_stop(struct bk7258_mic_dev_s *priv);
static void bk7258_mic_dma_isr(dma_id_t dma_id);

/* Capture thread */

static void bk7258_mic_deinterleave(int16_t *dest, const int16_t *src,
                                    unsigned int frames);
static int  bk7258_mic_capture_thread(int argc, char **argv);
static void bk7258_mic_flush_pending(struct bk7258_mic_dev_s *priv);
static void bk7258_mic_stop_thread(struct bk7258_mic_dev_s *priv);

/* audio_ops_s */

static int  bk7258_mic_getcaps(struct audio_lowerhalf_s *dev, int type,
                               struct audio_caps_s *caps);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int  bk7258_mic_configure(struct audio_lowerhalf_s *dev,
                                 void *session,
                                 const struct audio_caps_s *caps);
static int  bk7258_mic_start(struct audio_lowerhalf_s *dev, void *session);
static int  bk7258_mic_stop(struct audio_lowerhalf_s *dev, void *session);
static int  bk7258_mic_pause(struct audio_lowerhalf_s *dev, void *session);
static int  bk7258_mic_resume(struct audio_lowerhalf_s *dev, void *session);
static int  bk7258_mic_reserve(struct audio_lowerhalf_s *dev,
                               void **psession);
static int  bk7258_mic_release(struct audio_lowerhalf_s *dev,
                               void *session);
#else
static int  bk7258_mic_configure(struct audio_lowerhalf_s *dev,
                                 const struct audio_caps_s *caps);
static int  bk7258_mic_start(struct audio_lowerhalf_s *dev);
static int  bk7258_mic_stop(struct audio_lowerhalf_s *dev);
static int  bk7258_mic_pause(struct audio_lowerhalf_s *dev);
static int  bk7258_mic_resume(struct audio_lowerhalf_s *dev);
static int  bk7258_mic_reserve(struct audio_lowerhalf_s *dev);
static int  bk7258_mic_release(struct audio_lowerhalf_s *dev);
#endif
static int  bk7258_mic_shutdown(struct audio_lowerhalf_s *dev);
static int  bk7258_mic_enqueuebuffer(struct audio_lowerhalf_s *dev,
                                     struct ap_buffer_s *apb);
static int  bk7258_mic_cancelbuffer(struct audio_lowerhalf_s *dev,
                                    struct ap_buffer_s *apb);
static int  bk7258_mic_ioctl(struct audio_lowerhalf_s *dev, int cmd,
                             unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct audio_ops_s g_bk7258_mic_ops =
{
  .getcaps       = bk7258_mic_getcaps,
  .configure     = bk7258_mic_configure,
  .shutdown      = bk7258_mic_shutdown,
  .start         = bk7258_mic_start,
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  .stop          = bk7258_mic_stop,
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
  .pause         = bk7258_mic_pause,
  .resume        = bk7258_mic_resume,
#endif
  .enqueuebuffer = bk7258_mic_enqueuebuffer,
  .cancelbuffer  = bk7258_mic_cancelbuffer,
  .ioctl         = bk7258_mic_ioctl,
  .reserve       = bk7258_mic_reserve,
  .release       = bk7258_mic_release,
};

/* Single on-board microphone: one static instance, and the DMA finish ISR
 * needs it because bk_dma_register_isr() takes a bare void(*)(void) with no
 * argument to carry context.
 */

static struct bk7258_mic_dev_s g_bk7258_mic =
{
  .lock = NXMUTEX_INITIALIZER,
};

static bool g_bk7258_mic_registered;

/* The FIFO layout this driver de-interleaves against is fixed by the SoC:
 * REG_0x11 packs the left sample in [15:0] and the right in [31:16].
 */

_Static_assert(BK7258_MIC_BYTES_PER_SAMPLE * BK7258_MIC_FIFO_CHANNELS ==
               BK7258_MIC_FIFO_WORD_BYTES,
               "BK7258 AUD ADC FIFO must stay one 32-bit L/R sample pair");

_Static_assert((BK7258_MIC_DMA_FRAME_BYTES % BK7258_MIC_FIFO_WORD_BYTES)
               == 0,
               "BK7258 MIC DMA frame must be a whole number of FIFO words");

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_mic_result
 *
 * Description:
 *   Translate an SDK bk_err_t into a NuttX negated errno.
 *
 ****************************************************************************/

static int bk7258_mic_result(bk_err_t error)
{
  if (error == BK_OK)
    {
      return OK;
    }

  return -EIO;
}

/****************************************************************************
 * Name: bk7258_mic_dma_isr
 *
 * Description:
 *   Beken DMA finish callback.  Interrupt context: post the semaphore and
 *   return.  All ring-buffer work happens on the capture thread because
 *   ring_buffer_read() rewrites the DMA pause address.
 *
 *   dma_isr_t is void(*)(dma_id_t); the Beken samples cast this away with
 *   a (void *) at the registration site, which silently hides a prototype
 *   mismatch.  Match the real typedef instead so no cast is needed.
 *
 ****************************************************************************/

static void bk7258_mic_dma_isr(dma_id_t dma_id)
{
  struct bk7258_mic_dev_s *priv = &g_bk7258_mic;

  UNUSED(dma_id);

  if (priv->streaming)
    {
      nxsem_post(&priv->dmasem);
    }
}

/****************************************************************************
 * Name: bk7258_mic_hw_setup
 *
 * Description:
 *   Bring up the AUD ADC and the capture DMA channel.  Mirrors the official
 *   onboard_mic_stream reference sequence.  Leaves both stopped.
 *
 ****************************************************************************/

static int bk7258_mic_hw_setup(struct bk7258_mic_dev_s *priv)
{
  aud_adc_config_t cfg = DEFAULT_AUD_ADC_CONFIG();
  dma_config_t dma_cfg;
  uint32_t fifo_addr = 0;
  bk_err_t err;
  int ret;

  /* The ADC FIFO is L/R interleaved no matter what, so always open both
   * channels and drop the unused half in software.
   */

  cfg.adc_chl       = AUD_ADC_CHL_LR;
  cfg.samp_rate     = priv->samplerate;
  cfg.adc_gain      = priv->dig_gain;
  cfg.adc_mode      = AUD_ADC_MODE_DIFFEN;
  cfg.adc_samp_edge = AUD_ADC_SAMP_EDGE_RISING;
  cfg.clk_src       = AUD_CLK_APLL;

  /* bk_aud_adc_init() internally performs bk_aud_driver_init() (power vote,
   * PM_CLK_ID_AUDIO, INT_SRC_AUDIO registration, ANA_REG baseline) and
   * bk_aud_clk_config(), including the full APLL bring-up sequence.
   */

  err = bk_aud_adc_init(&cfg);
  if (err != BK_OK)
    {
      auderr("ERROR: bk_aud_adc_init failed: %d\n", err);
      return bk7258_mic_result(err);
    }

  /* No-op on BK7258 (see file header note 1) but kept so the intent is
   * explicit and the code stays portable to parts where it is wired.
   */

  bk_aud_adc_set_mic_mode(AUD_MIC_MIC1, AUD_ADC_MODE_DIFFEN);

  /* ana_mic0 == ANA_REG19 == hardware MIC1 == MICP1/MICN1. */

  err = bk_aud_set_ana_mic0_gain(priv->ana_gain);
  if (err != BK_OK)
    {
      auderr("ERROR: bk_aud_set_ana_mic0_gain failed: %d\n", err);
      ret = bk7258_mic_result(err);
      goto err_adc;
    }

  /* DMA destination ring, plus the de-interleave scratch frame. */

  priv->ring_mem = kmm_zalloc(BK7258_MIC_RING_BYTES);
  if (priv->ring_mem == NULL)
    {
      ret = -ENOMEM;
      goto err_adc;
    }

  priv->scratch = kmm_malloc(BK7258_MIC_DMA_FRAME_BYTES);
  if (priv->scratch == NULL)
    {
      ret = -ENOMEM;
      goto err_ring_mem;
    }

  /* The API comment says "> DMA_ID_MAX", but every official v3.1.1.9
   * audio caller treats DMA_ID_MAX as the failure sentinel.  Follow the
   * executable SDK usage, not the inconsistent prose.
   */

  priv->dma_id = bk_dma_alloc(DMA_DEV_AUDIO);
  if (priv->dma_id < DMA_ID_0 || priv->dma_id >= DMA_ID_MAX)
    {
      auderr("ERROR: bk_dma_alloc(DMA_DEV_AUDIO) exhausted\n");
      ret = -EBUSY;
      goto err_scratch;
    }

  priv->dma_allocated = true;

  err = bk_aud_adc_get_fifo_addr(&fifo_addr);
  if (err != BK_OK)
    {
      auderr("ERROR: bk_aud_adc_get_fifo_addr failed: %d\n", err);
      ret = bk7258_mic_result(err);
      goto err_dma_alloc;
    }

  memset(&dma_cfg, 0, sizeof(dma_cfg));

  dma_cfg.mode      = DMA_WORK_MODE_REPEAT;
  dma_cfg.chan_prio = 1;

  dma_cfg.src.dev         = DMA_DEV_AUDIO_RX;
  dma_cfg.src.width       = DMA_DATA_WIDTH_32BITS;
  dma_cfg.src.start_addr  = fifo_addr;
  dma_cfg.src.end_addr    = fifo_addr + BK7258_MIC_FIFO_WORD_BYTES;
  dma_cfg.src.addr_inc_en = DMA_ADDR_INC_ENABLE;
  dma_cfg.src.addr_loop_en = DMA_ADDR_LOOP_ENABLE;

  dma_cfg.dst.dev         = DMA_DEV_DTCM;
  dma_cfg.dst.width       = DMA_DATA_WIDTH_32BITS;
  dma_cfg.dst.start_addr  = (uint32_t)(uintptr_t)priv->ring_mem;
  dma_cfg.dst.end_addr    = (uint32_t)(uintptr_t)priv->ring_mem +
                            BK7258_MIC_RING_BYTES;
  dma_cfg.dst.addr_inc_en = DMA_ADDR_INC_ENABLE;
  dma_cfg.dst.addr_loop_en = DMA_ADDR_LOOP_ENABLE;

  err = bk_dma_init(priv->dma_id, &dma_cfg);
  if (err != BK_OK)
    {
      auderr("ERROR: bk_dma_init failed: %d\n", err);
      ret = bk7258_mic_result(err);
      goto err_dma_alloc;
    }

  /* Interrupt cadence: one interleaved frame per completion. */

  err = bk_dma_set_transfer_len(priv->dma_id, BK7258_MIC_DMA_FRAME_BYTES);
  if (err != BK_OK)
    {
      auderr("ERROR: bk_dma_set_transfer_len failed: %d\n", err);
      ret = bk7258_mic_result(err);
      goto err_dma_init;
    }

#ifdef CONFIG_BK7258_MIC_DMA_SECURE
  bk_dma_set_src_sec_attr(priv->dma_id, DMA_ATTR_SEC);
  bk_dma_set_dest_sec_attr(priv->dma_id, DMA_ATTR_SEC);
#endif

  err = bk_dma_register_isr(priv->dma_id, NULL, bk7258_mic_dma_isr);
  if (err != BK_OK)
    {
      auderr("ERROR: bk_dma_register_isr failed: %d\n", err);
      ret = bk7258_mic_result(err);
      goto err_dma_init;
    }

  err = bk_dma_enable_finish_interrupt(priv->dma_id);
  if (err != BK_OK)
    {
      auderr("ERROR: bk_dma_enable_finish_interrupt failed: %d\n", err);
      ret = bk7258_mic_result(err);
      goto err_dma_init;
    }

  /* RB_DMA_TYPE_WRITE: the DMA is the producer.  ring_buffer_read() will
   * pull the write pointer straight out of the DMA registers and push back
   * on the DMA by moving its pause address.
   */

  ring_buffer_init(&priv->ring, priv->ring_mem, BK7258_MIC_RING_BYTES,
                   priv->dma_id, RB_DMA_TYPE_WRITE);
  priv->ring_valid = true;

  audinfo("AUD ADC ready: %" PRIu32 " Hz, %u ch, dig 0x%02x, ana 0x%02x\n",
          priv->samplerate, priv->channels, priv->dig_gain, priv->ana_gain);

  return OK;

err_dma_init:
  bk_dma_deinit(priv->dma_id);

err_dma_alloc:
  bk_dma_free(DMA_DEV_AUDIO, priv->dma_id);
  priv->dma_allocated = false;

err_scratch:
  kmm_free(priv->scratch);
  priv->scratch = NULL;

err_ring_mem:
  kmm_free(priv->ring_mem);
  priv->ring_mem = NULL;

err_adc:
  bk_aud_adc_deinit();
  return ret;
}

/****************************************************************************
 * Name: bk7258_mic_hw_teardown
 *
 * Description:
 *   Release everything bk7258_mic_hw_setup() acquired.  Safe to call when
 *   setup never ran or partially failed.
 *
 ****************************************************************************/

static void bk7258_mic_hw_teardown(struct bk7258_mic_dev_s *priv)
{
  if (priv->ring_valid)
    {
      /* ring_buffer_clear() updates DMA producer/pause pointers.  It must
       * run before the DMA channel is deinitialized and returned.
       */

      ring_buffer_clear(&priv->ring);
      priv->ring_valid = false;
    }

  if (priv->dma_allocated)
    {
      bk_dma_deinit(priv->dma_id);
      bk_dma_free(DMA_DEV_AUDIO, priv->dma_id);
      priv->dma_allocated = false;
    }

  /* bk_aud_adc_deinit() stops the ADC first, then drops the audio power
   * vote through bk_aud_driver_deinit(), which is reference counted and so
   * will not tear down a DAC that is still in use.
   */

  bk_aud_adc_deinit();

  kmm_free(priv->scratch);
  priv->scratch = NULL;

  kmm_free(priv->ring_mem);
  priv->ring_mem = NULL;
}

/****************************************************************************
 * Name: bk7258_mic_hw_start / bk7258_mic_hw_stop
 *
 * Description:
 *   Order matters and matches the Beken reference: DMA before ADC on the
 *   way up so no samples are produced before there is somewhere to put
 *   them, and DMA before ADC on the way down so the transport is cut before
 *   the source.
 *
 ****************************************************************************/

static int bk7258_mic_hw_start(struct bk7258_mic_dev_s *priv)
{
  bk_err_t err;

  /* Reset the read/write pointers and push the DMA pause address out to the
   * end of the ring; without this a resume replays stale pointers.
   */

  ring_buffer_clear(&priv->ring);

  err = bk_dma_start(priv->dma_id);
  if (err != BK_OK)
    {
      auderr("ERROR: bk_dma_start failed: %d\n", err);
      return bk7258_mic_result(err);
    }

  err = bk_aud_adc_start();
  if (err != BK_OK)
    {
      auderr("ERROR: bk_aud_adc_start failed: %d\n", err);
      bk_dma_stop(priv->dma_id);
      return bk7258_mic_result(err);
    }

  return OK;
}

static void bk7258_mic_hw_stop(struct bk7258_mic_dev_s *priv)
{
  bk_dma_stop(priv->dma_id);
  bk_aud_adc_stop();
}

/****************************************************************************
 * Name: bk7258_mic_deinterleave
 *
 * Description:
 *   Collapse an interleaved L/R block down to the left channel only, which
 *   is hardware MIC1.  Matches the reference loop ptr[i] = ptr[2 * i].
 *
 ****************************************************************************/

static void bk7258_mic_deinterleave(int16_t *dest, const int16_t *src,
                                    unsigned int frames)
{
  unsigned int i;

  for (i = 0; i < frames; i++)
    {
      dest[i] = src[2 * i];
    }
}

/****************************************************************************
 * Name: bk7258_mic_flush_pending
 *
 * Description:
 *   Hand every still-queued buffer back to the upper half so a waiting
 *   thread is not stranded when capture stops.
 *
 ****************************************************************************/

static void bk7258_mic_flush_pending(struct bk7258_mic_dev_s *priv)
{
  struct ap_buffer_s *apb;

  nxmutex_lock(&priv->lock);

  while ((apb = (struct ap_buffer_s *)dq_remfirst(&priv->pendq)) != NULL)
    {
      nxmutex_unlock(&priv->lock);

      apb->nbytes  = 0;
      apb->curbyte = 0;
      apb->flags  |= AUDIO_APB_FINAL;

#ifdef CONFIG_AUDIO_MULTI_SESSION
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK,
                      priv);
#else
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
#endif

      nxmutex_lock(&priv->lock);
    }

  nxmutex_unlock(&priv->lock);
}

/****************************************************************************
 * Name: bk7258_mic_capture_thread
 *
 * Description:
 *   Drain the DMA ring into upper-half buffers.  Runs in task context
 *   because ring_buffer_read() touches DMA registers.
 *
 ****************************************************************************/

static int bk7258_mic_capture_thread(int argc, char **argv)
{
  struct bk7258_mic_dev_s *priv = &g_bk7258_mic;
  struct ap_buffer_s *apb;
  unsigned int frames;
  uint32_t got;

  audinfo("Capture thread started\n");

  while (!priv->terminate)
    {
      if (nxsem_wait_uninterruptible(&priv->dmasem) < 0)
        {
          continue;
        }

      if (priv->terminate)
        {
          break;
        }

      /* Only consume a frame once the DMA has actually landed one. */

      if (ring_buffer_get_fill_size(&priv->ring) <
          BK7258_MIC_DMA_FRAME_BYTES)
        {
          continue;
        }

      got = ring_buffer_read(&priv->ring, priv->scratch,
                             BK7258_MIC_DMA_FRAME_BYTES);
      if (got == 0)
        {
          continue;
        }

      /* Whole L/R pairs only. */

      frames = got / BK7258_MIC_FIFO_WORD_BYTES;
      if (frames == 0)
        {
          continue;
        }

      nxmutex_lock(&priv->lock);
      apb = (struct ap_buffer_s *)dq_remfirst(&priv->pendq);
      nxmutex_unlock(&priv->lock);

      if (apb == NULL)
        {
          /* No consumer buffer available; the samples are dropped rather
           * than stalling the DMA and corrupting the stream timing.
           */

          audwarn("WARNING: no queued buffer, dropped %u frames\n", frames);
          continue;
        }

      if (priv->channels == 1)
        {
          unsigned int capacity = apb->nmaxbytes /
                                  BK7258_MIC_BYTES_PER_SAMPLE;

          if (frames > capacity)
            {
              frames = capacity;
            }

          bk7258_mic_deinterleave((int16_t *)apb->samp,
                                  (const int16_t *)priv->scratch, frames);
          apb->nbytes = frames * BK7258_MIC_BYTES_PER_SAMPLE;
        }
      else
        {
          if (got > apb->nmaxbytes)
            {
              got = apb->nmaxbytes;
            }

          memcpy(apb->samp, priv->scratch, got);
          apb->nbytes = got;
        }

      apb->curbyte  = 0;
      apb->nsamples = frames;

      {
        bool final = (apb->flags & AUDIO_APB_FINAL) != 0;

#ifdef CONFIG_AUDIO_MULTI_SESSION
        priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK,
                        priv);
#else
        priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
#endif

        if (final)
          {
#ifdef CONFIG_AUDIO_MULTI_SESSION
            priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE,
                            NULL, OK, priv);
#else
            priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE,
                            NULL, OK);
#endif
          }
      }
    }

  bk7258_mic_flush_pending(priv);

  audinfo("Capture thread exiting\n");

  /* Must be the very last action.  bk7258_mic_stop_thread() frees the ring
   * and the scratch frame as soon as this is posted, and it — not this
   * thread — clears priv->pid, so that the "is a thread running" test and
   * the handshake can never disagree.
   */

  nxsem_post(&priv->donesem);
  return 0;
}

/****************************************************************************
 * Name: bk7258_mic_stop_thread
 *
 * Description:
 *   Ask the capture thread to exit and block until it has, so that the
 *   caller can safely release the ring buffer and scratch frame the thread
 *   dereferences.  Safe to call when no thread is running.
 *
 ****************************************************************************/

static void bk7258_mic_stop_thread(struct bk7258_mic_dev_s *priv)
{
  if (priv->pid < 0)
    {
      return;
    }

  priv->streaming = false;
  priv->terminate = true;

  /* Wake the thread out of its wait for the next DMA completion. */

  nxsem_post(&priv->dmasem);
  nxsem_wait_uninterruptible(&priv->donesem);

  priv->pid = -1;
}

/****************************************************************************
 * Private: audio lower-half operations
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_mic_getcaps
 ****************************************************************************/

static int bk7258_mic_getcaps(struct audio_lowerhalf_s *dev, int type,
                              struct audio_caps_s *caps)
{
  if (caps == NULL || caps->ac_len < sizeof(struct audio_caps_s))
    {
      return -EINVAL;
    }

  caps->ac_format.hw  = 0;
  caps->ac_controls.w = 0;

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_QUERY:

        /* Capture only. */

        caps->ac_channels = CONFIG_BK7258_MIC_CHANNELS;

        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:
              caps->ac_controls.b[0] = AUDIO_TYPE_INPUT;
              caps->ac_format.hw = 1 << (AUDIO_FMT_PCM - 1);
              break;

            default:
              caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
              break;
          }
        break;

      case AUDIO_TYPE_INPUT:
        caps->ac_channels = CONFIG_BK7258_MIC_CHANNELS;

        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:

              caps->ac_controls.hw[0] = AUDIO_SAMP_RATE_8K |
                                        AUDIO_SAMP_RATE_11K |
                                        AUDIO_SAMP_RATE_12K |
                                        AUDIO_SAMP_RATE_16K |
                                        AUDIO_SAMP_RATE_22K |
                                        AUDIO_SAMP_RATE_24K |
                                        AUDIO_SAMP_RATE_32K |
                                        AUDIO_SAMP_RATE_44K |
                                        AUDIO_SAMP_RATE_48K;
              break;

            default:
              break;
          }
        break;

      default:
        caps->ac_subtype = 0;
        caps->ac_channels = 0;
        break;
    }

  (void)dev;
  (void)type;
  return caps->ac_len;
}

/****************************************************************************
 * Name: bk7258_mic_configure
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_mic_configure(struct audio_lowerhalf_s *dev,
                                void *session,
                                const struct audio_caps_s *caps)
#else
static int bk7258_mic_configure(struct audio_lowerhalf_s *dev,
                                const struct audio_caps_s *caps)
#endif
{
  struct bk7258_mic_dev_s *priv = (struct bk7258_mic_dev_s *)dev;
  int ret = OK;

  DEBUGASSERT(priv != NULL && caps != NULL);

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_INPUT:
        {
          uint32_t rate = caps->ac_controls.hw[0] |
                          (caps->ac_controls.b[3] << 16);
          uint8_t  channels = caps->ac_channels;
          uint8_t  bits = caps->ac_controls.b[2];

          if (bits != BK7258_MIC_BITS_PER_SAMPLE)
            {
              auderr("ERROR: only %u-bit PCM is supported, got %u\n",
                     BK7258_MIC_BITS_PER_SAMPLE, bits);
              return -EINVAL;
            }

          if (channels != 1 && channels != 2)
            {
              auderr("ERROR: unsupported channel count %u\n", channels);
              return -EINVAL;
            }

          switch (rate)
            {
              case BK7258_MIC_RATE_8000:
              case BK7258_MIC_RATE_11025:
              case BK7258_MIC_RATE_12000:
              case BK7258_MIC_RATE_16000:
              case BK7258_MIC_RATE_22050:
              case BK7258_MIC_RATE_24000:
              case BK7258_MIC_RATE_32000:
              case BK7258_MIC_RATE_44100:
              case BK7258_MIC_RATE_48000:
                break;

              default:
                auderr("ERROR: unsupported sample rate %" PRIu32 "\n",
                       rate);
                return -EINVAL;
            }

          nxmutex_lock(&priv->lock);

          if (priv->state == BK7258_MIC_STATE_RUNNING ||
              priv->state == BK7258_MIC_STATE_PAUSED)
            {
              nxmutex_unlock(&priv->lock);
              return -EBUSY;
            }

          priv->samplerate = rate;
          priv->channels   = channels;
          priv->state      = BK7258_MIC_STATE_CONFIGURED;

          nxmutex_unlock(&priv->lock);

          audinfo("Configured %" PRIu32 " Hz, %u ch, %u bits\n",
                  rate, channels, bits);
        }
        break;

      case AUDIO_TYPE_FEATURE:

        /* Gain is exposed through AUDIOIOC_* volume semantics; treat an
         * unrecognised feature unit as unsupported rather than silently
         * accepting it.
         */

        ret = -ENOTTY;
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: bk7258_mic_start
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_mic_start(struct audio_lowerhalf_s *dev, void *session)
#else
static int bk7258_mic_start(struct audio_lowerhalf_s *dev)
#endif
{
  struct bk7258_mic_dev_s *priv = (struct bk7258_mic_dev_s *)dev;
  int ret;

  DEBUGASSERT(priv != NULL);

  if (priv->state == BK7258_MIC_STATE_RUNNING)
    {
      return OK;
    }

  if (priv->state != BK7258_MIC_STATE_CONFIGURED)
    {
      auderr("ERROR: start before configure\n");
      return -EINVAL;
    }

  ret = bk7258_mic_hw_setup(priv);
  if (ret < 0)
    {
      return ret;
    }

  priv->terminate = false;
  priv->streaming = true;

  /* Discard completions left over by the previous run, otherwise the new
   * capture thread wakes for frames that are no longer in the ring.
   */

  while (nxsem_trywait(&priv->dmasem) >= 0);

  /* The capture thread reaches the state through the static singleton, the
   * same way the DMA ISR must, so no argv marshalling is needed.
   */

  ret = kthread_create("bk7258_mic", CONFIG_BK7258_MIC_PRIORITY,
                       CONFIG_BK7258_MIC_STACKSIZE,
                       bk7258_mic_capture_thread, NULL);
  if (ret < 0)
    {
      auderr("ERROR: kthread_create failed: %d\n", ret);
      priv->streaming = false;
      bk7258_mic_hw_teardown(priv);
      return ret;
    }

  priv->pid = (pid_t)ret;

  ret = bk7258_mic_hw_start(priv);
  if (ret < 0)
    {
      bk7258_mic_stop_thread(priv);
      bk7258_mic_hw_teardown(priv);
      return ret;
    }

  priv->state = BK7258_MIC_STATE_RUNNING;
  audinfo("Capture started\n");

  return OK;
}

/****************************************************************************
 * Name: bk7258_mic_stop
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_mic_stop(struct audio_lowerhalf_s *dev, void *session)
#else
static int bk7258_mic_stop(struct audio_lowerhalf_s *dev)
#endif
{
  struct bk7258_mic_dev_s *priv = (struct bk7258_mic_dev_s *)dev;

  DEBUGASSERT(priv != NULL);

  if (priv->state != BK7258_MIC_STATE_RUNNING &&
      priv->state != BK7258_MIC_STATE_PAUSED)
    {
      return OK;
    }

  bk7258_mic_hw_stop(priv);

  /* Join before teardown: the thread dereferences the ring and the scratch
   * frame that bk7258_mic_hw_teardown() is about to free.
   */

  bk7258_mic_stop_thread(priv);
  bk7258_mic_hw_teardown(priv);

  priv->state = BK7258_MIC_STATE_CONFIGURED;
  audinfo("Capture stopped\n");

  return OK;
}
#endif

/****************************************************************************
 * Name: bk7258_mic_pause / bk7258_mic_resume
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_mic_pause(struct audio_lowerhalf_s *dev, void *session)
#else
static int bk7258_mic_pause(struct audio_lowerhalf_s *dev)
#endif
{
  struct bk7258_mic_dev_s *priv = (struct bk7258_mic_dev_s *)dev;

  DEBUGASSERT(priv != NULL);

  if (priv->state != BK7258_MIC_STATE_RUNNING)
    {
      return OK;
    }

  /* Keep the DMA channel and ring allocated so resume() is cheap; only the
   * transport and the converter are halted.
   */

  priv->streaming = false;
  bk7258_mic_hw_stop(priv);
  priv->state = BK7258_MIC_STATE_PAUSED;

  return OK;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_mic_resume(struct audio_lowerhalf_s *dev, void *session)
#else
static int bk7258_mic_resume(struct audio_lowerhalf_s *dev)
#endif
{
  struct bk7258_mic_dev_s *priv = (struct bk7258_mic_dev_s *)dev;
  int ret;

  DEBUGASSERT(priv != NULL);

  if (priv->state != BK7258_MIC_STATE_PAUSED)
    {
      return OK;
    }

  priv->streaming = true;

  ret = bk7258_mic_hw_start(priv);
  if (ret < 0)
    {
      priv->streaming = false;
      return ret;
    }

  priv->state = BK7258_MIC_STATE_RUNNING;
  return OK;
}
#endif

/****************************************************************************
 * Name: bk7258_mic_shutdown
 ****************************************************************************/

static int bk7258_mic_shutdown(struct audio_lowerhalf_s *dev)
{
  struct bk7258_mic_dev_s *priv = (struct bk7258_mic_dev_s *)dev;

  DEBUGASSERT(priv != NULL);

  if (priv->state == BK7258_MIC_STATE_RUNNING ||
      priv->state == BK7258_MIC_STATE_PAUSED)
    {
      bk7258_mic_hw_stop(priv);
      bk7258_mic_stop_thread(priv);
      bk7258_mic_hw_teardown(priv);
    }

  bk7258_mic_flush_pending(priv);

  priv->state = BK7258_MIC_STATE_RESET;
  return OK;
}

/****************************************************************************
 * Name: bk7258_mic_enqueuebuffer
 ****************************************************************************/

static int bk7258_mic_enqueuebuffer(struct audio_lowerhalf_s *dev,
                                    struct ap_buffer_s *apb)
{
  struct bk7258_mic_dev_s *priv = (struct bk7258_mic_dev_s *)dev;
  struct dq_entry_s *entry;

  if (priv == NULL || apb == NULL || apb->samp == NULL ||
      apb->nmaxbytes < BK7258_MIC_BYTES_PER_SAMPLE)
    {
      return -EINVAL;
    }

  if (!priv->reserved)
    {
      return -EACCES;
    }

  apb->nbytes  = 0;
  apb->curbyte = 0;

  nxmutex_lock(&priv->lock);

  if (!priv->reserved)
    {
      nxmutex_unlock(&priv->lock);
      return -EACCES;
    }

  for (entry = dq_peek(&priv->pendq); entry != NULL;
       entry = dq_next(entry))
    {
      if (entry == &apb->dq_entry)
        {
          nxmutex_unlock(&priv->lock);
          return -EALREADY;
        }
    }

  dq_addlast(&apb->dq_entry, &priv->pendq);
  nxmutex_unlock(&priv->lock);

  return OK;
}

/****************************************************************************
 * Name: bk7258_mic_cancelbuffer
 ****************************************************************************/

static int bk7258_mic_cancelbuffer(struct audio_lowerhalf_s *dev,
                                   struct ap_buffer_s *apb)
{
  struct bk7258_mic_dev_s *priv = (struct bk7258_mic_dev_s *)dev;
  struct dq_entry_s *entry;
  bool found = false;

  if (priv == NULL || apb == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&priv->lock);

  for (entry = dq_peek(&priv->pendq); entry != NULL;
       entry = dq_next(entry))
    {
      if (entry == &apb->dq_entry)
        {
          dq_rem(entry, &priv->pendq);
          found = true;
          break;
        }
    }

  nxmutex_unlock(&priv->lock);

  if (!found)
    {
      return -ENOENT;
    }

#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb,
                  OK, priv);
#else
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb,
                  OK);
#endif

  return OK;
}

/****************************************************************************
 * Name: bk7258_mic_ioctl
 ****************************************************************************/

static int bk7258_mic_ioctl(struct audio_lowerhalf_s *dev, int cmd,
                            unsigned long arg)
{
  int ret = OK;

  switch (cmd)
    {
#ifdef CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS
      case AUDIOIOC_GETBUFFERINFO:
        {
          struct ap_buffer_info_s *info = (struct ap_buffer_info_s *)arg;

          DEBUGASSERT(info != NULL);

          info->buffer_size = BK7258_MIC_FRAME_BYTES;
          info->nbuffers    = CONFIG_BK7258_MIC_RING_FRAMES;
        }
        break;
#endif

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: bk7258_mic_reserve / bk7258_mic_release
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_mic_reserve(struct audio_lowerhalf_s *dev,
                              void **psession)
#else
static int bk7258_mic_reserve(struct audio_lowerhalf_s *dev)
#endif
{
  struct bk7258_mic_dev_s *priv = (struct bk7258_mic_dev_s *)dev;

  DEBUGASSERT(priv != NULL);

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (psession != NULL)
    {
      *psession = NULL;
    }
  else
    {
      return -EINVAL;
    }
#endif

  nxmutex_lock(&priv->lock);

  if (priv->reserved)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  priv->reserved = true;
  dq_init(&priv->pendq);
  nxmutex_unlock(&priv->lock);

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (psession != NULL)
    {
      *psession = priv;
    }
#endif

  return OK;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_mic_release(struct audio_lowerhalf_s *dev, void *session)
#else
static int bk7258_mic_release(struct audio_lowerhalf_s *dev)
#endif
{
  struct bk7258_mic_dev_s *priv = (struct bk7258_mic_dev_s *)dev;

  DEBUGASSERT(priv != NULL);

  bk7258_mic_flush_pending(priv);

  nxmutex_lock(&priv->lock);
  priv->reserved = false;
  nxmutex_unlock(&priv->lock);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_mic_initialize
 *
 * Description:
 *   Publish the on-board microphone as /dev/audio/<devname>.  Idempotent.
 *
 ****************************************************************************/

int bk7258_mic_initialize(void)
{
  struct bk7258_mic_dev_s *priv = &g_bk7258_mic;
  int ret;

  if (g_bk7258_mic_registered)
    {
      return OK;
    }

  priv->dev.ops    = &g_bk7258_mic_ops;
  priv->samplerate = CONFIG_BK7258_MIC_SAMPLE_RATE;
  priv->channels   = CONFIG_BK7258_MIC_CHANNELS;
  priv->dig_gain   = CONFIG_BK7258_MIC_DIG_GAIN;
  priv->ana_gain   = CONFIG_BK7258_MIC_ANA_GAIN;
  priv->dma_id     = DMA_ID_MAX + 1;   /* Not a valid channel */
  priv->pid        = -1;
  priv->state      = BK7258_MIC_STATE_RESET;
  priv->reserved   = false;

  if (priv->ana_gain > BK7258_MIC_ANA_GAIN_MAX)
    {
      audwarn("WARNING: analog gain 0x%02x exceeds 0x%02x, clamping\n",
              priv->ana_gain, BK7258_MIC_ANA_GAIN_MAX);
      priv->ana_gain = BK7258_MIC_ANA_GAIN_MAX;
    }

  if (priv->dig_gain > BK7258_MIC_DIG_GAIN_MAX)
    {
      audwarn("WARNING: digital gain 0x%02x exceeds 0x%02x, clamping\n",
              priv->dig_gain, BK7258_MIC_DIG_GAIN_MAX);
      priv->dig_gain = BK7258_MIC_DIG_GAIN_MAX;
    }

  dq_init(&priv->pendq);
  ret = nxsem_init(&priv->dmasem, 0, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxsem_init(&priv->donesem, 0, 0);
  if (ret < 0)
    {
      nxsem_destroy(&priv->dmasem);
      return ret;
    }

  ret = audio_register(CONFIG_BK7258_MIC_DEVNAME, &priv->dev);
  if (ret < 0)
    {
      auderr("ERROR: audio_register(%s) failed: %d\n",
             CONFIG_BK7258_MIC_DEVNAME, ret);
      nxsem_destroy(&priv->dmasem);
      nxsem_destroy(&priv->donesem);
      return ret;
    }

  g_bk7258_mic_registered = true;

  syslog(LOG_INFO,
         "BMIC BOOT PASS dev=/dev/audio/%s rate=%u ch=%u dig=0x%02x "
         "ana=0x%02x\n",
         CONFIG_BK7258_MIC_DEVNAME, (unsigned)priv->samplerate,
         (unsigned)priv->channels, (unsigned)priv->dig_gain,
         (unsigned)priv->ana_gain);

  return OK;
}

#endif /* CONFIG_BK7258_MIC */
