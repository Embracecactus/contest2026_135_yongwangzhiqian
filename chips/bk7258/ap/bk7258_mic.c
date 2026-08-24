/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/ap/bk7258_mic.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 on-board analog microphone capture — a shared NuttX audio
 * lower-half over the official Beken bk_aud_adc_* / bk_dma_* SDK APIs.
 * Register access is read-only and limited to checking that the immutable
 * SDK actually committed a channel configuration.  The selected physical-
 * board header supplies the fixed one- or two-microphone topology.
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
 *  1. bk_aud_adc_set_mic_mode() is a no-op on BK7258 because both single_en
 *     HAL bodies are empty.  Differential mode is nevertheless established
 *     by bk_aud_driver_init() through the ANA_REG19/ANA_REG27 defaults.
 *  2. bk_aud_set_ana_mic0_gain() drives ANA_REG19 == hardware MIC1;
 *     bk_aud_set_ana_mic1_gain() drives ANA_REG27 == hardware MIC2.
 *     CONFIG_SOC_BK7236XX is also selected for the BK7258 SDK bundle, so its
 *     common audio init programs both analog records even though the generic
 *     mic2_en HAL helper is empty.
 *  3. The ADC FIFO is always a 32-bit L/R word.  The current official
 *     onboard_mic_stream therefore initializes AUD_ADC_CHL_LR and uses a
 *     32-bit DMA for both stream formats.  Mono capture drops the unused
 *     right half; stereo capture preserves the MIC1/MIC2 sample pair.
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

#include <arch/chip/bk7258_board_binding.h>
#include <arch/chip/bk7258_mic.h>
#include <arch/chip/bk7258_psram.h>

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
#include <soc/reg_base.h>

#include "bk7258_media_root.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_MIC_DEVNAME
#  define CONFIG_BK7258_MIC_DEVNAME     "pcm0c"
#endif

#define BK7258_MIC_DMA_UNIT_CHANNELS     8u
#define BK7258_MIC_DMA_UNIT_STRIDE       0x10000u
#define BK7258_MIC_DMA_CHANNEL_BASE      0x40u
#define BK7258_MIC_DMA_CHANNEL_STRIDE    0x40u
#define BK7258_MIC_DMA_CTRL_OFFSET       0x00u
#define BK7258_MIC_DMA_DEST_START_OFFSET 0x04u
#define BK7258_MIC_DMA_REQ_MUX_OFFSET    0x1cu

/* On BK7258 the pinned SDK occasionally returns BK_OK from bk_dma_init()
 * before its bulk channel configuration is visible in the DMA registers.
 * Later one-field setters still take effect, leaving a misleading channel
 * with transfer length / IRQ bits but no request mux or destination.  Verify
 * the immutable SDK's complete programming result and retry through its
 * public deinit/init contract instead of writing any register ourselves.
 *
 * The expected values are the BK7258 encoding of this driver's fixed setup:
 * repeat mode, 32-bit source/destination, increment+loop on both ends,
 * priority 1, AUDIO_RX -> DTCM.  Enable and interrupt-enable bits are omitted
 * from the control comparison because they are managed separately.
 */

#define BK7258_MIC_DMA_PROGRAM_ATTEMPTS  3u
#define BK7258_MIC_DMA_RETRY_DELAY_US    10u
#define BK7258_MIC_DMA_CTRL_CONFIG_MASK  0x00001ff8u
#define BK7258_MIC_DMA_CTRL_CONFIG_VALUE 0x00001fa8u
#define BK7258_MIC_DMA_CTRL_LENGTH_MASK  0xffff0000u
#define BK7258_MIC_DMA_REQ_MUX_MASK      0x3f7ff3ffu
#define BK7258_MIC_DMA_REQ_MUX_VALUE     0x0030000eu

