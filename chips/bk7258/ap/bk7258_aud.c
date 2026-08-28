/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/ap/bk7258_aud.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 on-board analog speaker playback lower half.
 *
 * The immutable AP SDK supplies the AUD DAC, general DMA and audio ring
 * buffer implementation.  This wrapper owns only the DAC submodule and one
 * DMA channel; the selected board owns the external power-amplifier GPIO.
 *
 * Data path:
 *
 *   NuttX ap_buffer_s queue
 *     -> task-context frame assembler
 *       -> two-frame DTCM ring (+ the SDK's eight-byte pause guard)
 *         -> repeat DMA, one interrupt per frame
 *           -> AUD DAC FIFO
 *
 * The DMA interrupt only posts a semaphore.  RingBufferContext accesses DMA
 * registers and therefore remains in task context.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AUD

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/audio/audio.h>
#include <nuttx/clock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/percpu.h>
#include <nuttx/queue.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_aud.h>
#include <arch/chip/bk7258_pm.h>

/* Do not include the legacy driver/aud.h or driver/aud_types.h.  Their DAC
 * work-mode enumeration contradicts the modular aud_dac_types.h used by the
 * actual v3.1.1.9 implementation.
 */

#include <driver/aud_common.h>
#include <driver/aud_dac.h>
#include <driver/audio_ring_buff.h>
#include <driver/dma.h>

#ifdef CONFIG_BK7258_AUD_DAC_EQ
#  include "arm_internal.h"
#endif
#include "bk7258_media_root.h"

#ifdef CONFIG_BK7258_AGENT_MEDIA_PLAYER
extern void bk7258_agent_media_player_link(void);
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_AUD_SAMPLE_RATE
#  define CONFIG_BK7258_AUD_SAMPLE_RATE 16000
#endif

#ifndef CONFIG_BK7258_AUD_DIG_GAIN
#  define CONFIG_BK7258_AUD_DIG_GAIN 45
#endif

#ifndef CONFIG_BK7258_AUD_ANA_GAIN
#  define CONFIG_BK7258_AUD_ANA_GAIN 10
#endif

#ifndef CONFIG_BK7258_AUD_FRAME_SAMPLES
#  define CONFIG_BK7258_AUD_FRAME_SAMPLES 320
#endif

#ifndef CONFIG_BK7258_AUD_QUEUE_DEPTH
#  define CONFIG_BK7258_AUD_QUEUE_DEPTH 8
#endif

#ifndef CONFIG_BK7258_AUD_WORKER_PRIORITY
#  define CONFIG_BK7258_AUD_WORKER_PRIORITY 245
#endif

#define BK7258_AUD_WORKER_STACKSIZE      3072

#define BK7258_AUD_DMA_RING_FRAMES       2u
#define BK7258_AUD_DMA_RING_GUARD_BYTES  8u
#define BK7258_AUD_FIFO_BYTES            4u
#define BK7258_AUD_REQUIRED_CPU_HZ        480000000u

#define BK7258_AUD_FRAME_BYTES \
  (CONFIG_BK7258_AUD_FRAME_SAMPLES * BK7258_AUD_BYTES_PER_SAMPLE)

#define BK7258_AUD_RING_BYTES \
  ((BK7258_AUD_FRAME_BYTES * BK7258_AUD_DMA_RING_FRAMES) + \
   BK7258_AUD_DMA_RING_GUARD_BYTES)

#ifdef CONFIG_BK7258_AUD_DAC_EQ
#  define BK7258_AUD_EQ_ENABLE_REG       0x47800060u
#  define BK7258_AUD_EQ_ENABLE_BIT       (1u << 2)
#  define BK7258_AUD_EQ_BANK_REG_BASE    0x47800080u
#  define BK7258_AUD_EQ_BANK_REG_STRIDE  0x0cu
#  define BK7258_AUD_EQ_A1A2_REG_OFFSET  0x00u
#  define BK7258_AUD_EQ_B0B1_REG_OFFSET  0x04u
#  define BK7258_AUD_EQ_B2_REG_OFFSET    0x08u
#  define BK7258_AUD_EQ_EXT_REG_BASE     0x478000b0u
#  define BK7258_AUD_EQ_EXT_REG_STRIDE   0x04u
#  define BK7258_AUD_EQ_RAW_MASK         0x003fffffu
#  define BK7258_AUD_EQ_HIGH_MASK        0x0000ffffu
#  define BK7258_AUD_EQ_LOW_MASK         0x0000003fu
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum bk7258_aud_state_e
{
  BK7258_AUD_STATE_RESET = 0,
  BK7258_AUD_STATE_RESERVED,
  BK7258_AUD_STATE_CONFIGURED,
  BK7258_AUD_STATE_STARTING,
  BK7258_AUD_STATE_RUNNING,
  BK7258_AUD_STATE_DRAINED,
  BK7258_AUD_STATE_STOPPING,
  BK7258_AUD_STATE_FAULT
};

/* An APB can end anywhere within a physical DMA frame.  Delay its DEQUEUE
 * callback until that frame has actually reached the DAC FIFO.
 */

struct bk7258_aud_frame_s
{
  struct ap_buffer_s *done[CONFIG_BK7258_AUD_QUEUE_DEPTH];
  uint8_t             ndone;
  bool                final;
  uint32_t            payload_bytes;
};

struct bk7258_aud_dev_s
{
  struct audio_lowerhalf_s dev;       /* Must be first */

  /* Immutable physical-board contract supplied by board bring-up. */

  FAR const struct bk7258_aud_board_s *board;
  FAR const struct bk7258_aud_config_s *board_config;

  mutex_t lock;                       /* State and APB ownership */
  mutex_t worker_lock;                /* DMA/ring control plane */
  sem_t   dmasem;                     /* Posted by finish ISR */
  sem_t   donesem;                    /* Posted when worker exits */

  struct dq_queue_s pendq;
  struct ap_buffer_s *active;
  uint16_t outstanding;

  uint32_t samplerate;
  uint8_t  channels;
  uint8_t  bits;
  uint8_t  dig_gain;
  uint8_t  ana_gain;
  bool     muted;

#ifdef CONFIG_BK7258_AUD_DAC_EQ
  /* worker_lock serializes the EQ shadow and SDK/register control path.
   * Mutations also take lock when publishing resources or diagnostics.
   */

  struct bk7258_aud_eq_config_s eq_config;
  bool     eq_shadow_valid;
  bool     eq_cleanup_needed;
  bool     eq_applied;
#endif

  dma_id_t dma_id;
  bool     dma_allocated;
  bool     dma_initialized;
  bool     dma_irq_enabled;
  bool     dma_started;
  bool     dac_initialized;
  bool     dac_started;
  bool     ring_valid;
  bool     pa_enabled;
  bool     frequency_voted;
  bool     frequency_uncertain;
  bool     audio_session_owned;

  uint8_t          *ring_mem;
  uint8_t          *scratch;
  RingBufferContext ring;
  struct bk7258_aud_frame_s frames[BK7258_AUD_DMA_RING_FRAMES];
  uint32_t consume_sequence;
  uint32_t produce_sequence;
  volatile uint32_t isr_sequence;
  uint32_t isr_last_cycle[BK7258_AUD_DIAG_CPU_SLOTS];
  clock_t  isr_last_tick[BK7258_AUD_DIAG_CPU_SLOTS];
  uint8_t  drain_remaining;

  pid_t         pid;
  volatile bool terminate;
  volatile bool streaming;

  enum bk7258_aud_state_e state;
  bool close_safe;                     /* Atomic last-close fast path */
  bool reserved;
  bool configured;
  bool final_queued;
  bool complete_sent;

  struct bk7258_aud_diag_s diag;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_aud_getcaps(struct audio_lowerhalf_s *dev, int type,
                              struct audio_caps_s *caps);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_configure(struct audio_lowerhalf_s *dev,
                                void *session,
                                const struct audio_caps_s *caps);
static int bk7258_aud_start(struct audio_lowerhalf_s *dev, void *session);
static int bk7258_aud_stop(struct audio_lowerhalf_s *dev, void *session);
static int bk7258_aud_pause(struct audio_lowerhalf_s *dev, void *session);
static int bk7258_aud_resume(struct audio_lowerhalf_s *dev, void *session);
static int bk7258_aud_reserve(struct audio_lowerhalf_s *dev,
                              void **psession);
static int bk7258_aud_release(struct audio_lowerhalf_s *dev,
                              void *session);
#else
static int bk7258_aud_configure(struct audio_lowerhalf_s *dev,
                                const struct audio_caps_s *caps);
static int bk7258_aud_start(struct audio_lowerhalf_s *dev);
static int bk7258_aud_stop(struct audio_lowerhalf_s *dev);
static int bk7258_aud_pause(struct audio_lowerhalf_s *dev);
static int bk7258_aud_resume(struct audio_lowerhalf_s *dev);
static int bk7258_aud_reserve(struct audio_lowerhalf_s *dev);
static int bk7258_aud_release(struct audio_lowerhalf_s *dev);
#endif
static int bk7258_aud_shutdown(struct audio_lowerhalf_s *dev);
static int bk7258_aud_enqueuebuffer(struct audio_lowerhalf_s *dev,
                                    struct ap_buffer_s *apb);
static int bk7258_aud_cancelbuffer(struct audio_lowerhalf_s *dev,
                                   struct ap_buffer_s *apb);
static int bk7258_aud_ioctl(struct audio_lowerhalf_s *dev, int cmd,
                            unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct audio_ops_s g_bk7258_aud_ops =
{
  .getcaps       = bk7258_aud_getcaps,
  .configure     = bk7258_aud_configure,
  .shutdown      = bk7258_aud_shutdown,
  .start         = bk7258_aud_start,
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  .stop          = bk7258_aud_stop,
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
  .pause         = bk7258_aud_pause,
  .resume        = bk7258_aud_resume,
#endif
  .allocbuffer   = NULL,
  .freebuffer    = NULL,
  .enqueuebuffer = bk7258_aud_enqueuebuffer,
  .cancelbuffer  = bk7258_aud_cancelbuffer,
  .ioctl         = bk7258_aud_ioctl,
  .read          = NULL,
  .write         = NULL,
  .reserve       = bk7258_aud_reserve,
  .release       = bk7258_aud_release,
};

static struct bk7258_aud_dev_s g_bk7258_aud =
{
  .lock        = NXMUTEX_INITIALIZER,
  .worker_lock = NXMUTEX_INITIALIZER,
  .dma_id      = DMA_ID_MAX,
  .pid         = -1,
};

static bool g_bk7258_aud_registered;
_Static_assert((BK7258_AUD_FRAME_BYTES % sizeof(uint32_t)) == 0,
               "BK7258 AUD DMA frame must be 32-bit aligned");
_Static_assert(CONFIG_BK7258_AUD_QUEUE_DEPTH == 8,
               "BK7258 AUD only validates the eight-buffer playback tuple");
_Static_assert(CONFIG_BK7258_AUD_QUEUE_DEPTH <= UINT8_MAX,
               "BK7258 AUD queue count must fit in uint8_t");
_Static_assert(CONFIG_BK7258_AUD_DIG_GAIN <= 0x3f,
               "BK7258 AUD digital gain exceeds DAC field");
_Static_assert(CONFIG_BK7258_AUD_ANA_GAIN <= 0x0f,
               "BK7258 AUD analog gain exceeds DAC field");
_Static_assert(CONFIG_BK7258_AUD_SAMPLE_RATE == 16000,
               "BK7258 AUD only validates the 16-kHz playback tuple");
_Static_assert(CONFIG_BK7258_AUD_FRAME_SAMPLES == 320,
               "BK7258 AUD only validates 320-sample DMA frames");
#ifdef CONFIG_BK7258_AUD_DAC_EQ
_Static_assert(sizeof(struct bk7258_aud_eq_config_s) == 88,
               "BK7258 AUD EQ private ABI must remain fixed-width");
_Static_assert(offsetof(struct bk7258_aud_eq_config_s, version) == 0 &&
               offsetof(struct bk7258_aud_eq_config_s, size) == 2 &&
               offsetof(struct bk7258_aud_eq_config_s, flags) == 4 &&
               offsetof(struct bk7258_aud_eq_config_s, coeff) == 8,
               "BK7258 AUD EQ private ABI layout changed");
_Static_assert(sizeof(unsigned long) >= sizeof(uintptr_t),
               "BK7258 AUD EQ ioctl cannot carry a pointer");
_Static_assert(_IOC_TYPE(BK7258_AUDIOIOC_SET_DAC_EQ) == _AUDIOIOCBASE &&
               _IOC_NR(BK7258_AUDIOIOC_SET_DAC_EQ) == 0x80,
               "BK7258 AUD EQ private ioctl encoding changed");
_Static_assert(sizeof(aud_dac_eq_config_t) == 80,
               "BK7258 SDK DAC EQ ABI changed");
_Static_assert(sizeof(struct bk7258_aud_diag_s) == 0x138,
               "BK7258 AUD diagnostic v6 ABI changed");
#else
_Static_assert(sizeof(struct bk7258_aud_diag_s) == 0x110,
               "BK7258 AUD diagnostic v5 ABI changed");
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bk7258_aud_board_valid(
  FAR const struct bk7258_aud_board_s *board)
{
  FAR const struct bk7258_aud_config_s *config;

  if (board == NULL || board->initialize == NULL || board->set == NULL ||
      board->is_enabled == NULL)
    {
      return false;
    }

  config = board->config;
  if (config == NULL || config->variant_name == NULL ||
      config->speaker_control_gpio >= BK7258_AUD_GPIO_COUNT ||
      config->speaker_on_delay_ms > UINT32_MAX / 1000u ||
      config->speaker_off_delay_ms > UINT32_MAX / 1000u)
    {
      return false;
    }

  return true;
}

static int bk7258_aud_result(bk_err_t error)
{
  return error == BK_OK ? OK : -EIO;
}

static bool bk7258_aud_rate_supported(uint32_t rate)
{
  return rate == CONFIG_BK7258_AUD_SAMPLE_RATE;
}

static uint32_t bk7258_aud_tick_delta(struct bk7258_aud_dev_s *priv,
                                      clock_t later, clock_t earlier)
{
  if (later < earlier)
    {
      __atomic_add_fetch(&priv->diag.tick_regression_count, 1u,
                         __ATOMIC_RELAXED);
      return 0;
    }

  return (uint32_t)(later - earlier);
}

static uint32_t bk7258_aud_diag_state(enum bk7258_aud_state_e state)
{
  switch (state)
    {
      case BK7258_AUD_STATE_RESET:
        return BK7258_AUD_DIAG_RESET;

      case BK7258_AUD_STATE_RESERVED:
        return BK7258_AUD_DIAG_RESERVED;

      case BK7258_AUD_STATE_CONFIGURED:
      case BK7258_AUD_STATE_STARTING:
      case BK7258_AUD_STATE_STOPPING:
        return BK7258_AUD_DIAG_CONFIGURED;

      case BK7258_AUD_STATE_RUNNING:
        return BK7258_AUD_DIAG_RUNNING;

      case BK7258_AUD_STATE_DRAINED:
        return BK7258_AUD_DIAG_DRAINED;

      default:
        return BK7258_AUD_DIAG_FAULT;
    }
}

static void bk7258_aud_set_state(struct bk7258_aud_dev_s *priv,
                                 enum bk7258_aud_state_e state)
{
  priv->state = state;
  priv->diag.state = bk7258_aud_diag_state(state);
}

static void bk7258_aud_record_error(struct bk7258_aud_dev_s *priv,
                                    int error)
{
  if (error < 0 && priv->diag.first_error == 0)
    {
      priv->diag.first_error = error;
    }
}

static bool bk7258_aud_resources_owned(struct bk7258_aud_dev_s *priv)
{
  bool owned;

  owned = priv->dac_initialized || priv->dma_allocated ||
          priv->dma_initialized || priv->dma_irq_enabled ||
          priv->dma_started || priv->dac_started || priv->ring_valid ||
          priv->pa_enabled || priv->ring_mem != NULL ||
          priv->scratch != NULL;
#ifdef CONFIG_BK7258_AUD_DAC_EQ
  owned = owned || priv->eq_cleanup_needed;
#endif

  return owned;
}

#ifdef CONFIG_BK7258_AUD_DAC_EQ

static uint32_t
bk7258_aud_eq_hash(const struct bk7258_aud_eq_config_s *config)
{
  const uint8_t *bytes = (const uint8_t *)config;
  uint32_t hash = 2166136261u;
  unsigned int i;

  for (i = 0; i < sizeof(*config); i++)
    {
      hash ^= bytes[i];
      hash *= 16777619u;
    }

  return hash;
}

static int
bk7258_aud_eq_validate(const struct bk7258_aud_eq_config_s *config)
{
  unsigned int coefficient;
  unsigned int bank;

  if (config->version != BK7258_AUD_EQ_CONFIG_VERSION ||
      config->size != sizeof(*config) ||
      (config->flags & ~BK7258_AUD_EQ_FLAG_MASK) != 0)
    {
      return -EINVAL;
    }

  for (bank = 0; bank < BK7258_AUD_EQ_BANK_COUNT; bank++)
    {
      for (coefficient = 0; coefficient < BK7258_AUD_EQ_COEFF_COUNT;
           coefficient++)
        {
          if (config->coeff[bank][coefficient] <
                BK7258_AUD_EQ_COEFF_MIN ||
              config->coeff[bank][coefficient] >
                BK7258_AUD_EQ_COEFF_MAX)
            {
              return -ERANGE;
            }
        }
    }

  return OK;
}

/* Both driver locks are held when a session releases its shadow.  Keep the
 * last accepted hash and cumulative counters for post-mortem diagnostics,
 * but never let a later opener inherit another session's requested filter.
 */

static void
bk7258_aud_eq_clear_shadow_locked(struct bk7258_aud_dev_s *priv)
{
  memset(&priv->eq_config, 0, sizeof(priv->eq_config));
  priv->eq_shadow_valid = false;
  priv->diag.eq_shadow_valid = 0;
  priv->diag.eq_requested = 0;
}

static void
bk7258_aud_eq_build_sdk_config(
  const struct bk7258_aud_eq_config_s *source,
  aud_dac_eq_config_t *target)
{
  target->flt0_A1 = source->coeff[0][BK7258_AUD_EQ_COEFF_A1];
  target->flt0_A2 = source->coeff[0][BK7258_AUD_EQ_COEFF_A2];
  target->flt0_B0 = source->coeff[0][BK7258_AUD_EQ_COEFF_B0];
  target->flt0_B1 = source->coeff[0][BK7258_AUD_EQ_COEFF_B1];
  target->flt0_B2 = source->coeff[0][BK7258_AUD_EQ_COEFF_B2];
  target->flt1_A1 = source->coeff[1][BK7258_AUD_EQ_COEFF_A1];
  target->flt1_A2 = source->coeff[1][BK7258_AUD_EQ_COEFF_A2];
  target->flt1_B0 = source->coeff[1][BK7258_AUD_EQ_COEFF_B0];
  target->flt1_B1 = source->coeff[1][BK7258_AUD_EQ_COEFF_B1];
  target->flt1_B2 = source->coeff[1][BK7258_AUD_EQ_COEFF_B2];
  target->flt2_A1 = source->coeff[2][BK7258_AUD_EQ_COEFF_A1];
  target->flt2_A2 = source->coeff[2][BK7258_AUD_EQ_COEFF_A2];
  target->flt2_B0 = source->coeff[2][BK7258_AUD_EQ_COEFF_B0];
  target->flt2_B1 = source->coeff[2][BK7258_AUD_EQ_COEFF_B1];
  target->flt2_B2 = source->coeff[2][BK7258_AUD_EQ_COEFF_B2];
  target->flt3_A1 = source->coeff[3][BK7258_AUD_EQ_COEFF_A1];
  target->flt3_A2 = source->coeff[3][BK7258_AUD_EQ_COEFF_A2];
  target->flt3_B0 = source->coeff[3][BK7258_AUD_EQ_COEFF_B0];
  target->flt3_B1 = source->coeff[3][BK7258_AUD_EQ_COEFF_B1];
  target->flt3_B2 = source->coeff[3][BK7258_AUD_EQ_COEFF_B2];
}

/* Verify the SDK's complete hardware contract rather than treating its
 * return value as proof.  Each signed-22 coefficient is split into a 16-bit
 * high field and a six-bit extension field.  Reserved register bits are not
 * part of the contract and are deliberately ignored.
 */

static int bk7258_aud_eq_verify_registers(
  struct bk7258_aud_dev_s *priv,
  const struct bk7258_aud_eq_config_s *config,
  bool enabled)
{
  uint32_t actual[BK7258_AUD_EQ_COEFF_COUNT];
  uintptr_t bank_address;
  uintptr_t ext_address;
  uint32_t expected;
  uint32_t a1a2;
  uint32_t b0b1;
  uint32_t b2;
  uint32_t ext;
  unsigned int coefficient;
  unsigned int bank;
  bool mismatch;

  mismatch = ((getreg32(BK7258_AUD_EQ_ENABLE_REG) &
               BK7258_AUD_EQ_ENABLE_BIT) != 0) != enabled;

  for (bank = 0; bank < BK7258_AUD_EQ_BANK_COUNT; bank++)
    {
      bank_address = BK7258_AUD_EQ_BANK_REG_BASE +
                     bank * BK7258_AUD_EQ_BANK_REG_STRIDE;
      ext_address = BK7258_AUD_EQ_EXT_REG_BASE +
                    bank * BK7258_AUD_EQ_EXT_REG_STRIDE;
      a1a2 = getreg32(bank_address + BK7258_AUD_EQ_A1A2_REG_OFFSET);
      b0b1 = getreg32(bank_address + BK7258_AUD_EQ_B0B1_REG_OFFSET);
      b2 = getreg32(bank_address + BK7258_AUD_EQ_B2_REG_OFFSET);
      ext = getreg32(ext_address);

      actual[BK7258_AUD_EQ_COEFF_A1] =
        ((a1a2 & BK7258_AUD_EQ_HIGH_MASK) << 6) |
        ((ext >> (BK7258_AUD_EQ_COEFF_A1 * 6u)) &
         BK7258_AUD_EQ_LOW_MASK);
      actual[BK7258_AUD_EQ_COEFF_A2] =
        (((a1a2 >> 16) & BK7258_AUD_EQ_HIGH_MASK) << 6) |
        ((ext >> (BK7258_AUD_EQ_COEFF_A2 * 6u)) &
         BK7258_AUD_EQ_LOW_MASK);
      actual[BK7258_AUD_EQ_COEFF_B0] =
        ((b0b1 & BK7258_AUD_EQ_HIGH_MASK) << 6) |
        ((ext >> (BK7258_AUD_EQ_COEFF_B0 * 6u)) &
         BK7258_AUD_EQ_LOW_MASK);
      actual[BK7258_AUD_EQ_COEFF_B1] =
        (((b0b1 >> 16) & BK7258_AUD_EQ_HIGH_MASK) << 6) |
        ((ext >> (BK7258_AUD_EQ_COEFF_B1 * 6u)) &
         BK7258_AUD_EQ_LOW_MASK);
      actual[BK7258_AUD_EQ_COEFF_B2] =
        ((b2 & BK7258_AUD_EQ_HIGH_MASK) << 6) |
        ((ext >> (BK7258_AUD_EQ_COEFF_B2 * 6u)) &
         BK7258_AUD_EQ_LOW_MASK);

      for (coefficient = 0; coefficient < BK7258_AUD_EQ_COEFF_COUNT;
           coefficient++)
        {
          expected = config == NULL ? 0u :
                     ((uint32_t)config->coeff[bank][coefficient] &
                      BK7258_AUD_EQ_RAW_MASK);
          mismatch = mismatch || actual[coefficient] != expected;
        }
    }

  nxmutex_lock(&priv->lock);
  if (mismatch)
    {
      priv->diag.eq_readback_fail_count++;
    }
  else
    {
      priv->diag.eq_readback_count++;
    }

  nxmutex_unlock(&priv->lock);
  return mismatch ? -EIO : OK;
}

/* worker_lock is held and the DAC is stopped but still initialized.  Keep
 * ownership until both the SDK operation and the complete zero readback
 * succeed; an uncertain filter must block DAC deinit.
 */

static int bk7258_aud_eq_deconfig(struct bk7258_aud_dev_s *priv)
{
  int verify_ret;
  bk_err_t error;

  if (!priv->eq_cleanup_needed)
    {
      return OK;
    }

  error = bk_aud_dac_eq_deconfig();
  verify_ret = bk7258_aud_eq_verify_registers(priv, NULL, false);
  if (error != BK_OK)
    {
      return bk7258_aud_result(error);
    }

  if (verify_ret < 0)
    {
      return verify_ret;
    }

  nxmutex_lock(&priv->lock);
  priv->eq_cleanup_needed = false;
  priv->eq_applied = false;
  priv->diag.eq_applied = 0;
  priv->diag.eq_deconfig_count++;
  priv->diag.resources &= ~BK7258_AUD_RESOURCE_EQ;
  nxmutex_unlock(&priv->lock);
  return OK;
}

/* worker_lock owns this SDK control path.  Publish cleanup ownership before
 * entering the immutable SDK so every failure after the first register may
 * have changed is unwound by the ordinary START teardown path.
 */

static int bk7258_aud_eq_apply(struct bk7258_aud_dev_s *priv)
{
  struct bk7258_aud_eq_config_s config;
  aud_dac_eq_config_t sdk_config;
  int ret;
  bk_err_t error;

  if (!priv->eq_shadow_valid ||
      (priv->eq_config.flags & BK7258_AUD_EQ_FLAG_ENABLE) == 0)
    {
      return OK;
    }

  if (!priv->dac_initialized || priv->dac_started ||
      priv->eq_cleanup_needed)
    {
      return -EBUSY;
    }

  memcpy(&config, &priv->eq_config, sizeof(config));
  nxmutex_lock(&priv->lock);
  priv->eq_cleanup_needed = true;
  priv->diag.resources |= BK7258_AUD_RESOURCE_EQ;
  nxmutex_unlock(&priv->lock);

  bk7258_aud_eq_build_sdk_config(&config, &sdk_config);
  error = bk_aud_dac_eq_config(&sdk_config);
  if (error != BK_OK)
    {
      ret = bk7258_aud_result(error);
    }
  else
    {
      ret = bk7258_aud_eq_verify_registers(priv, &config, true);
    }

  if (ret < 0)
    {
      /* Preserve the configuration/readback error while making one immediate
       * best-effort rollback.  A failed rollback retains ownership for the
       * ordinary START error teardown to retry.
       */

      (void)bk7258_aud_eq_deconfig(priv);
      return ret;
    }

  nxmutex_lock(&priv->lock);
  priv->eq_applied = true;
  priv->diag.eq_applied = 1;
  priv->diag.eq_apply_count++;
  nxmutex_unlock(&priv->lock);
  return OK;
}

static int bk7258_aud_eq_reject(struct bk7258_aud_dev_s *priv, int error)
{
  nxmutex_lock(&priv->lock);
  priv->diag.eq_reject_count++;
  nxmutex_unlock(&priv->lock);
  return error;
}

static int bk7258_aud_eq_set_config(struct bk7258_aud_dev_s *priv,
                                    unsigned long arg)
{
  const struct bk7258_aud_eq_config_s *user_config;
  struct bk7258_aud_eq_config_s config;
  uint32_t hash;
  int ret;

  user_config = (const struct bk7258_aud_eq_config_s *)(uintptr_t)arg;
  if (user_config == NULL)
    {
      return bk7258_aud_eq_reject(priv, -EINVAL);
    }

  /* Reject a shorter or foreign version before reading past its fixed
   * header.  As with the standard audio pointer ioctls, flat-build pointer
   * validity is the caller's responsibility.
   */

  if (user_config->version != BK7258_AUD_EQ_CONFIG_VERSION ||
      user_config->size != sizeof(config))
    {
      return bk7258_aud_eq_reject(priv, -EINVAL);
    }

  /* Never retain or validate through the caller's pointer. */

  memcpy(&config, user_config, sizeof(config));
  ret = bk7258_aud_eq_validate(&config);
  if (ret < 0)
    {
      return bk7258_aud_eq_reject(priv, ret);
    }

  hash = bk7258_aud_eq_hash(&config);

  nxmutex_lock(&priv->worker_lock);
  nxmutex_lock(&priv->lock);
  if (!priv->reserved)
    {
      ret = -EACCES;
    }
  else if ((priv->state != BK7258_AUD_STATE_RESERVED &&
            priv->state != BK7258_AUD_STATE_CONFIGURED) ||
           bk7258_aud_resources_owned(priv))
    {
      ret = -EBUSY;
    }
  else
    {
      memcpy(&priv->eq_config, &config, sizeof(priv->eq_config));
      priv->eq_shadow_valid = true;
      priv->diag.eq_shadow_valid = 1;
      priv->diag.eq_requested =
        (config.flags & BK7258_AUD_EQ_FLAG_ENABLE) != 0;
      priv->diag.eq_config_count++;
      priv->diag.eq_last_config_hash = hash;
      ret = OK;
    }

  if (ret < 0)
    {
      priv->diag.eq_reject_count++;
    }

  nxmutex_unlock(&priv->lock);
  nxmutex_unlock(&priv->worker_lock);
  return ret;
}

#endif /* CONFIG_BK7258_AUD_DAC_EQ */

/* The official v3.1.1.9 on-board speaker stream holds PM_DEV_ID_AUDIO at
 * 480 MHz from open through close.  Use the stable board-owned client ID,
 * not the role-dependent SDK enum, and keep this vote outside the stream
 * resource predicate: STOP tears down DMA/DAC but a reserved NuttX session
 * continues to own its frequency vote until RELEASE.
 */

static int bk7258_aud_frequency_acquire(struct bk7258_aud_dev_s *priv)
{
  uint32_t before;
  uint32_t active;
  int ret;

  before = (uint32_t)perf_getfreq();
  ret = bk7258_pm_frequency_vote(BK7258_PM_FREQ_CLIENT_AUDIO,
                                  BK7258_PM_OPP_480M);
  if (ret < 0)
    {
      /* The PM transaction may already have committed on CP before AP's
       * post-reply timebase repair reports an error.  Treat every failed
       * vote as uncertain ownership and force an idempotent DEFAULT
       * compensation instead of assuming that no remote state changed.
       */

      nxmutex_lock(&priv->lock);
      priv->frequency_uncertain = true;
      priv->diag.resources |= BK7258_AUD_RESOURCE_FREQUENCY;
      bk7258_aud_record_error(priv, ret);
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  active = (uint32_t)perf_getfreq();

  nxmutex_lock(&priv->lock);
  priv->frequency_voted = true;
  priv->frequency_uncertain = false;
  priv->diag.frequency_vote_count++;
  priv->diag.frequency_before_vote = before;
  priv->diag.perf_frequency = active;
  priv->diag.resources |= BK7258_AUD_RESOURCE_FREQUENCY;
  nxmutex_unlock(&priv->lock);

  if (active != BK7258_AUD_REQUIRED_CPU_HZ)
    {
      nxmutex_lock(&priv->lock);
      bk7258_aud_record_error(priv, -EIO);
      nxmutex_unlock(&priv->lock);
      return -EIO;
    }

  return OK;
}

static int bk7258_aud_frequency_release(struct bk7258_aud_dev_s *priv)
{
  uint32_t after;
  int ret;

  nxmutex_lock(&priv->lock);
  if (!priv->frequency_voted && !priv->frequency_uncertain)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  nxmutex_unlock(&priv->lock);

  ret = bk7258_pm_frequency_vote(BK7258_PM_FREQ_CLIENT_AUDIO,
                                  BK7258_PM_OPP_DEFAULT);
  if (ret < 0)
    {
      nxmutex_lock(&priv->lock);
      bk7258_aud_record_error(priv, ret);
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  after = (uint32_t)perf_getfreq();

  nxmutex_lock(&priv->lock);
  priv->frequency_voted = false;
  priv->frequency_uncertain = false;
  priv->diag.frequency_release_count++;
  priv->diag.frequency_after_release = after;
  priv->diag.resources &= ~BK7258_AUD_RESOURCE_FREQUENCY;
  nxmutex_unlock(&priv->lock);
  return OK;
}

/* Finish the session only after stream resources and APBs have gone away.
 * worker_lock serializes the potentially blocking RPMsg frequency release
 * against START and another lifecycle transition.  STOPPING closes the
 * CONFIGURE/START admission window while the vote is being released.
 */

static int bk7258_aud_finish_session(struct bk7258_aud_dev_s *priv)
{
  int session_ret;
  int ret;

  nxmutex_lock(&priv->worker_lock);
  nxmutex_lock(&priv->lock);

  if (priv->outstanding != 0 || priv->pid >= 0 ||
      bk7258_aud_resources_owned(priv))
    {
      bk7258_aud_set_state(priv, BK7258_AUD_STATE_FAULT);
      nxmutex_unlock(&priv->lock);
      nxmutex_unlock(&priv->worker_lock);
      return -EBUSY;
    }

  bk7258_aud_set_state(priv, BK7258_AUD_STATE_STOPPING);
  nxmutex_unlock(&priv->lock);

  ret = bk7258_aud_frequency_release(priv);
  if (ret == OK && priv->audio_session_owned)
    {
      session_ret = bk7258_media_audio_session_release(
        BK7258_MEDIA_AUDIO_DAC);
      if (session_ret < 0)
        {
          ret = session_ret;
        }
      else
        {
          priv->audio_session_owned = false;
        }
    }

  nxmutex_lock(&priv->lock);
  if (ret < 0 || priv->frequency_voted || priv->frequency_uncertain)
    {
      bk7258_aud_set_state(priv, BK7258_AUD_STATE_FAULT);
      __atomic_store_n(&priv->close_safe, false, __ATOMIC_RELEASE);
      if (ret == OK)
        {
          ret = -EIO;
          bk7258_aud_record_error(priv, ret);
        }
    }
  else
    {
      priv->reserved = false;
      priv->configured = false;
      priv->final_queued = false;
      priv->complete_sent = false;
#ifdef CONFIG_BK7258_AUD_DAC_EQ
      bk7258_aud_eq_clear_shadow_locked(priv);
#endif
      bk7258_aud_set_state(priv, BK7258_AUD_STATE_RESET);
      __atomic_store_n(&priv->close_safe, true, __ATOMIC_RELEASE);
    }

  nxmutex_unlock(&priv->lock);
  nxmutex_unlock(&priv->worker_lock);
  return ret;
}

static int bk7258_aud_set_pa(struct bk7258_aud_dev_s *priv, bool enable)
{
  int ret;

  ret = priv->board->set(priv->board_config, enable);
  if (ret < 0 || priv->board->is_enabled(priv->board_config) != enable)
    {
      return ret < 0 ? ret : -EIO;
    }

  if (priv->pa_enabled != enable)
    {
      priv->pa_enabled = enable;

      if (enable)
        {
          priv->diag.pa_on_count++;
          priv->diag.resources |= BK7258_AUD_RESOURCE_PA;
        }
      else
        {
          priv->diag.pa_off_count++;
          priv->diag.resources &= ~BK7258_AUD_RESOURCE_PA;
        }
    }

  priv->diag.pa_enabled = enable;
  return OK;
}

static void bk7258_aud_callback(struct bk7258_aud_dev_s *priv,
                                uint16_t reason,
                                struct ap_buffer_s *apb,
                                uint16_t status)
{
#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, reason, apb, status, priv);
#else
  priv->dev.upper(priv->dev.priv, reason, apb, status);
#endif
}

static void bk7258_aud_notify_error(struct bk7258_aud_dev_s *priv,
                                    int error)
{
  uint16_t status;

  bk7258_aud_record_error(priv, error);
  status = error < 0 ? (uint16_t)(-error) : (uint16_t)error;
  bk7258_aud_callback(priv, AUDIO_CALLBACK_IOERR, NULL, status);
}

static void bk7258_aud_notify_complete(struct bk7258_aud_dev_s *priv)
{
  bool notify = false;

  nxmutex_lock(&priv->lock);
  if (!priv->complete_sent)
    {
      priv->complete_sent = true;
      priv->diag.complete_count++;
      notify = true;
    }

  nxmutex_unlock(&priv->lock);

  if (notify)
    {
      bk7258_aud_callback(priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
    }
}

/* The static singleton is required because bk_dma_register_isr() supplies
 * only the channel number and no caller context.
 */

static void bk7258_aud_dma_isr(dma_id_t dma_id)
{
  struct bk7258_aud_dev_s *priv = &g_bk7258_aud;
  uint32_t delta;
  uint32_t now;
  clock_t tick;
  int cpu;

  if (dma_id == priv->dma_id && priv->streaming)
    {
      cpu = this_cpu();
      now = (uint32_t)perf_gettime();
      tick = clock_systime_ticks();

      if ((unsigned int)cpu < BK7258_AUD_DIAG_CPU_SLOTS)
        {
          __atomic_fetch_or(&priv->diag.dma_isr_cpu_mask,
                            1u << cpu, __ATOMIC_RELAXED);
          __atomic_add_fetch(&priv->diag.dma_isr_cpu_count[cpu], 1u,
                             __ATOMIC_RELAXED);

          if (priv->isr_last_cycle[cpu] != 0)
            {
              delta = now - priv->isr_last_cycle[cpu];
              if (priv->diag.dma_isr_min_cycles[cpu] == 0 ||
                  delta < priv->diag.dma_isr_min_cycles[cpu])
                {
                  priv->diag.dma_isr_min_cycles[cpu] = delta;
                }

              if (delta > priv->diag.dma_isr_max_cycles[cpu])
                {
                  priv->diag.dma_isr_max_cycles[cpu] = delta;
                }
            }

          if (priv->isr_last_tick[cpu] != 0)
            {
              __atomic_fetch_add(&priv->diag.dma_isr_interval_ticks,
                                 bk7258_aud_tick_delta(
                                   priv, tick, priv->isr_last_tick[cpu]),
                                 __ATOMIC_RELAXED);
              __atomic_add_fetch(&priv->diag.dma_isr_interval_count, 1u,
                                 __ATOMIC_RELAXED);
            }

          priv->isr_last_cycle[cpu] = now;
          priv->isr_last_tick[cpu] = tick;
        }

      __atomic_add_fetch(&priv->isr_sequence, 1u, __ATOMIC_RELEASE);
      __atomic_add_fetch(&priv->diag.dma_isr_count, 1u,
                         __ATOMIC_RELAXED);
      nxsem_post(&priv->dmasem);
    }
}

static int bk7258_aud_hw_setup(struct bk7258_aud_dev_s *priv)
{
  aud_dac_config_t cfg = DEFAULT_AUD_DAC_CONFIG();
  dma_config_t dma_cfg;
  uintptr_t ring_addr;
  uint32_t fifo_addr;
  bk_err_t error;
  bk_err_t rollback_error;
  int ret;

  ret = bk7258_media_root_initialize(BK7258_MEDIA_ROOT_DMA);
  if (ret < 0)
    {
      return ret;
    }

  cfg.dac_chl        = AUD_DAC_CHL_L;
  cfg.samp_rate      = priv->samplerate;
  cfg.work_mode      = AUD_DAC_WORK_MODE_DIFFEN;
  cfg.dac_gain       = priv->dig_gain;
  cfg.dac_clk_invert = AUD_DAC_CLK_INVERT_RISING;
  cfg.clk_src        = AUD_CLK_XTAL;

  /* The SDK DAC init is idempotent rather than reference counted.  Refuse
   * to borrow an owner that this wrapper could later tear down.
   */

  if (bk_aud_get_module_init_sta(AUD_MODULE_DAC))
    {
      return -EBUSY;
    }

  error = bk_aud_dac_init(&cfg);
  if (error != BK_OK)
    {
      /* The pinned SDK can initialize the common AUD root and analog DAC
       * blocks before a later sample-rate step fails.  It clears the DAC
       * module flag on that path but does not release the common root, so
       * the normal dac_initialized-gated teardown cannot see the half-open
       * resource.  With all module flags clear, common deinit is the SDK's
       * own fail-safe rollback; if another AUD submodule is active it leaves
       * the shared root enabled.
       */

      rollback_error = bk_aud_driver_deinit();
      if (rollback_error != BK_OK)
        {
          auderr("ERROR: DAC init rollback failed: %d\n", rollback_error);
        }

      return bk7258_aud_result(error);
    }

  priv->dac_initialized = true;
  priv->diag.resources |= BK7258_AUD_RESOURCE_DAC;

  error = bk_aud_dac_mute();
  if (error != BK_OK)
    {
      return bk7258_aud_result(error);
    }

  error = bk_aud_set_ana_dac_gain(priv->ana_gain);
  if (error != BK_OK)
    {
      return bk7258_aud_result(error);
    }

  ret = bk7258_aud_set_pa(priv, false);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_AUD_DAC_EQ
  ret = bk7258_aud_eq_apply(priv);
  if (ret < 0)
    {
      return ret;
    }
#endif

  priv->ring_mem = kmm_zalloc(BK7258_AUD_RING_BYTES);
  if (priv->ring_mem == NULL)
    {
      return -ENOMEM;
    }

  priv->scratch = kmm_zalloc(BK7258_AUD_FRAME_BYTES);
  if (priv->scratch == NULL)
    {
      return -ENOMEM;
    }

  ring_addr = (uintptr_t)priv->ring_mem;
  if (ring_addr > UINT32_MAX ||
      ring_addr + BK7258_AUD_RING_BYTES < ring_addr ||
      ring_addr + BK7258_AUD_RING_BYTES > UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  priv->dma_id = bk_dma_alloc(DMA_DEV_AUDIO);
  if (priv->dma_id < DMA_ID_0 || priv->dma_id >= DMA_ID_MAX)
    {
      priv->dma_id = DMA_ID_MAX;
      return -EBUSY;
    }

  priv->dma_allocated = true;
  priv->diag.dma_alloc_count++;
  priv->diag.resources |= BK7258_AUD_RESOURCE_DMA;

  error = bk_aud_dac_get_fifo_addr(&fifo_addr);
  if (error != BK_OK)
    {
      return bk7258_aud_result(error);
    }

  memset(&dma_cfg, 0, sizeof(dma_cfg));
  dma_cfg.mode                = DMA_WORK_MODE_REPEAT;
  dma_cfg.chan_prio           = 1;
  dma_cfg.trans_type          = DMA_TRANS_DEFAULT;
  dma_cfg.src.dev             = DMA_DEV_DTCM;
  dma_cfg.src.width           = DMA_DATA_WIDTH_32BITS;
  dma_cfg.src.addr_inc_en     = DMA_ADDR_INC_ENABLE;
  dma_cfg.src.addr_loop_en    = DMA_ADDR_LOOP_ENABLE;
  dma_cfg.src.start_addr      = (uint32_t)ring_addr;
  dma_cfg.src.end_addr        = (uint32_t)ring_addr +
                                BK7258_AUD_RING_BYTES;
  dma_cfg.dst.dev             = DMA_DEV_AUDIO;
  dma_cfg.dst.width           = DMA_DATA_WIDTH_16BITS;
  dma_cfg.dst.addr_inc_en     = DMA_ADDR_INC_ENABLE;
  dma_cfg.dst.addr_loop_en    = DMA_ADDR_LOOP_ENABLE;
  dma_cfg.dst.start_addr      = fifo_addr;
  dma_cfg.dst.end_addr        = fifo_addr + BK7258_AUD_FIFO_BYTES;

  error = bk_dma_init(priv->dma_id, &dma_cfg);
  if (error != BK_OK)
    {
      return bk7258_aud_result(error);
    }

  priv->dma_initialized = true;

  error = bk_dma_set_transfer_len(priv->dma_id,
                                  BK7258_AUD_FRAME_BYTES);
  if (error != BK_OK)
    {
      return bk7258_aud_result(error);
    }

  /* The pinned AP archive was built with CONFIG_SPE=1.  Both DMA endpoints
   * must therefore carry the secure attribute or setup succeeds without
   * producing completion interrupts.
   */

  error = bk_dma_set_src_sec_attr(priv->dma_id, DMA_ATTR_SEC);
  if (error != BK_OK)
    {
      return bk7258_aud_result(error);
    }

  error = bk_dma_set_dest_sec_attr(priv->dma_id, DMA_ATTR_SEC);
  if (error != BK_OK)
    {
      return bk7258_aud_result(error);
    }

  error = bk_dma_register_isr(priv->dma_id, NULL, bk7258_aud_dma_isr);
  if (error != BK_OK)
    {
      return bk7258_aud_result(error);
    }

  error = bk_dma_enable_finish_interrupt(priv->dma_id);
  if (error != BK_OK)
    {
      return bk7258_aud_result(error);
    }

  priv->dma_irq_enabled = true;

  ring_buffer_init(&priv->ring, priv->ring_mem, BK7258_AUD_RING_BYTES,
                   priv->dma_id, RB_DMA_TYPE_READ);
  priv->ring_valid = true;
  priv->diag.resources |= BK7258_AUD_RESOURCE_RING;
  return OK;
}

static int bk7258_aud_hw_stream_stop(struct bk7258_aud_dev_s *priv)
{
  int first = OK;
  int ret;
  bk_err_t error;

  priv->streaming = false;

  if (priv->dma_started)
    {
      error = bk_dma_stop(priv->dma_id);
      if (error == BK_OK)
        {
          priv->dma_started = false;
          priv->diag.dma_stop_count++;
        }
      else
        {
          first = bk7258_aud_result(error);
        }
    }

  if (priv->dac_initialized)
    {
      error = bk_aud_dac_mute();
      if (error != BK_OK && first == OK)
        {
          first = bk7258_aud_result(error);
        }
    }

  if (priv->pa_enabled)
    {
      nxsig_usleep(priv->board_config->speaker_off_delay_ms * 1000u);
      ret = bk7258_aud_set_pa(priv, false);
      if (ret < 0 && first == OK)
        {
          first = ret;
        }
    }

  if (priv->dac_started)
    {
      error = bk_aud_dac_stop();
      if (error == BK_OK)
        {
          priv->dac_started = false;
          priv->diag.dac_stop_count++;
        }
      else if (first == OK)
        {
          first = bk7258_aud_result(error);
        }
    }

  if (!priv->dma_started && !priv->dac_started && !priv->pa_enabled)
    {
      priv->diag.resources &= ~BK7258_AUD_RESOURCE_STREAM;
    }

  bk7258_aud_record_error(priv, first);
  return first;
}

static int bk7258_aud_hw_teardown(struct bk7258_aud_dev_s *priv)
{
  int first = OK;
  int ret;
  bk_err_t error;

  if (priv->dma_started || priv->dac_started || priv->pa_enabled)
    {
      first = bk7258_aud_hw_stream_stop(priv);
    }

  if (priv->dma_irq_enabled)
    {
      error = bk_dma_disable_finish_interrupt(priv->dma_id);
      if (error == BK_OK)
        {
          priv->dma_irq_enabled = false;
        }
      else if (first == OK)
        {
          first = bk7258_aud_result(error);
        }
    }

  /* ring_buffer_clear() reads the DMA pointer and updates its pause
   * address.  It must precede channel deinit/free.
   */

  if (priv->ring_valid)
    {
      ring_buffer_clear(&priv->ring);
      priv->ring_valid = false;
      priv->diag.resources &= ~BK7258_AUD_RESOURCE_RING;
    }

  if (priv->dma_initialized)
    {
      error = bk_dma_deinit(priv->dma_id);
      if (error == BK_OK)
        {
          priv->dma_initialized = false;
          priv->dma_started = false;
          priv->dma_irq_enabled = false;
          priv->diag.dma_deinit_count++;
        }
      else if (first == OK)
        {
          first = bk7258_aud_result(error);
        }
    }

  if (priv->dma_allocated && !priv->dma_initialized)
    {
      error = bk_dma_free(DMA_DEV_AUDIO, priv->dma_id);
      if (error == BK_OK)
        {
          priv->dma_allocated = false;
          priv->dma_id = DMA_ID_MAX;
          priv->diag.dma_free_count++;
          priv->diag.resources &= ~BK7258_AUD_RESOURCE_DMA;
        }
      else if (first == OK)
        {
          first = bk7258_aud_result(error);
        }
    }

  if (!priv->dma_allocated)
    {
      kmm_free(priv->ring_mem);
      priv->ring_mem = NULL;
    }

  kmm_free(priv->scratch);
  priv->scratch = NULL;

#ifdef CONFIG_BK7258_AUD_DAC_EQ
  if (priv->dac_initialized && !priv->dac_started)
    {
      ret = bk7258_aud_eq_deconfig(priv);
      if (ret < 0 && first == OK)
        {
          first = ret;
        }
    }
#endif

  if (priv->dac_initialized && !priv->dac_started
#ifdef CONFIG_BK7258_AUD_DAC_EQ
      && !priv->eq_cleanup_needed
#endif
     )
    {
      error = bk_aud_dac_deinit();
      if (error == BK_OK)
        {
          priv->dac_initialized = false;
          priv->diag.resources &= ~BK7258_AUD_RESOURCE_DAC;
        }
      else if (first == OK)
        {
          first = bk7258_aud_result(error);
        }
    }

  if (priv->pa_enabled)
    {
      ret = bk7258_aud_set_pa(priv, false);
      if (ret < 0 && first == OK)
        {
          first = ret;
        }
    }

  bk7258_aud_record_error(priv, first);
  return first;
}

static int bk7258_aud_hw_start(struct bk7258_aud_dev_s *priv)
{
  bk_err_t error;
  int ret;

  ret = bk7258_aud_set_pa(priv, false);
  if (ret < 0)
    {
      return ret;
    }

  error = bk_aud_dac_mute();
  if (error != BK_OK)
    {
      return bk7258_aud_result(error);
    }

  error = bk_dma_start(priv->dma_id);
  if (error != BK_OK)
    {
      return bk7258_aud_result(error);
    }

  priv->dma_started = true;
  priv->diag.dma_start_count++;

  error = bk_aud_dac_start();
  if (error != BK_OK)
    {
      ret = bk7258_aud_result(error);
      bk7258_aud_hw_stream_stop(priv);
      return ret;
    }

  priv->dac_started = true;
  priv->diag.dac_start_count++;

  nxsig_usleep(priv->board_config->speaker_on_delay_ms * 1000u);
  ret = bk7258_aud_set_pa(priv, true);
  if (ret < 0)
    {
      bk7258_aud_hw_stream_stop(priv);
      return ret;
    }

  if (!priv->muted && priv->dig_gain != 0)
    {
      error = bk_aud_dac_unmute();
      if (error != BK_OK)
        {
          ret = bk7258_aud_result(error);
          bk7258_aud_hw_stream_stop(priv);
          return ret;
        }
    }

  priv->diag.resources |= BK7258_AUD_RESOURCE_STREAM;
  return OK;
}

static bool bk7258_aud_apb_queued(struct bk7258_aud_dev_s *priv,
                                  struct ap_buffer_s *apb)
{
  struct dq_entry_s *entry;
  unsigned int frame;
  unsigned int i;

  if (priv->active == apb)
    {
      return true;
    }

  for (entry = dq_peek(&priv->pendq); entry != NULL;
       entry = dq_next(entry))
    {
      if (entry == &apb->dq_entry)
        {
          return true;
        }
    }

  for (frame = 0; frame < BK7258_AUD_DMA_RING_FRAMES; frame++)
    {
      for (i = 0; i < priv->frames[frame].ndone; i++)
        {
          if (priv->frames[frame].done[i] == apb)
            {
              return true;
            }
        }
    }

  return false;
}

/* Assemble one physical DMA frame.  worker_lock serializes the scratch
 * buffer and RingBufferContext; lock protects APB state.
 */

static int bk7258_aud_fill_frame(struct bk7258_aud_dev_s *priv,
                                 unsigned int index)
{
  struct bk7258_aud_frame_s *frame = &priv->frames[index];
  struct ap_buffer_s *apb;
  int sem_value = 0;
  uint32_t offset = 0;
  uint32_t available;
  uint32_t copy;
  uint32_t written;

  memset(frame, 0, sizeof(*frame));
  memset(priv->scratch, 0, BK7258_AUD_FRAME_BYTES);

  nxmutex_lock(&priv->lock);

  while (offset < BK7258_AUD_FRAME_BYTES)
    {
      if (priv->active == NULL)
        {
          priv->active =
            (struct ap_buffer_s *)dq_remfirst(&priv->pendq);

          if (priv->active != NULL)
            {
              priv->active->flags &= ~AUDIO_APB_OUTPUT_ENQUEUED;
              priv->active->flags |= AUDIO_APB_OUTPUT_PROCESS;
            }
        }

      apb = priv->active;
      if (apb == NULL)
        {
          break;
        }

      available = apb->nbytes - apb->curbyte;
      copy = BK7258_AUD_FRAME_BYTES - offset;
      if (copy > available)
        {
          copy = available;
        }

      if (copy > 0)
        {
          memcpy(priv->scratch + offset, apb->samp + apb->curbyte, copy);
          apb->curbyte += copy;
          offset += copy;
          frame->payload_bytes += copy;
        }

      if (apb->curbyte == apb->nbytes)
        {
          DEBUGASSERT(frame->ndone < CONFIG_BK7258_AUD_QUEUE_DEPTH);
          frame->done[frame->ndone++] = apb;
          frame->final = (apb->flags & AUDIO_APB_FINAL) != 0;
          priv->active = NULL;

          if (frame->final)
            {
              break;
            }
        }
    }

  if (frame->payload_bytes == 0 && !frame->final && !priv->final_queued)
    {
      if (priv->diag.underrun_count == 0)
        {
          (void)nxsem_get_value(&priv->dmasem, &sem_value);
          if (sem_value < 0)
            {
              sem_value = 0;
            }

          priv->diag.first_underrun_isr_sequence =
            __atomic_load_n(&priv->isr_sequence, __ATOMIC_ACQUIRE);
          priv->diag.first_underrun_worker_wake =
            priv->diag.worker_wake_count;
          priv->diag.first_underrun_sem_value = (uint32_t)sem_value;
          priv->diag.first_underrun_outstanding = priv->outstanding;
          priv->diag.first_underrun_enqueue_count =
            priv->diag.enqueue_count;
          priv->diag.first_underrun_dequeue_count =
            priv->diag.dequeue_count;
          priv->diag.first_underrun_consume_sequence =
            priv->consume_sequence;
          priv->diag.first_underrun_produce_sequence =
            priv->produce_sequence;
          priv->diag.first_underrun_cpu = (uint32_t)this_cpu();
        }

      priv->diag.underrun_count++;
    }

  nxmutex_unlock(&priv->lock);

  written = ring_buffer_write(&priv->ring, priv->scratch,
                              BK7258_AUD_FRAME_BYTES);
  if (written != BK7258_AUD_FRAME_BYTES)
    {
      return -EIO;
    }

  priv->produce_sequence++;
  return OK;
}

static unsigned int
bk7258_aud_detach_all(struct bk7258_aud_dev_s *priv,
                      struct ap_buffer_s **buffers)
{
  struct ap_buffer_s *apb;
  unsigned int count = 0;
  unsigned int frame;
  unsigned int i;

  nxmutex_lock(&priv->lock);

  for (frame = 0; frame < BK7258_AUD_DMA_RING_FRAMES; frame++)
    {
      for (i = 0; i < priv->frames[frame].ndone; i++)
        {
          DEBUGASSERT(count < CONFIG_BK7258_AUD_QUEUE_DEPTH);
          buffers[count++] = priv->frames[frame].done[i];
        }

      memset(&priv->frames[frame], 0, sizeof(priv->frames[frame]));
    }

  if (priv->active != NULL)
    {
      DEBUGASSERT(count < CONFIG_BK7258_AUD_QUEUE_DEPTH);
      buffers[count++] = priv->active;
      priv->active = NULL;
    }

  while ((apb = (struct ap_buffer_s *)dq_remfirst(&priv->pendq)) != NULL)
    {
      DEBUGASSERT(count < CONFIG_BK7258_AUD_QUEUE_DEPTH);
      buffers[count++] = apb;
    }

  nxmutex_unlock(&priv->lock);
  return count;
}

static void
bk7258_aud_return_buffers(struct bk7258_aud_dev_s *priv,
                          struct ap_buffer_s **buffers,
                          unsigned int count)
{
  struct ap_buffer_s *apb;
  clock_t callback_start_tick;
  uint32_t callback_start_cycle;
  uint32_t delta;
  int callback_cpu;
  unsigned int i;

  for (i = 0; i < count; i++)
    {
      apb = buffers[i];
      if (apb == NULL)
        {
          continue;
        }

      nxmutex_lock(&priv->lock);
      DEBUGASSERT(priv->outstanding > 0);
      if (priv->outstanding > 0)
        {
          priv->outstanding--;
        }

      apb->flags &= ~(AUDIO_APB_OUTPUT_ENQUEUED |
                      AUDIO_APB_OUTPUT_PROCESS);
      priv->diag.dequeue_count++;
      nxmutex_unlock(&priv->lock);

      /* Keep the lower-half reference through the callback.  The upper half
       * clears output samples and may synchronously re-enter enqueuebuffer.
       * Do not dereference apb after apb_free().
       */

      callback_cpu = this_cpu();
      callback_start_cycle = (uint32_t)perf_gettime();
      callback_start_tick = clock_systime_ticks();
      bk7258_aud_callback(priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);

      priv->diag.dequeue_callback_count++;
      delta = bk7258_aud_tick_delta(priv, clock_systime_ticks(),
                                    callback_start_tick);
      if (delta > priv->diag.dequeue_callback_max_ticks)
        {
          priv->diag.dequeue_callback_max_ticks = delta;
        }

      if (callback_cpu == this_cpu())
        {
          delta = (uint32_t)perf_gettime() - callback_start_cycle;
          if (delta > priv->diag.dequeue_callback_max_cycles)
            {
              priv->diag.dequeue_callback_max_cycles = delta;
            }
        }
      else
        {
          priv->diag.dequeue_callback_cpu_migrations++;
        }

      apb_free(apb);
    }
}

static void
bk7258_aud_record_worker_service(struct bk7258_aud_dev_s *priv,
                                 uint32_t start_cycle,
                                 clock_t start_tick,
                                 uint32_t start_isr,
                                 int start_cpu)
{
  uint32_t delta;
  uint32_t isr_delta;
  int end_cpu;

  priv->diag.worker_service_count++;

  delta = bk7258_aud_tick_delta(priv, clock_systime_ticks(), start_tick);
  if (delta > priv->diag.worker_service_max_ticks)
    {
      priv->diag.worker_service_max_ticks = delta;
    }

  end_cpu = this_cpu();
  if (end_cpu == start_cpu)
    {
      delta = (uint32_t)perf_gettime() - start_cycle;
      if (delta > priv->diag.worker_service_max_cycles)
        {
          priv->diag.worker_service_max_cycles = delta;
        }
    }
  else
    {
      priv->diag.worker_service_cpu_migrations++;
    }

  isr_delta = __atomic_load_n(&priv->isr_sequence, __ATOMIC_ACQUIRE) -
              start_isr;
  if (isr_delta > 0)
    {
      priv->diag.worker_isr_during_service_count++;
      if (isr_delta > priv->diag.worker_isr_during_service_max)
        {
          priv->diag.worker_isr_during_service_max = isr_delta;
        }
    }
}

static void bk7258_aud_stop_thread(struct bk7258_aud_dev_s *priv)
{
  if (priv->pid < 0)
    {
      return;
    }

  priv->streaming = false;
  priv->terminate = true;
  nxsem_post(&priv->dmasem);
  nxsem_wait_uninterruptible(&priv->donesem);

  priv->pid = -1;
  priv->diag.resources &= ~BK7258_AUD_RESOURCE_WORKER;
}

static int bk7258_aud_worker(int argc, char **argv)
{
  struct bk7258_aud_dev_s *priv = &g_bk7258_aud;
  struct sched_param param;
  struct ap_buffer_s *done[CONFIG_BK7258_AUD_QUEUE_DEPTH];
  struct ap_buffer_s *detached[CONFIG_BK7258_AUD_QUEUE_DEPTH];
  struct bk7258_aud_frame_s *frame;
  unsigned int index;
  unsigned int ndone;
  unsigned int ndetached;
  clock_t service_start_tick;
  uint32_t service_start_cycle;
  uint32_t service_start_isr;
  int sem_value;
  int service_start_cpu;
  int cpu;
  bool final;
  bool drain_complete;
  int error;

  (void)argc;
  (void)argv;

  memset(&param, 0, sizeof(param));
  if (sched_getparam(0, &param) == 0)
    {
      priv->diag.worker_priority = (uint32_t)param.sched_priority;
    }

  for (; ; )
    {
      if (nxsem_wait_uninterruptible(&priv->dmasem) < 0)
        {
          continue;
        }

      if (priv->terminate)
        {
          break;
        }

      service_start_isr = __atomic_load_n(&priv->isr_sequence,
                                          __ATOMIC_ACQUIRE);
      service_start_cpu = this_cpu();
      service_start_cycle = (uint32_t)perf_gettime();
      service_start_tick = clock_systime_ticks();
      cpu = service_start_cpu;
      if (cpu >= 0 && cpu < 32)
        {
          priv->diag.worker_cpu_mask |= 1u << cpu;
        }

      priv->diag.worker_wake_count++;
      sem_value = 0;
      if (nxsem_get_value(&priv->dmasem, &sem_value) == OK &&
          sem_value > 0)
        {
          priv->diag.sem_backlog_events++;
          if ((uint32_t)sem_value > priv->diag.sem_backlog_max)
            {
              priv->diag.sem_backlog_max = (uint32_t)sem_value;
            }
        }

      nxmutex_lock(&priv->worker_lock);

      if (priv->terminate || !priv->streaming)
        {
          nxmutex_unlock(&priv->worker_lock);
          continue;
        }

      index = priv->consume_sequence % BK7258_AUD_DMA_RING_FRAMES;
      frame = &priv->frames[index];
      ndone = frame->ndone;
      DEBUGASSERT(ndone <= CONFIG_BK7258_AUD_QUEUE_DEPTH);
      memcpy(done, frame->done, ndone * sizeof(done[0]));
      final = frame->final;
      drain_complete = false;
      priv->diag.played_bytes += frame->payload_bytes;
      memset(frame, 0, sizeof(*frame));
      priv->consume_sequence++;

      if (final)
        {
          /* A DMA completion means the final payload reached the DAC FIFO,
           * not that it has left the analog output.  Keep repeat DMA fed with
           * silence for two complete physical frames, matching the official
           * speaker stream's drain delay, before muting and stopping.
           */

          priv->drain_remaining = BK7258_AUD_FINAL_DRAIN_FRAMES;
          error = bk7258_aud_fill_frame(priv, index);
        }
      else if (priv->drain_remaining > 0)
        {
          priv->drain_remaining--;
          priv->diag.final_drain_count++;
          if (priv->drain_remaining == 0)
            {
              drain_complete = true;
              error = OK;
            }
          else
            {
              error = bk7258_aud_fill_frame(priv, index);
            }
        }
      else
        {
          error = bk7258_aud_fill_frame(priv, index);
        }

      if (error < 0)
        {
          priv->terminate = true;
          bk7258_aud_record_error(priv, error);
          (void)bk7258_aud_hw_stream_stop(priv);
          ndetached = bk7258_aud_detach_all(priv, detached);
          (void)bk7258_aud_hw_teardown(priv);
          nxmutex_lock(&priv->lock);
          bk7258_aud_set_state(priv, BK7258_AUD_STATE_FAULT);
          nxmutex_unlock(&priv->lock);
          nxmutex_unlock(&priv->worker_lock);

          bk7258_aud_return_buffers(priv, done, ndone);
          bk7258_aud_return_buffers(priv, detached, ndetached);
          bk7258_aud_notify_error(priv, error);
          bk7258_aud_notify_complete(priv);
          break;
        }

      if (drain_complete)
        {
          priv->terminate = true;
          error = bk7258_aud_hw_stream_stop(priv);
          nxmutex_lock(&priv->lock);
          bk7258_aud_set_state(priv, error < 0 ?
                              BK7258_AUD_STATE_FAULT :
                              BK7258_AUD_STATE_DRAINED);
          nxmutex_unlock(&priv->lock);
          nxmutex_unlock(&priv->worker_lock);

          bk7258_aud_return_buffers(priv, done, ndone);
          if (error < 0)
            {
              bk7258_aud_notify_error(priv, error);
            }

          bk7258_aud_notify_complete(priv);
          break;
        }

      nxmutex_unlock(&priv->worker_lock);
      bk7258_aud_return_buffers(priv, done, ndone);
      bk7258_aud_record_worker_service(priv, service_start_cycle,
                                       service_start_tick,
                                       service_start_isr,
                                       service_start_cpu);
    }

  priv->streaming = false;
  nxsem_post(&priv->donesem);
  return OK;
}

static int bk7258_aud_stop_internal(struct bk7258_aud_dev_s *priv)
{
  struct ap_buffer_s *buffers[CONFIG_BK7258_AUD_QUEUE_DEPTH];
  unsigned int count;
  bool had_activity;
  bool notify;
  int first = OK;
  int ret;

  nxmutex_lock(&priv->lock);
  had_activity = priv->state == BK7258_AUD_STATE_RUNNING ||
                 priv->state == BK7258_AUD_STATE_STARTING ||
                 priv->outstanding != 0;
  bk7258_aud_set_state(priv, BK7258_AUD_STATE_STOPPING);
  priv->terminate = true;
  priv->streaming = false;
  nxmutex_unlock(&priv->lock);

  nxmutex_lock(&priv->worker_lock);
  ret = bk7258_aud_hw_stream_stop(priv);
  if (ret < 0)
    {
      first = ret;
    }

  nxmutex_unlock(&priv->worker_lock);

  bk7258_aud_stop_thread(priv);

  nxmutex_lock(&priv->worker_lock);
  count = bk7258_aud_detach_all(priv, buffers);
  ret = bk7258_aud_hw_teardown(priv);
  if (ret < 0 && first == OK)
    {
      first = ret;
    }

  nxmutex_unlock(&priv->worker_lock);

  bk7258_aud_return_buffers(priv, buffers, count);

  nxmutex_lock(&priv->lock);
  notify = had_activity && !priv->complete_sent;
  priv->configured = false;
  priv->final_queued = false;
  priv->drain_remaining = 0;

  if (first < 0 || bk7258_aud_resources_owned(priv) ||
      priv->outstanding != 0)
    {
      bk7258_aud_set_state(priv, BK7258_AUD_STATE_FAULT);
      if (first == OK)
        {
          first = -EIO;
        }
    }
  else
    {
      bk7258_aud_set_state(priv, BK7258_AUD_STATE_RESERVED);
    }

  nxmutex_unlock(&priv->lock);

  if (first < 0)
    {
      bk7258_aud_notify_error(priv, first);
    }

  if (notify)
    {
      bk7258_aud_notify_complete(priv);
    }

  return first;
}

/****************************************************************************
 * audio_ops_s
 ****************************************************************************/

static int bk7258_aud_getcaps(struct audio_lowerhalf_s *dev, int type,
                              struct audio_caps_s *caps)
{
  if (caps == NULL || caps->ac_len < sizeof(*caps))
    {
      return -EINVAL;
    }

  caps->ac_format.hw = 0;
  caps->ac_controls.w = 0;

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_QUERY:
        caps->ac_channels = 1;
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.b[0] = AUDIO_TYPE_OUTPUT |
                                     AUDIO_TYPE_FEATURE;
            caps->ac_format.hw = 1 << (AUDIO_FMT_PCM - 1);
          }
        else
          {
            caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
          }
        break;

      case AUDIO_TYPE_OUTPUT:
        caps->ac_channels = 1;
        if (caps->ac_subtype == AUDIO_TYPE_QUERY)
          {
            caps->ac_controls.hw[0] = AUDIO_SAMP_RATE_16K;
          }
        break;

      case AUDIO_TYPE_FEATURE:
        caps->ac_channels = 1;
        if (caps->ac_subtype == AUDIO_FU_UNDEF)
          {
            caps->ac_controls.b[0] = AUDIO_FU_VOLUME | AUDIO_FU_MUTE;
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

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_configure(struct audio_lowerhalf_s *dev,
                                void *session,
                                const struct audio_caps_s *caps)
#else
static int bk7258_aud_configure(struct audio_lowerhalf_s *dev,
                                const struct audio_caps_s *caps)
#endif
{
  struct bk7258_aud_dev_s *priv = (struct bk7258_aud_dev_s *)dev;
  uint32_t rate;
  uint16_t volume;
  uint8_t old_gain;
  uint8_t gain;
  bool old_muted;
  bool old_effective_mute;
  bool hardware_active;
  bool target_muted;
  bool mute;
  bk_err_t error;
  bk_err_t rollback;
  int ret = OK;

  if (priv == NULL || caps == NULL)
    {
      return -EINVAL;
    }

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (session != priv)
    {
      return -EINVAL;
    }
#endif

  if (caps->ac_type == AUDIO_TYPE_FEATURE)
    {
      /* worker_lock -> lock is the common control-plane order.  Keeping both
       * while touching the short SDK control path prevents STOP from tearing
       * down the DAC between validation and the operation, and software state
       * is committed only after the hardware accepts the whole change.
       */

      nxmutex_lock(&priv->worker_lock);
      nxmutex_lock(&priv->lock);
      if (!priv->reserved)
        {
          nxmutex_unlock(&priv->lock);
          nxmutex_unlock(&priv->worker_lock);
          return -EACCES;
        }

      old_gain = priv->dig_gain;
      old_muted = priv->muted;
      gain = old_gain;
      target_muted = old_muted;

      switch (caps->ac_format.hw)
        {
          case AUDIO_FU_VOLUME:
            volume = caps->ac_controls.hw[0];
            if (volume > AUDIO_VOLUME_MAX)
              {
                nxmutex_unlock(&priv->lock);
                nxmutex_unlock(&priv->worker_lock);
                return -ERANGE;
              }

            gain = (uint8_t)((volume * 0x3fu +
                              AUDIO_VOLUME_MAX / 2u) /
                             AUDIO_VOLUME_MAX);
            break;

          case AUDIO_FU_MUTE:
            target_muted = caps->ac_controls.hw[0] != 0;
            break;

          default:
            nxmutex_unlock(&priv->lock);
            nxmutex_unlock(&priv->worker_lock);
            return -ENOTTY;
        }

      mute = target_muted || gain == 0;
      hardware_active = priv->dac_started;
      if (hardware_active)
        {
          error = bk_aud_dac_set_gain(gain);
          if (error == BK_OK)
            {
              error = mute ? bk_aud_dac_mute() : bk_aud_dac_unmute();
            }

          if (error != BK_OK)
            {
              /* Restore both controls when the second SDK operation fails;
               * otherwise the wrapper would report the old setting while the
               * DAC retained a partially applied new one.
               */

              old_effective_mute = old_muted || old_gain == 0;
              rollback = bk_aud_dac_set_gain(old_gain);
              if (rollback == BK_OK)
                {
                  rollback = old_effective_mute ? bk_aud_dac_mute() :
                                                  bk_aud_dac_unmute();
                }

              ret = bk7258_aud_result(error);
              if (rollback != BK_OK)
                {
                  ret = -EIO;
                }

              bk7258_aud_record_error(priv, ret);
              nxmutex_unlock(&priv->lock);
              nxmutex_unlock(&priv->worker_lock);
              return ret;
            }
        }

      priv->dig_gain = gain;
      priv->muted = target_muted;
      priv->diag.digital_gain = gain;
      nxmutex_unlock(&priv->lock);
      nxmutex_unlock(&priv->worker_lock);
      return OK;
    }

  if (caps->ac_type != AUDIO_TYPE_OUTPUT ||
      caps->ac_subtype != AUDIO_FMT_PCM ||
      caps->ac_channels != 1 ||
      caps->ac_controls.b[2] != BK7258_AUD_BITS_PER_SAMPLE)
    {
      return -EINVAL;
    }

  rate = caps->ac_controls.hw[0] |
         ((uint32_t)caps->ac_controls.b[3] << 16);
  if (!bk7258_aud_rate_supported(rate))
    {
      return -ERANGE;
    }

  nxmutex_lock(&priv->lock);
  if (!priv->reserved)
    {
      nxmutex_unlock(&priv->lock);
      return -EACCES;
    }

  if (priv->state != BK7258_AUD_STATE_RESERVED &&
      priv->state != BK7258_AUD_STATE_CONFIGURED)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  if (priv->outstanding != 0 || bk7258_aud_resources_owned(priv))
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  priv->samplerate = rate;
  priv->channels = 1;
  priv->bits = BK7258_AUD_BITS_PER_SAMPLE;
  priv->configured = true;
  priv->diag.samplerate = rate;
  priv->diag.channels = 1;
  priv->diag.bits = BK7258_AUD_BITS_PER_SAMPLE;
  bk7258_aud_set_state(priv, BK7258_AUD_STATE_CONFIGURED);
  nxmutex_unlock(&priv->lock);
  return OK;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_start(struct audio_lowerhalf_s *dev, void *session)
#else
static int bk7258_aud_start(struct audio_lowerhalf_s *dev)
#endif
{
  struct bk7258_aud_dev_s *priv = (struct bk7258_aud_dev_s *)dev;
  struct ap_buffer_s *buffers[CONFIG_BK7258_AUD_QUEUE_DEPTH];
  unsigned int count = 0;
  unsigned int i;
  int pid;
  int ret;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (session != priv)
    {
      return -EINVAL;
    }
#endif

  nxmutex_lock(&priv->lock);
  if (!priv->reserved || !priv->configured ||
      priv->state != BK7258_AUD_STATE_CONFIGURED)
    {
      nxmutex_unlock(&priv->lock);
      return -EINVAL;
    }

  if (priv->outstanding == 0)
    {
      nxmutex_unlock(&priv->lock);
      return -EAGAIN;
    }

  priv->complete_sent = false;
  priv->terminate = false;
  priv->streaming = false;
  bk7258_aud_set_state(priv, BK7258_AUD_STATE_STARTING);
  nxmutex_unlock(&priv->lock);

  nxmutex_lock(&priv->worker_lock);
  ret = bk7258_aud_hw_setup(priv);
  if (ret < 0)
    {
      goto errout_locked;
    }

  while (nxsem_trywait(&priv->dmasem) >= 0);
  while (nxsem_trywait(&priv->donesem) >= 0);

  ring_buffer_clear(&priv->ring);
  memset(priv->frames, 0, sizeof(priv->frames));
  memset(priv->isr_last_cycle, 0, sizeof(priv->isr_last_cycle));
  memset(priv->isr_last_tick, 0, sizeof(priv->isr_last_tick));
  __atomic_store_n(&priv->isr_sequence, 0u, __ATOMIC_RELEASE);
  priv->consume_sequence = 0;
  priv->produce_sequence = 0;
  priv->drain_remaining = 0;

  for (i = 0; i < BK7258_AUD_DMA_RING_FRAMES; i++)
    {
      ret = bk7258_aud_fill_frame(priv, i);
      if (ret < 0)
        {
          goto errout_locked;
        }
    }

  priv->terminate = false;
  priv->streaming = true;
  pid = kthread_create("bk7258_aud",
                       CONFIG_BK7258_AUD_WORKER_PRIORITY,
                       BK7258_AUD_WORKER_STACKSIZE,
                       bk7258_aud_worker, NULL);
  if (pid < 0)
    {
      ret = pid;
      priv->streaming = false;
      goto errout_locked;
    }

  priv->pid = (pid_t)pid;
  priv->diag.resources |= BK7258_AUD_RESOURCE_WORKER;

  ret = bk7258_aud_hw_start(priv);
  if (ret < 0)
    {
      priv->streaming = false;
      priv->terminate = true;
      nxmutex_unlock(&priv->worker_lock);
      bk7258_aud_stop_thread(priv);
      nxmutex_lock(&priv->worker_lock);
      goto errout_locked;
    }

  nxmutex_lock(&priv->lock);
  bk7258_aud_set_state(priv, BK7258_AUD_STATE_RUNNING);
  nxmutex_unlock(&priv->lock);
  nxmutex_unlock(&priv->worker_lock);

  audinfo("Speaker started: %" PRIu32 " Hz, mono S16, frame=%u\n",
          priv->samplerate, (unsigned int)BK7258_AUD_FRAME_BYTES);
  return OK;

errout_locked:
  priv->streaming = false;
  priv->terminate = true;
  count = bk7258_aud_detach_all(priv, buffers);
  (void)bk7258_aud_hw_teardown(priv);
  nxmutex_lock(&priv->lock);
  bk7258_aud_set_state(priv, BK7258_AUD_STATE_FAULT);
  nxmutex_unlock(&priv->lock);
  nxmutex_unlock(&priv->worker_lock);

  bk7258_aud_return_buffers(priv, buffers, count);
  bk7258_aud_notify_error(priv, ret);
  bk7258_aud_notify_complete(priv);
  return ret;
}

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_stop(struct audio_lowerhalf_s *dev, void *session)
#else
static int bk7258_aud_stop(struct audio_lowerhalf_s *dev)
#endif
{
  struct bk7258_aud_dev_s *priv = (struct bk7258_aud_dev_s *)dev;
  bool needs_stop;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (session != priv)
    {
      return -EINVAL;
    }
#endif

  nxmutex_lock(&priv->lock);
  if (!priv->reserved)
    {
      nxmutex_unlock(&priv->lock);
      return -EACCES;
    }

  needs_stop = priv->state == BK7258_AUD_STATE_RUNNING ||
               priv->state == BK7258_AUD_STATE_DRAINED ||
               priv->state == BK7258_AUD_STATE_FAULT ||
               priv->state == BK7258_AUD_STATE_STARTING ||
               priv->outstanding != 0 || priv->pid >= 0 ||
               bk7258_aud_resources_owned(priv);
  if (!needs_stop)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  nxmutex_unlock(&priv->lock);
  return bk7258_aud_stop_internal(priv);
}
#endif

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_pause(struct audio_lowerhalf_s *dev, void *session)
#else
static int bk7258_aud_pause(struct audio_lowerhalf_s *dev)
#endif
{
  (void)dev;
#ifdef CONFIG_AUDIO_MULTI_SESSION
  (void)session;
#endif
  return -ENOTSUP;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_resume(struct audio_lowerhalf_s *dev, void *session)
#else
static int bk7258_aud_resume(struct audio_lowerhalf_s *dev)
#endif
{
  (void)dev;
#ifdef CONFIG_AUDIO_MULTI_SESSION
  (void)session;
#endif
  return -ENOTSUP;
}
#endif

static int bk7258_aud_shutdown(struct audio_lowerhalf_s *dev)
{
  struct bk7258_aud_dev_s *priv = (struct bk7258_aud_dev_s *)dev;
  int ret;

  /* NuttX invokes the last-close shutdown while holding the audio upper
   * spinlock.  An active worker could be blocked in a DEQUEUE callback on
   * that same lock.  The supported close contract is therefore:
   * STOP (or explicit SHUTDOWN), keep the MQ registered while draining all
   * DEQUEUE/COMPLETE messages, RELEASE, UNREGISTERMQ, FREEBUFFER, then close.
   * In that normal path this function is callback-free.
   */

  /* audio_close() invokes this callback while holding the upper-half
   * spinlock, so even the normal no-op path must not take a sleeping mutex.
   * release() publishes close_safe only after every worker, APB and hardware
   * owner has been quiesced.
   */

  if (__atomic_load_n(&priv->close_safe, __ATOMIC_ACQUIRE))
    {
      return OK;
    }

  ret = bk7258_aud_stop_internal(priv);
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_aud_finish_session(priv);
}

static int bk7258_aud_enqueuebuffer(struct audio_lowerhalf_s *dev,
                                    struct ap_buffer_s *apb)
{
  struct bk7258_aud_dev_s *priv = (struct bk7258_aud_dev_s *)dev;
  int ret;

  if (priv == NULL || apb == NULL || apb->samp == NULL ||
      apb->nbytes > apb->nmaxbytes || apb->curbyte > apb->nbytes ||
      (apb->nbytes % BK7258_AUD_BYTES_PER_SAMPLE) != 0 ||
      (apb->curbyte % BK7258_AUD_BYTES_PER_SAMPLE) != 0)
    {
      return -EINVAL;
    }

  /* enqueuebuffer() is a non-blocking lower-half operation.  The worker
   * holds this mutex only while moving at most one small DMA frame.  This
   * entry may also be reached by audio_try_enqueue() under the audio upper
   * spinlock, so neither the driver mutex nor the APB reference mutex may
   * sleep here.
   */

  ret = nxmutex_trylock(&priv->lock);
  if (ret < 0)
    {
      return -EAGAIN;
    }

  if (!priv->reserved ||
      (priv->state != BK7258_AUD_STATE_CONFIGURED &&
       priv->state != BK7258_AUD_STATE_RUNNING))
    {
      ret = -EACCES;
      goto errout;
    }

  if (priv->final_queued)
    {
      ret = -ESHUTDOWN;
      goto errout;
    }

  if (priv->outstanding >= CONFIG_BK7258_AUD_QUEUE_DEPTH)
    {
      ret = -ENOMEM;
      goto errout;
    }

  if (bk7258_aud_apb_queued(priv, apb))
    {
      ret = -EALREADY;
      goto errout;
    }

  ret = nxmutex_trylock(&apb->lock);
  if (ret < 0)
    {
      ret = -EAGAIN;
      goto errout;
    }

  apb->crefs++;
  nxmutex_unlock(&apb->lock);

  apb->flags &= ~(AUDIO_APB_DEQUEUED | AUDIO_APB_OUTPUT_PROCESS);
  apb->flags |= AUDIO_APB_OUTPUT_ENQUEUED;
  dq_addlast(&apb->dq_entry, &priv->pendq);
  priv->outstanding++;
  priv->diag.enqueue_count++;
  priv->diag.submitted_bytes += apb->nbytes - apb->curbyte;

  if ((apb->flags & AUDIO_APB_FINAL) != 0)
    {
      priv->final_queued = true;
    }

  nxmutex_unlock(&priv->lock);
  return OK;

errout:
  nxmutex_unlock(&priv->lock);
  return ret;
}

static int bk7258_aud_cancelbuffer(struct audio_lowerhalf_s *dev,
                                   struct ap_buffer_s *apb)
{
  (void)dev;
  (void)apb;
  return -ENOTSUP;
}

static int bk7258_aud_ioctl(struct audio_lowerhalf_s *dev, int cmd,
                            unsigned long arg)
{
  struct ap_buffer_info_s *info;

  (void)dev;

  switch (cmd)
    {
#ifdef CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS
      case AUDIOIOC_GETBUFFERINFO:
        info = (struct ap_buffer_info_s *)(uintptr_t)arg;
        if (info == NULL)
          {
            return -EINVAL;
          }

        info->buffer_size = BK7258_AUD_FRAME_BYTES;
        info->nbuffers = CONFIG_BK7258_AUD_QUEUE_DEPTH;
        return OK;
#endif

#ifdef CONFIG_BK7258_AUD_DAC_EQ
      case BK7258_AUDIOIOC_SET_DAC_EQ:
        return bk7258_aud_eq_set_config(
          (struct bk7258_aud_dev_s *)dev, arg);
#endif

      default:
        return -ENOTTY;
    }
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_reserve(struct audio_lowerhalf_s *dev,
                              void **psession)
#else
static int bk7258_aud_reserve(struct audio_lowerhalf_s *dev)
#endif
{
  struct bk7258_aud_dev_s *priv = (struct bk7258_aud_dev_s *)dev;
  int cleanup_ret;
  int session_ret;
  int ret;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (psession == NULL)
    {
      return -EINVAL;
    }

  *psession = NULL;
#endif

  /* Serialize the blocking AP/CP vote with START/RELEASE.  Do not publish a
   * reserved session until the vote is confirmed and the AP timebase reports
   * the required 480-MHz clock.
   */

  nxmutex_lock(&priv->worker_lock);
  nxmutex_lock(&priv->lock);
  if (priv->reserved || priv->state != BK7258_AUD_STATE_RESET ||
      priv->outstanding != 0 || bk7258_aud_resources_owned(priv) ||
      priv->frequency_voted || priv->frequency_uncertain)
    {
      nxmutex_unlock(&priv->lock);
      nxmutex_unlock(&priv->worker_lock);
      return -EBUSY;
    }

  nxmutex_unlock(&priv->lock);

  ret = bk7258_media_audio_session_acquire(BK7258_MEDIA_AUDIO_DAC);
  if (ret < 0)
    {
      nxmutex_unlock(&priv->worker_lock);
      return ret;
    }

  nxmutex_lock(&priv->lock);
  priv->audio_session_owned = true;
  __atomic_store_n(&priv->close_safe, false, __ATOMIC_RELEASE);
  nxmutex_unlock(&priv->lock);

  ret = bk7258_aud_frequency_acquire(priv);
  if (ret < 0)
    {
      /* A successful remote vote followed by a local frequency mismatch is
       * still owned.  Roll it back before reporting RESERVE failure.  If that
       * rollback fails, retain an internal reserved/fault state so last-close
       * cannot hide the leak; the caller's explicit SHUTDOWN can retry before
       * close (slow last-close cleanup is unsafe in the NuttX upper lock).
       */

      cleanup_ret = bk7258_aud_frequency_release(priv);

      session_ret = OK;
      if (cleanup_ret == OK && !priv->frequency_voted &&
          !priv->frequency_uncertain)
        {
          session_ret = bk7258_media_audio_session_release(
            BK7258_MEDIA_AUDIO_DAC);
          if (session_ret == OK)
            {
              priv->audio_session_owned = false;
            }
        }

      nxmutex_lock(&priv->lock);
      if (cleanup_ret < 0 || priv->frequency_voted ||
          priv->frequency_uncertain || session_ret < 0)
        {
          priv->reserved = true;
          bk7258_aud_set_state(priv, BK7258_AUD_STATE_FAULT);
          __atomic_store_n(&priv->close_safe, false, __ATOMIC_RELEASE);
        }
      else
        {
          bk7258_aud_set_state(priv, BK7258_AUD_STATE_RESET);
          __atomic_store_n(&priv->close_safe, true, __ATOMIC_RELEASE);
        }

      nxmutex_unlock(&priv->lock);
      nxmutex_unlock(&priv->worker_lock);
      return cleanup_ret < 0 ? cleanup_ret :
             session_ret < 0 ? session_ret : ret;
    }

  nxmutex_lock(&priv->lock);
  priv->reserved = true;
  priv->configured = false;
  priv->final_queued = false;
  priv->complete_sent = false;
  dq_init(&priv->pendq);
  bk7258_aud_set_state(priv, BK7258_AUD_STATE_RESERVED);
  nxmutex_unlock(&priv->lock);
  nxmutex_unlock(&priv->worker_lock);

#ifdef CONFIG_AUDIO_MULTI_SESSION
  *psession = priv;
#endif
  return OK;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int bk7258_aud_release(struct audio_lowerhalf_s *dev, void *session)
#else
static int bk7258_aud_release(struct audio_lowerhalf_s *dev)
#endif
{
  struct bk7258_aud_dev_s *priv = (struct bk7258_aud_dev_s *)dev;
  bool needs_stop;
  int ret = OK;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  if (session != priv)
    {
      return -EINVAL;
    }
#endif

  nxmutex_lock(&priv->lock);
  if (!priv->reserved && !priv->frequency_voted &&
      !priv->frequency_uncertain)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  needs_stop = priv->state == BK7258_AUD_STATE_RUNNING ||
               priv->state == BK7258_AUD_STATE_DRAINED ||
               priv->state == BK7258_AUD_STATE_FAULT ||
               priv->state == BK7258_AUD_STATE_STARTING ||
               priv->outstanding != 0 || priv->pid >= 0 ||
               bk7258_aud_resources_owned(priv);
  nxmutex_unlock(&priv->lock);

  if (needs_stop)
    {
      ret = bk7258_aud_stop_internal(priv);
      if (ret < 0)
        {
          return ret;
        }
    }

  return bk7258_aud_finish_session(priv);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_aud_initialize(FAR const struct bk7258_aud_board_s *board)
{
  struct bk7258_aud_dev_s *priv = &g_bk7258_aud;
  int ret;

#ifdef CONFIG_BK7258_AGENT_MEDIA_PLAYER
  bk7258_agent_media_player_link();
#endif

  if (g_bk7258_aud_registered)
    {
      return OK;
    }

  if (!bk7258_aud_rate_supported(CONFIG_BK7258_AUD_SAMPLE_RATE))
    {
      return -EINVAL;
    }

  if (!bk7258_aud_board_valid(board))
    {
      auderr("ERROR: BK7258 audio board configuration is invalid\n");
      return -EINVAL;
    }

  ret = board->initialize(board->config);
  if (ret < 0)
    {
      return ret;
    }

  ret = board->set(board->config, false);
  if (ret < 0 || board->is_enabled(board->config))
    {
      return ret < 0 ? ret : -EIO;
    }

  priv->board = board;
  priv->board_config = board->config;
  priv->dev.ops = &g_bk7258_aud_ops;
  priv->samplerate = CONFIG_BK7258_AUD_SAMPLE_RATE;
  priv->channels = 1;
  priv->bits = BK7258_AUD_BITS_PER_SAMPLE;
  priv->dig_gain = CONFIG_BK7258_AUD_DIG_GAIN;
  priv->ana_gain = CONFIG_BK7258_AUD_ANA_GAIN;
  priv->muted = false;
#ifdef CONFIG_BK7258_AUD_DAC_EQ
  memset(&priv->eq_config, 0, sizeof(priv->eq_config));
  priv->eq_shadow_valid = false;
  priv->eq_cleanup_needed = false;
  priv->eq_applied = false;
#endif
  priv->dma_id = DMA_ID_MAX;
  priv->pid = -1;
  dq_init(&priv->pendq);

  memset(&priv->diag, 0, sizeof(priv->diag));
  priv->diag.magic = BK7258_AUD_DIAG_MAGIC;
  priv->diag.version = BK7258_AUD_DIAG_VERSION;
  priv->diag.size = sizeof(priv->diag);
  priv->diag.samplerate = priv->samplerate;
  priv->diag.channels = priv->channels;
  priv->diag.bits = priv->bits;
  priv->diag.digital_gain = priv->dig_gain;
  priv->diag.analog_gain = priv->ana_gain;
  priv->diag.perf_frequency = (uint32_t)perf_getfreq();
  bk7258_aud_set_state(priv, BK7258_AUD_STATE_RESET);
  __atomic_store_n(&priv->close_safe, true, __ATOMIC_RELEASE);

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

  ret = audio_register(CONFIG_BK7258_AUD_DEVNAME, &priv->dev);
  if (ret < 0)
    {
      nxsem_destroy(&priv->donesem);
      nxsem_destroy(&priv->dmasem);
      return ret;
    }

  g_bk7258_aud_registered = true;
  syslog(LOG_INFO,
         "BDAC BOOT PASS board=%s dev=/dev/audio/%s rate=%" PRIu32
         " frame=%u queue=%u pa=P%u active=%s\n",
         priv->board_config->variant_name, CONFIG_BK7258_AUD_DEVNAME,
         priv->samplerate, (unsigned int)BK7258_AUD_FRAME_BYTES,
         (unsigned int)CONFIG_BK7258_AUD_QUEUE_DEPTH,
         (unsigned int)priv->board_config->speaker_control_gpio,
         priv->board_config->speaker_active_high ? "high" : "low");
  return OK;
}

int bk7258_aud_get_diag(struct bk7258_aud_diag_s *diag)
{
  struct bk7258_aud_dev_s *priv = &g_bk7258_aud;

  if (diag == NULL || !g_bk7258_aud_registered)
    {
      return -EINVAL;
    }

  nxmutex_lock(&priv->lock);
  priv->diag.pa_enabled = priv->board->is_enabled(priv->board_config);
  memcpy(diag, &priv->diag, sizeof(*diag));
  nxmutex_unlock(&priv->lock);
  return OK;
}

#endif /* CONFIG_BK7258_AUD */