#ifndef CONFIG_BK7258_MIC_SAMPLE_RATE
#  define CONFIG_BK7258_MIC_SAMPLE_RATE BK7258_MIC_RATE_16000
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

  /* Immutable physical-board contract supplied by board bring-up. */

  FAR const struct bk7258_mic_binding_s *binding;
  FAR const struct bk7258_mic_config_s *config;

  /* Negotiated format */

  uint32_t samplerate;
  uint8_t  channels;
  uint8_t  dig_gain;
  uint8_t  ana_gain;

  /* Beken DMA / ring-buffer context */

  dma_id_t          dma_id;
  bool              dma_allocated;
  dma_config_t      dma_config;
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

  /* Serialize the capture worker against pause/stop.  Once pause returns,
   * no buffer callback from the pre-pause stream remains in flight.
   */

  mutex_t           worker_lock;

  enum bk7258_mic_state_e state;
  bool close_safe;
  bool reserved;
  bool audio_session_owned;

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
static int  bk7258_mic_result(bk_err_t error);
static int  bk7258_mic_dma_program(struct bk7258_mic_dev_s *priv,
                                   const dma_config_t *config);
static void bk7258_mic_dma_isr(dma_id_t dma_id);

/* Capture thread */

static void bk7258_mic_deinterleave(int16_t *dest, const int16_t *src,
                                    unsigned int frames);
static int  bk7258_mic_capture_thread(int argc, char **argv);
static void bk7258_mic_flush_pending(struct bk7258_mic_dev_s *priv);
static void bk7258_mic_stop_thread(struct bk7258_mic_dev_s *priv);

/* Physical-board binding validation. */

static FAR const struct bk7258_mic_binding_s *
  bk7258_mic_get_binding(void);

/* audio_ops_s */

static int  bk7258_mic_getcaps(struct audio_lowerhalf_s *dev, int type,
                               struct audio_caps_s *caps);
#ifdef CONFIG_BK7258_AGENT_MEDIA_RECORDER
extern void bk7258_agent_media_recorder_link(void);
#endif
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

/* One on-board capture device: its negotiated stream may contain one or two
 * physical microphones.  The DMA finish ISR needs the static instance because
 * bk_dma_register_isr() takes no caller context.
 */

static struct bk7258_mic_dev_s g_bk7258_mic =
{
  .lock        = NXMUTEX_INITIALIZER,
  .worker_lock = NXMUTEX_INITIALIZER,
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

_Static_assert(CONFIG_BK7258_MIC_SAMPLE_RATE == BK7258_MIC_RATE_8000 ||
               CONFIG_BK7258_MIC_SAMPLE_RATE == BK7258_MIC_RATE_11025 ||
               CONFIG_BK7258_MIC_SAMPLE_RATE == BK7258_MIC_RATE_12000 ||
               CONFIG_BK7258_MIC_SAMPLE_RATE == BK7258_MIC_RATE_16000 ||
               CONFIG_BK7258_MIC_SAMPLE_RATE == BK7258_MIC_RATE_22050 ||
               CONFIG_BK7258_MIC_SAMPLE_RATE == BK7258_MIC_RATE_24000 ||
               CONFIG_BK7258_MIC_SAMPLE_RATE == BK7258_MIC_RATE_32000 ||
               CONFIG_BK7258_MIC_SAMPLE_RATE == BK7258_MIC_RATE_44100 ||
               CONFIG_BK7258_MIC_SAMPLE_RATE == BK7258_MIC_RATE_48000,
               "BK7258 MIC default sample rate must be supported by SDK");

_Static_assert(CONFIG_BK7258_MIC_DIG_GAIN <= BK7258_MIC_DIG_GAIN_MAX,
               "BK7258 MIC digital gain exceeds the SDK field");

_Static_assert(CONFIG_BK7258_MIC_DIG_GAIN >= BK7258_MIC_DIG_GAIN_MIN,
               "BK7258 MIC digital gain is below the SDK field");

_Static_assert(CONFIG_BK7258_MIC_ANA_GAIN <= BK7258_MIC_ANA_GAIN_MAX,
               "BK7258 MIC analog gain exceeds the hardware field");

_Static_assert(CONFIG_BK7258_MIC_ANA_GAIN >= BK7258_MIC_ANA_GAIN_MIN,
               "BK7258 MIC analog gain is below the hardware field");

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t bk7258_mic_dma_reg(dma_id_t id, uint32_t offset)
{
  uintptr_t address = SOC_GENER_DMA_REG_BASE +
                      (id / BK7258_MIC_DMA_UNIT_CHANNELS) *
                        BK7258_MIC_DMA_UNIT_STRIDE +
                      BK7258_MIC_DMA_CHANNEL_BASE +
                      (id % BK7258_MIC_DMA_UNIT_CHANNELS) *
                        BK7258_MIC_DMA_CHANNEL_STRIDE + offset;

  return *(volatile uint32_t *)address;
}

static bool bk7258_mic_dma_program_valid(struct bk7258_mic_dev_s *priv,
                                         uint32_t *ctrl_out,
                                         uint32_t *req_mux_out,
                                         uint32_t *dest_start_out)
{
  uint32_t ctrl = bk7258_mic_dma_reg(priv->dma_id,
                                    BK7258_MIC_DMA_CTRL_OFFSET);
  uint32_t req_mux = bk7258_mic_dma_reg(priv->dma_id,
                                       BK7258_MIC_DMA_REQ_MUX_OFFSET);
  uint32_t dest_start = bk7258_mic_dma_reg(
    priv->dma_id, BK7258_MIC_DMA_DEST_START_OFFSET);
  uint32_t expected_length = (BK7258_MIC_DMA_FRAME_BYTES - 1u) << 16;

  *ctrl_out = ctrl;
  *req_mux_out = req_mux;
  *dest_start_out = dest_start;

  return (ctrl & BK7258_MIC_DMA_CTRL_CONFIG_MASK) ==
           BK7258_MIC_DMA_CTRL_CONFIG_VALUE &&
         (ctrl & BK7258_MIC_DMA_CTRL_LENGTH_MASK) == expected_length &&
         (req_mux & BK7258_MIC_DMA_REQ_MUX_MASK) ==
           BK7258_MIC_DMA_REQ_MUX_VALUE &&
         dest_start == (uint32_t)(uintptr_t)priv->ring_mem;
}

static int bk7258_mic_dma_program(struct bk7258_mic_dev_s *priv,
                                  const dma_config_t *config)
{
  uint32_t ctrl = 0;
  uint32_t req_mux = 0;
  uint32_t dest_start = 0;
  unsigned int attempt;
  bk_err_t err;
  int ret = -EIO;

  for (attempt = 1; attempt <= BK7258_MIC_DMA_PROGRAM_ATTEMPTS; attempt++)
    {
      err = bk_dma_init(priv->dma_id, config);
      if (err != BK_OK)
        {
          ret = bk7258_mic_result(err);
          goto retry_deinit;
        }

      err = bk_dma_set_transfer_len(priv->dma_id,
                                    BK7258_MIC_DMA_FRAME_BYTES);
      if (err != BK_OK)
        {
          ret = bk7258_mic_result(err);
          goto retry_deinit;
        }

      err = bk_dma_set_src_sec_attr(priv->dma_id, DMA_ATTR_SEC);
      if (err != BK_OK)
        {
          ret = bk7258_mic_result(err);
          goto retry_deinit;
        }

      err = bk_dma_set_dest_sec_attr(priv->dma_id, DMA_ATTR_SEC);
      if (err != BK_OK)
        {
          ret = bk7258_mic_result(err);
          goto retry_deinit;
        }

      err = bk_dma_register_isr(priv->dma_id, NULL, bk7258_mic_dma_isr);
      if (err != BK_OK)
        {
          ret = bk7258_mic_result(err);
          goto retry_deinit;
        }

      err = bk_dma_enable_finish_interrupt(priv->dma_id);
      if (err != BK_OK)
        {
          ret = bk7258_mic_result(err);
          goto retry_deinit;
        }

      UP_DSB();
      if (bk7258_mic_dma_program_valid(priv, &ctrl, &req_mux, &dest_start))
        {
          if (attempt > 1)
            {
              audwarn("WARNING: DMA%u configuration recovered on attempt %u\n",
                      (unsigned int)priv->dma_id, attempt);
            }

          return OK;
        }

      ret = -EIO;
      audwarn("WARNING: DMA%u configuration readback failed on attempt %u: "
              "ctrl=%08" PRIx32 " req=%08" PRIx32 " dst=%08" PRIx32 "\n",
              (unsigned int)priv->dma_id, attempt, ctrl, req_mux, dest_start);

retry_deinit:
      bk_dma_deinit(priv->dma_id);

      if (attempt < BK7258_MIC_DMA_PROGRAM_ATTEMPTS)
        {
          UP_DSB();
          up_udelay(BK7258_MIC_DMA_RETRY_DELAY_US);
        }
    }

  auderr("ERROR: DMA%u configuration failed after %u attempts: %d\n",
         (unsigned int)priv->dma_id, BK7258_MIC_DMA_PROGRAM_ATTEMPTS, ret);
  return ret;
}

/****************************************************************************
 * Name: bk7258_mic_get_binding
 ****************************************************************************/

static FAR const struct bk7258_mic_binding_s *
  bk7258_mic_get_binding(void)
{
  FAR const struct bk7258_board_binding_s *board;
  FAR const struct bk7258_mic_binding_s *binding;
  FAR const struct bk7258_mic_config_s *config;

  board = bk7258_board_get_binding();
  if (board == NULL || board->version != BK7258_BINDING_VERSION ||
      board->size < sizeof(*board))
    {
      return NULL;
    }

  binding = board->mic;
  if (binding == NULL || binding->version != BK7258_BINDING_VERSION ||
      binding->size < sizeof(*binding) || binding->initialize == NULL)
    {
      return NULL;
    }

  config = binding->config;
  if (config == NULL || config->version != BK7258_BINDING_VERSION ||
      config->size < sizeof(*config) || config->channels < 1 ||
      config->channels > BK7258_MIC_FIFO_CHANNELS ||
      (config->flags & BK7258_MIC_BINDING_MIC1) == 0 ||
      ((config->flags & BK7258_MIC_BINDING_MIC2) != 0) !=
        (config->channels == 2))
    {
      return NULL;
    }

  return binding;
}

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

  ret = bk7258_media_root_initialize(BK7258_MEDIA_ROOT_DMA);
  if (ret < 0)
    {
      auderr("ERROR: shared DMA root init failed: %d\n", ret);
      return ret;
    }

  /* The current official onboard_mic_stream explicitly opens L/R for both
   * one- and two-channel formats because the DMA must carry each packed
   * 32-bit FIFO word.  Mono selection happens when the worker discards R.
   */

  cfg.adc_chl       = AUD_ADC_CHL_LR;
  cfg.samp_rate     = priv->samplerate;
  cfg.adc_gain      = priv->dig_gain;
  cfg.adc_mode      = AUD_ADC_MODE_DIFFEN;
  cfg.adc_samp_edge = AUD_ADC_SAMP_EDGE_RISING;

  /* Keep the converter on the SDK onboard-mic reference clock.  The AP
   * libdriver.a is built for the secure audio domain and its production
   * stream selects XTAL here; APLL is not a board-level tuning knob.
   */

  cfg.clk_src       = AUD_CLK_XTAL;

  /* bk_aud_adc_init() internally performs bk_aud_driver_init() (power vote,
   * PM_CLK_ID_AUDIO, INT_SRC_AUDIO registration, ANA_REG baseline) and
   * bk_aud_clk_config(), including the converter clock setup.
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

  err = bk_aud_adc_set_mic_mode(priv->channels == 2 ? AUD_MIC_BOTH :
                                                      AUD_MIC_MIC1,
                                AUD_ADC_MODE_DIFFEN);
  if (err != BK_OK)
    {
      auderr("ERROR: bk_aud_adc_set_mic_mode failed: %d\n", err);
      ret = bk7258_mic_result(err);
      goto err_adc;
    }

  /* ana_mic0 == ANA_REG19 == hardware MIC1 == MICP1/MICN1. */

  err = bk_aud_set_ana_mic0_gain(priv->ana_gain);
  if (err != BK_OK)
    {
      auderr("ERROR: bk_aud_set_ana_mic0_gain failed: %d\n", err);
      ret = bk7258_mic_result(err);
      goto err_adc;
    }

  /* ana_mic1 == ANA_REG27 == hardware MIC2 == MICP2/MICN2. */

  if (priv->channels == 2)
    {
      err = bk_aud_set_ana_mic1_gain(priv->ana_gain);
      if (err != BK_OK)
        {
          auderr("ERROR: bk_aud_set_ana_mic1_gain failed: %d\n", err);
          ret = bk7258_mic_result(err);
          goto err_adc;
        }
    }

  /* DMA destination ring, plus the de-interleave scratch frame. */

#ifdef CONFIG_BK7258_PSRAM
  /* Match the official onboard_mic_stream allocation domain.  Its
   * audio_dma_mem_calloc() resolves to psram_malloc() in the pinned AP SDK.
   * The NuttX kernel heap can extend into shared SMEM4 (0x2808xxxx), where
   * this DMA request drops enable before moving its destination pointer.
   * The board PSRAM heap is non-cacheable and is the SDK's intended audio
   * DMA storage on this profile.
   */

  priv->ring_mem = bk7258_psram_zalloc(BK7258_MIC_RING_BYTES);
#else
  priv->ring_mem = kmm_zalloc(BK7258_MIC_RING_BYTES);
#endif
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

  priv->dma_config = dma_cfg;

  /* This immutable AP libdriver.a was built with CONFIG_SPE=1: its DMA
   * driver registers INT_SRC_GDMA / INT_SRC_DMA1_SEC, and the official
   * onboard-mic stream marks both ends secure.  A NuttX-only conditional
   * cannot reproduce that archive-time setting; omitting these attributes
   * accepts setup but the audio FIFO never completes a transfer.
   */

  ret = bk7258_mic_dma_program(priv, &dma_cfg);
  if (ret < 0)
    {
      goto err_dma_alloc;
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

err_dma_alloc:
  bk_dma_free(DMA_DEV_AUDIO, priv->dma_id);
  priv->dma_allocated = false;

err_scratch:
  kmm_free(priv->scratch);
  priv->scratch = NULL;

err_ring_mem:
#ifdef CONFIG_BK7258_PSRAM
  bk7258_psram_free(priv->ring_mem);
#else
  kmm_free(priv->ring_mem);
#endif
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

#ifdef CONFIG_BK7258_PSRAM
  bk7258_psram_free(priv->ring_mem);
#else
  kmm_free(priv->ring_mem);
#endif
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
  uint32_t ctrl;
  uint32_t req_mux;
  uint32_t dest_start;
  bk_err_t err;
  int ret;

  /* Reset the read/write pointers and push the DMA pause address out to the
   * end of the ring; without this a resume replays stale pointers.
   */

  ring_buffer_clear(&priv->ring);

  /* The upper half creates its capture worker between hardware setup and
   * start.  Recheck at the last safe point before enabling the channel so a
   * late DMA-domain reset cannot turn into a three-second receive timeout.
   * A bad readback is repaired through the same public deinit/init sequence;
   * ring_buffer_clear() is repeated because deinit resets the pause address.
   */

  UP_DSB();
  if (!bk7258_mic_dma_program_valid(priv, &ctrl, &req_mux, &dest_start))
    {
      audwarn("WARNING: DMA%u configuration lost before start: "
              "ctrl=%08" PRIx32 " req=%08" PRIx32 " dst=%08" PRIx32 "\n",
              (unsigned int)priv->dma_id, ctrl, req_mux, dest_start);
      bk_dma_deinit(priv->dma_id);
      UP_DSB();
      up_udelay(BK7258_MIC_DMA_RETRY_DELAY_US);

      ret = bk7258_mic_dma_program(priv, &priv->dma_config);
      if (ret < 0)
        {
          return ret;
        }

      ring_buffer_clear(&priv->ring);
      UP_DSB();
      if (!bk7258_mic_dma_program_valid(priv, &ctrl, &req_mux, &dest_start))
        {
          auderr("ERROR: DMA%u configuration lost after start recovery: "
                 "ctrl=%08" PRIx32 " req=%08" PRIx32 " dst=%08" PRIx32
                 "\n", (unsigned int)priv->dma_id, ctrl, req_mux,
                 dest_start);
          return -EIO;
        }
    }

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

      /* Pause/stop serialize their hardware transition through worker_lock
       * and clear streaming before releasing it.  Recheck after taking the
       * lock so a pre-pause semaphore cannot produce a late callback.
       */

      nxmutex_lock(&priv->worker_lock);

      if (priv->terminate || !priv->streaming)
        {
          nxmutex_unlock(&priv->worker_lock);
          continue;
        }

      /* Only consume a frame once the DMA has actually landed one. */

      if (ring_buffer_get_fill_size(&priv->ring) <
          BK7258_MIC_DMA_FRAME_BYTES)
        {
          nxmutex_unlock(&priv->worker_lock);
          continue;
        }

      got = ring_buffer_read(&priv->ring, priv->scratch,
                             BK7258_MIC_DMA_FRAME_BYTES);
      if (got == 0)
        {
          nxmutex_unlock(&priv->worker_lock);
          continue;
        }

      /* Whole L/R pairs only. */

      frames = got / BK7258_MIC_FIFO_WORD_BYTES;
      if (frames == 0)
        {
          nxmutex_unlock(&priv->worker_lock);
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
          nxmutex_unlock(&priv->worker_lock);
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

      nxmutex_unlock(&priv->worker_lock);
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

        caps->ac_channels = g_bk7258_mic.config->channels;

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
        caps->ac_channels = g_bk7258_mic.config->channels;

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

          if (channels < 1 || channels > priv->config->channels)
            {
              auderr("ERROR: board %s supports 1..%u microphone channels, "
                     "got %u\n",
                     priv->config->variant_name != NULL ?
                       priv->config->variant_name : "unknown",
                     priv->config->channels, channels);
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

  if (!priv->reserved || !priv->audio_session_owned)
    {
      auderr("ERROR: start denied: reserved=%d audio_session=%d state=%d\n",
             priv->reserved, priv->audio_session_owned, priv->state);
      return -EACCES;
    }

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
  bool pending;

  DEBUGASSERT(priv != NULL);

  if (priv->state != BK7258_MIC_STATE_RUNNING &&
      priv->state != BK7258_MIC_STATE_PAUSED)
    {
      nxmutex_lock(&priv->lock);
      pending = dq_peek(&priv->pendq) != NULL;
      nxmutex_unlock(&priv->lock);
      if (pending)
        {
          nxmutex_lock(&priv->worker_lock);
          bk7258_mic_flush_pending(priv);
          nxmutex_unlock(&priv->worker_lock);

#ifdef CONFIG_AUDIO_MULTI_SESSION
          priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL,
                          OK, priv);
#else
          priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
#endif
        }

      return OK;
    }

  /* The worker's ring_buffer_read() also touches DMA producer/pause
   * registers.  Quiesce it before stopping those registers underneath it.
   */

  nxmutex_lock(&priv->worker_lock);
  priv->streaming = false;
  bk7258_mic_hw_stop(priv);
  nxmutex_unlock(&priv->worker_lock);

  /* Join before teardown: the thread dereferences the ring and the scratch
   * frame that bk7258_mic_hw_teardown() is about to free.
   */

  bk7258_mic_stop_thread(priv);
  bk7258_mic_hw_teardown(priv);

  priv->state = BK7258_MIC_STATE_CONFIGURED;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK, priv);
#else
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
#endif

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

  /* Serialize against ring_buffer_read() and the upper callback while the
   * DMA/ADC registers are stopped.  A pending completion is consumed after
   * unlock and ignored because streaming is false.
   */

  nxmutex_lock(&priv->worker_lock);
  priv->streaming = false;
  bk7258_mic_hw_stop(priv);
  priv->state = BK7258_MIC_STATE_PAUSED;
  nxmutex_unlock(&priv->worker_lock);

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

  /* Keep the worker out while stale completions and ring pointers from the
   * pre-pause stream are discarded and the hardware is restarted.
   */

  nxmutex_lock(&priv->worker_lock);

  while (nxsem_trywait(&priv->dmasem) >= 0);

  ret = bk7258_mic_hw_start(priv);
  if (ret < 0)
    {
      priv->streaming = false;
      nxmutex_unlock(&priv->worker_lock);
      return ret;
    }

  priv->streaming = true;
  priv->state = BK7258_MIC_STATE_RUNNING;
  nxmutex_unlock(&priv->worker_lock);
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

  /* audio_close() invokes shutdown while holding the upper-half spinlock.
   * The supported bridge always performs STOP and RELEASE first; that path
   * publishes close_safe only after workers, buffers and the AP-wide AUD
   * session owner are gone.  Never take a sleeping mutex from last-close.
   */

  return __atomic_load_n(&priv->close_safe, __ATOMIC_ACQUIRE) ? OK :
         -EBUSY;
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
  struct bk7258_mic_dev_s *priv = (struct bk7258_mic_dev_s *)dev;
  int ret = OK;

  if (priv == NULL)
    {
      return -EINVAL;
    }

  switch (cmd)
    {
#ifdef CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS
      case AUDIOIOC_GETBUFFERINFO:
        {
          struct ap_buffer_info_s *info = (struct ap_buffer_info_s *)arg;

          if (info == NULL)
            {
              return -EINVAL;
            }

          info->buffer_size = BK7258_MIC_FRAME_BYTES * priv->channels;
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
  int ret;

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

  ret = bk7258_media_audio_session_acquire(BK7258_MEDIA_AUDIO_MIC);
  if (ret < 0)
    {
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  priv->reserved = true;
  priv->audio_session_owned = true;
  __atomic_store_n(&priv->close_safe, false, __ATOMIC_RELEASE);
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
  int ret;

  DEBUGASSERT(priv != NULL);

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (session != priv)
    {
      return -EINVAL;
    }
#endif

  if (priv->state == BK7258_MIC_STATE_RUNNING ||
      priv->state == BK7258_MIC_STATE_PAUSED)
    {
#ifdef CONFIG_AUDIO_MULTI_SESSION
      ret = bk7258_mic_stop(dev, session);
#else
      ret = bk7258_mic_stop(dev);
#endif
      if (ret < 0)
        {
          return ret;
        }
    }

  nxmutex_lock(&priv->lock);

  if (!priv->reserved && !priv->audio_session_owned)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  /* Close the enqueue gate before draining.  Otherwise an enqueue can land
   * after flush_pending() observes an empty queue but before reserved is
   * cleared, leaving a buffer that no owner can ever recover.
   */

  priv->reserved = false;
  nxmutex_unlock(&priv->lock);

  /* Join a buffer already removed from pendq by the capture worker, then
   * keep the worker excluded while every remaining buffer is returned.  No
   * new enqueue can pass the gate above, and the worker cannot race the
   * final drain by reacquiring worker_lock between a join and the flush.
   */

  nxmutex_lock(&priv->worker_lock);
  bk7258_mic_flush_pending(priv);
  nxmutex_unlock(&priv->worker_lock);

  ret = bk7258_media_audio_session_release(BK7258_MEDIA_AUDIO_MIC);
  if (ret == OK)
    {
      nxmutex_lock(&priv->lock);
      priv->audio_session_owned = false;
      __atomic_store_n(&priv->close_safe, true, __ATOMIC_RELEASE);
      nxmutex_unlock(&priv->lock);
    }

  return ret;
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
  FAR const struct bk7258_mic_binding_s *binding;
  int ret;

#ifdef CONFIG_BK7258_AGENT_MEDIA_RECORDER
  bk7258_agent_media_recorder_link();
#endif

  if (g_bk7258_mic_registered)
    {
      return OK;
    }

  priv->dev.ops    = &g_bk7258_mic_ops;
  priv->samplerate = CONFIG_BK7258_MIC_SAMPLE_RATE;
  __atomic_store_n(&priv->close_safe, true, __ATOMIC_RELEASE);

  binding = bk7258_mic_get_binding();
  if (binding == NULL)
    {
      auderr("ERROR: BK7258 microphone board binding is unavailable\n");
      return -ENODEV;
    }

  ret = binding->initialize();
  if (ret < 0)
    {
      auderr("ERROR: BK7258 microphone board binding initialization failed: %d\n",
             ret);
      return ret;
    }

  priv->binding   = binding;
  priv->config    = binding->config;
  priv->channels  = binding->config->channels;
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
         "BMIC BOOT PASS board=%s dev=/dev/audio/%s rate=%u max_ch=%u "
         "dig=0x%02x ana=0x%02x\n",
         priv->config->variant_name != NULL ? priv->config->variant_name :
           "unknown",
         CONFIG_BK7258_MIC_DEVNAME, (unsigned)priv->samplerate,
         (unsigned)priv->channels, (unsigned)priv->dig_gain,
         (unsigned)priv->ana_gain);

#ifdef CONFIG_BK7258_MIC_LIFECYCLE_VALIDATION
  ret = bk7258_mic_validation_start();
  if (ret < 0)
    {
      syslog(LOG_ERR, "BMICVAL FAIL stage=worker ret=%d\n", ret);
    }
#endif

  return OK;
}

#endif /* CONFIG_BK7258_MIC */
