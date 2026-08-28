/****************************************************************************
 * chips/bk7258/ap/bk7258_aud_validation.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bounded real-board validation for the public BK7258 speaker audio ABI.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AUD_LIFECYCLE_VALIDATION

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <mqueue.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>
#include <nuttx/clock.h>
#include <nuttx/kthread.h>

#include <arch/chip/bk7258_aud.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BDACVAL_DEVPATH \
  "/dev/audio/" CONFIG_BK7258_AUD_DEVNAME
#define BDACVAL_MQNAME               "bdacval"
#define BDACVAL_CYCLES               2u
#define BDACVAL_BUFFERS              CONFIG_BK7258_AUD_QUEUE_DEPTH
#define BDACVAL_TARGET_DEQUEUES      24u
#define BDACVAL_START_DELAY_US       1000000u
#define BDACVAL_TIMEOUT_SEC          8
#define BDACVAL_STACKSIZE            6144
#define BDACVAL_TONE_HZ              1000u
#define BDACVAL_TONE_AMPLITUDE       4096

#define BDACVAL_MAGIC                0x56434442u /* "BDCV" */
#ifdef CONFIG_BK7258_AUD_DAC_EQ
#  define BDACVAL_VERSION            6u
#  define BDACVAL_EQ_REJECTS_PER_CYCLE 8u
#  define BDACVAL_EQ_CONFIGS_PER_CYCLE 2u
#  define BDACVAL_EQ_READBACKS_PER_CYCLE 2u
#else
#  define BDACVAL_VERSION            5u
#endif
#define BDACVAL_RUNNING              1u
#define BDACVAL_PASSED               2u
#define BDACVAL_FAILED               3u
#define BDACVAL_IDLE_CPU_HZ           120000000u
#define BDACVAL_ACTIVE_CPU_HZ         480000000u

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum bdacval_stage_e
{
  BDACVAL_STAGE_INIT = 1,
  BDACVAL_STAGE_OPEN,
  BDACVAL_STAGE_CAPS,
  BDACVAL_STAGE_RESERVE,
#ifdef CONFIG_BK7258_AUD_DAC_EQ
  BDACVAL_STAGE_EQ_CONFIG,
#endif
  BDACVAL_STAGE_CONFIGURE,
  BDACVAL_STAGE_BUFFER_INFO,
  BDACVAL_STAGE_MESSAGE_QUEUE,
  BDACVAL_STAGE_ALLOCATE,
  BDACVAL_STAGE_PRIME,
  BDACVAL_STAGE_START,
  BDACVAL_STAGE_RECEIVE,
  BDACVAL_STAGE_STOP,
  BDACVAL_STAGE_DRAIN,
  BDACVAL_STAGE_RELEASE,
  BDACVAL_STAGE_DIAGNOSTIC
};

struct bdacval_diag_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t state;
  int32_t  result;
  uint32_t stage;
  uint32_t cycles;
  uint32_t dequeues;
  uint32_t completes;
  uint32_t submitted_buffers;
  uint32_t final_buffers;
  uint32_t manual_stop_cycles;
  uint32_t natural_drain_cycles;
  uint32_t driver_state;
  int32_t  driver_first_error;
  uint32_t driver_resources;
  uint32_t driver_enqueue_delta;
  uint32_t driver_dequeue_delta;
  uint32_t driver_complete_delta;
  uint32_t driver_submitted_delta;
  uint32_t driver_played_delta;
  uint32_t driver_dma_alloc_delta;
  uint32_t driver_dma_start_delta;
  uint32_t driver_dma_isr_delta;
  uint32_t driver_final_drain_delta;
  uint32_t driver_dma_stop_delta;
  uint32_t driver_dma_deinit_delta;
  uint32_t driver_dma_free_delta;
  uint32_t driver_dac_start_delta;
  uint32_t driver_dac_stop_delta;
  uint32_t driver_pa_on_delta;
  uint32_t driver_pa_off_delta;
  uint32_t driver_underrun_delta;
  uint32_t driver_pa_enabled;
  uint32_t producer_priority;
  uint32_t producer_cpu_mask;
  uint32_t mq_depth_after_receive_max;
  uint32_t tick_frequency;
  uint32_t tick_regression_count;
  uint32_t monotonic_regression_count;
  uint32_t monotonic_sample_errors;

  /* START call duration, then the post-START wait until the producer has
   * actually received its first DEQUEUE message.
   */

  uint32_t cycle_ticks[BDACVAL_CYCLES];
  uint32_t start_ioctl_ticks[BDACVAL_CYCLES];
  uint32_t first_dequeue_delay_ticks[BDACVAL_CYCLES];
  uint32_t dequeue_interval_max_ticks;
  uint32_t received_dequeue_to_enqueue_max_ticks;
  uint32_t driver_perf_before_frequency;
  uint32_t driver_perf_frequency;
  uint32_t driver_frequency_vote_delta;
  uint32_t driver_frequency_release_delta;
  uint32_t driver_frequency_before_vote;
  uint32_t driver_frequency_after_release;
  uint32_t cycle_frequency_before_vote[BDACVAL_CYCLES];
  uint32_t cycle_frequency_active[BDACVAL_CYCLES];
  uint32_t cycle_frequency_after_release[BDACVAL_CYCLES];
  uint32_t driver_dma_isr_cpu_mask;
  uint32_t driver_dma_isr_cpu_count[BK7258_AUD_DIAG_CPU_SLOTS];
  uint32_t driver_dma_isr_min_cycles[BK7258_AUD_DIAG_CPU_SLOTS];
  uint32_t driver_dma_isr_max_cycles[BK7258_AUD_DIAG_CPU_SLOTS];
  uint32_t driver_dma_isr_interval_ticks;
  uint32_t driver_dma_isr_interval_count;
  uint32_t driver_tick_regression_delta;
  uint32_t driver_worker_wake_delta;
  uint32_t driver_worker_cpu_mask;
  uint32_t driver_worker_priority;
  uint32_t driver_sem_backlog_max;
  uint32_t driver_sem_backlog_events_delta;
  uint32_t driver_worker_service_count_delta;
  uint32_t driver_worker_service_max_cycles;
  uint32_t driver_worker_service_max_ticks;
  uint32_t driver_worker_service_cpu_migrations_delta;
  uint32_t driver_worker_isr_during_service_count_delta;
  uint32_t driver_worker_isr_during_service_max;
  uint32_t driver_callback_count_delta;
  uint32_t driver_callback_max_cycles;
  uint32_t driver_callback_max_ticks;
  uint32_t driver_callback_cpu_migrations_delta;
  uint32_t first_underrun_isr_sequence;
  uint32_t first_underrun_worker_wake;
  uint32_t first_underrun_sem_value;
  uint32_t first_underrun_outstanding;
  uint32_t first_underrun_enqueue_count;
  uint32_t first_underrun_dequeue_count;
  uint32_t first_underrun_consume_sequence;
  uint32_t first_underrun_produce_sequence;
  uint32_t first_underrun_cpu;

#ifdef CONFIG_BK7258_AUD_DAC_EQ
  uint32_t eq_pre_reserve_rejects;
  uint32_t eq_invalid_rejects;
  uint32_t eq_runtime_rejects;
  uint32_t expected_eq_config_hash;
  uint32_t driver_eq_shadow_valid;
  uint32_t driver_eq_requested;
  uint32_t driver_eq_applied;
  uint32_t driver_eq_config_delta;
  uint32_t driver_eq_apply_delta;
  uint32_t driver_eq_deconfig_delta;
  uint32_t driver_eq_reject_delta;
  uint32_t driver_eq_readback_delta;
  uint32_t driver_eq_readback_fail_delta;
  uint32_t driver_eq_last_config_hash;
#endif
};

#ifdef CONFIG_BK7258_AUD_DAC_EQ
_Static_assert(sizeof(struct bdacval_diag_s) == 0x1b4,
               "BK7258 AUD EQ validation diagnostic v6 ABI changed");
#else
_Static_assert(sizeof(struct bdacval_diag_s) == 0x17c,
               "BK7258 AUD validation diagnostic v5 ABI changed");
#endif

struct bdacval_cycle_s
{
  struct ap_buffer_s *buffers[BDACVAL_BUFFERS];
  bool                outstanding[BDACVAL_BUFFERS];
  uint32_t            phase;
  uint32_t            dequeues;
  uint32_t            completes;
  uint32_t            submitted;
  int32_t             final_index;
  uint32_t            cycle_number;
  clock_t             start_tick;
  clock_t             last_dequeue_tick;
  clock_t             dequeue_ready_tick;
  bool                dequeue_pending;
  bool                final_enqueued;
  bool                final_dequeued;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Non-static so a non-halting J-Link read can inspect the result without
 * depending on the console or RPMsg syslog transport.
 */

volatile struct bdacval_diag_s g_bk7258_aud_validation_diag;

static uint64_t g_bdacval_monotonic_max_ns;
static bool g_bdacval_monotonic_valid;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bdacval_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

#ifdef CONFIG_BK7258_AUD_DAC_EQ

static uint32_t
bdacval_eq_hash(const struct bk7258_aud_eq_config_s *config)
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

static void
bdacval_eq_init_config(struct bk7258_aud_eq_config_s *config,
                       bool boundary_pattern)
{
  static const int32_t pattern[BK7258_AUD_EQ_COEFF_COUNT] =
    {
      BK7258_AUD_EQ_COEFF_MIN,
      BK7258_AUD_EQ_COEFF_MAX,
      -1,
      1,
      0x12345
    };
  unsigned int coefficient;
  unsigned int bank;

  memset(config, 0, sizeof(*config));
  config->version = BK7258_AUD_EQ_CONFIG_VERSION;
  config->size = sizeof(*config);
  config->flags = BK7258_AUD_EQ_FLAG_ENABLE;

  if (boundary_pattern)
    {
      for (bank = 0; bank < BK7258_AUD_EQ_BANK_COUNT; bank++)
        {
          for (coefficient = 0;
               coefficient < BK7258_AUD_EQ_COEFF_COUNT;
               coefficient++)
            {
              config->coeff[bank][coefficient] = pattern[coefficient];
            }
        }
    }
}

static int
bdacval_eq_expect_reject(int fd, unsigned long arg, int expected_errno,
                         volatile uint32_t *counter)
{
  int ret;

  errno = 0;
  ret = ioctl(fd, BK7258_AUDIOIOC_SET_DAC_EQ, arg);
  if (ret >= 0 || errno != expected_errno)
    {
      return -EPROTO;
    }

  (*counter)++;
  return OK;
}

static int bdacval_eq_pre_reserve(int fd)
{
  struct bk7258_aud_eq_config_s config;

  bdacval_eq_init_config(&config, false);
  return bdacval_eq_expect_reject(
    fd, (unsigned long)(uintptr_t)&config, EACCES,
    &g_bk7258_aud_validation_diag.eq_pre_reserve_rejects);
}

static int bdacval_eq_configure_reserved(int fd)
{
  struct bk7258_aud_eq_config_s config;
  struct bk7258_aud_diag_s before;
  struct bk7258_aud_diag_s after;
  uint32_t boundary_hash;
  uint32_t safe_hash;
  int ret;

  ret = bk7258_aud_get_diag(&before);
  if (ret < 0)
    {
      return ret;
    }

  ret = bdacval_eq_expect_reject(
    fd, 0, EINVAL,
    &g_bk7258_aud_validation_diag.eq_invalid_rejects);
  if (ret < 0)
    {
      return ret;
    }

  bdacval_eq_init_config(&config, false);
  config.version++;
  ret = bdacval_eq_expect_reject(
    fd, (unsigned long)(uintptr_t)&config, EINVAL,
    &g_bk7258_aud_validation_diag.eq_invalid_rejects);
  if (ret < 0)
    {
      return ret;
    }

  bdacval_eq_init_config(&config, false);
  config.size -= sizeof(uint32_t);
  ret = bdacval_eq_expect_reject(
    fd, (unsigned long)(uintptr_t)&config, EINVAL,
    &g_bk7258_aud_validation_diag.eq_invalid_rejects);
  if (ret < 0)
    {
      return ret;
    }

  bdacval_eq_init_config(&config, false);
  config.flags |= 1u << 1;
  ret = bdacval_eq_expect_reject(
    fd, (unsigned long)(uintptr_t)&config, EINVAL,
    &g_bk7258_aud_validation_diag.eq_invalid_rejects);
  if (ret < 0)
    {
      return ret;
    }

  bdacval_eq_init_config(&config, false);
  config.coeff[0][0] = BK7258_AUD_EQ_COEFF_MIN - 1;
  ret = bdacval_eq_expect_reject(
    fd, (unsigned long)(uintptr_t)&config, ERANGE,
    &g_bk7258_aud_validation_diag.eq_invalid_rejects);
  if (ret < 0)
    {
      return ret;
    }

  bdacval_eq_init_config(&config, false);
  config.coeff[0][0] = BK7258_AUD_EQ_COEFF_MAX + 1;
  ret = bdacval_eq_expect_reject(
    fd, (unsigned long)(uintptr_t)&config, ERANGE,
    &g_bk7258_aud_validation_diag.eq_invalid_rejects);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_aud_get_diag(&after);
  if (ret < 0)
    {
      return ret;
    }

  if (after.eq_config_count != before.eq_config_count ||
      after.eq_reject_count - before.eq_reject_count != 6 ||
      after.eq_shadow_valid != before.eq_shadow_valid ||
      after.eq_requested != before.eq_requested ||
      after.eq_applied != before.eq_applied ||
      after.eq_last_config_hash != before.eq_last_config_hash ||
      after.eq_readback_count != before.eq_readback_count ||
      after.eq_readback_fail_count != before.eq_readback_fail_count ||
      (after.resources & BK7258_AUD_RESOURCE_EQ) != 0)
    {
      return -EPROTO;
    }

  bdacval_eq_init_config(&config, true);
  boundary_hash = bdacval_eq_hash(&config);
  if (ioctl(fd, BK7258_AUDIOIOC_SET_DAC_EQ,
            (unsigned long)(uintptr_t)&config) < 0)
    {
      return bdacval_errno();
    }

  memset(&config, 0xa5, sizeof(config));
  ret = bk7258_aud_get_diag(&after);
  if (ret < 0)
    {
      return ret;
    }

  if (after.eq_config_count != before.eq_config_count + 1 ||
      after.eq_shadow_valid != 1 || after.eq_requested != 1 ||
      after.eq_applied != 0 ||
      after.eq_last_config_hash != boundary_hash ||
      (after.resources & BK7258_AUD_RESOURCE_EQ) != 0)
    {
      return -EPROTO;
    }

  /* Only the canonical all-zero raw banks reach the running DAC.  This
   * validates enable-bit and zero-bank apply/readback/deconfigure lifecycle
   * without proving nonzero bank packing, Q format, pass-through response or
   * coefficient stability.
   */

  bdacval_eq_init_config(&config, false);
  safe_hash = bdacval_eq_hash(&config);
  if (ioctl(fd, BK7258_AUDIOIOC_SET_DAC_EQ,
            (unsigned long)(uintptr_t)&config) < 0)
    {
      return bdacval_errno();
    }

  g_bk7258_aud_validation_diag.expected_eq_config_hash = safe_hash;
  ret = bk7258_aud_get_diag(&after);
  if (ret < 0)
    {
      return ret;
    }

  if (after.eq_config_count != before.eq_config_count + 2 ||
      after.eq_reject_count - before.eq_reject_count != 6 ||
      after.eq_shadow_valid != 1 || after.eq_requested != 1 ||
      after.eq_applied != 0 || after.eq_last_config_hash != safe_hash ||
      after.eq_readback_count != before.eq_readback_count ||
      after.eq_readback_fail_count != before.eq_readback_fail_count ||
      (after.resources & BK7258_AUD_RESOURCE_EQ) != 0)
    {
      return -EPROTO;
    }

  return OK;
}

static int bdacval_eq_expect_running(int fd)
{
  struct bk7258_aud_eq_config_s config;
  struct bk7258_aud_diag_s diag;
  int ret;

  bdacval_eq_init_config(&config, false);
  ret = bdacval_eq_expect_reject(
    fd, (unsigned long)(uintptr_t)&config, EBUSY,
    &g_bk7258_aud_validation_diag.eq_runtime_rejects);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_aud_get_diag(&diag);
  if (ret < 0)
    {
      return ret;
    }

  if (diag.eq_shadow_valid != 1 || diag.eq_requested != 1 ||
      diag.eq_applied != 1 ||
      diag.eq_last_config_hash !=
        g_bk7258_aud_validation_diag.expected_eq_config_hash ||
      (diag.resources & BK7258_AUD_RESOURCE_EQ) == 0)
    {
      return -EPROTO;
    }

  return OK;
}

#endif /* CONFIG_BK7258_AUD_DAC_EQ */

static void bdacval_sample_monotonic(void)
{
  struct timespec ts;
  uint64_t now;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0 || ts.tv_sec < 0 ||
      ts.tv_nsec < 0 || ts.tv_nsec >= NSEC_PER_SEC)
    {
      g_bk7258_aud_validation_diag.monotonic_sample_errors++;
      return;
    }

  now = (uint64_t)ts.tv_sec * NSEC_PER_SEC + (uint32_t)ts.tv_nsec;
  if (g_bdacval_monotonic_valid && now < g_bdacval_monotonic_max_ns)
    {
      g_bk7258_aud_validation_diag.monotonic_regression_count++;
    }
  else
    {
      g_bdacval_monotonic_max_ns = now;
    }

  g_bdacval_monotonic_valid = true;
}

static uint32_t bdacval_tick_delta(clock_t later, clock_t earlier)
{
  bdacval_sample_monotonic();

  if (later < earlier)
    {
      g_bk7258_aud_validation_diag.tick_regression_count++;
      return 0;
    }

  return (uint32_t)(later - earlier);
}

static void bdacval_set_stage(enum bdacval_stage_e stage)
{
  g_bk7258_aud_validation_diag.stage = stage;
}

static void bdacval_publish_state(uint32_t state)
{
  __asm__ __volatile__("dmb sy" : : : "memory");
  g_bk7258_aud_validation_diag.state = state;
}

static void bdacval_note_cpu(void)
{
  int cpu = sched_getcpu();

  if (cpu >= 0 && cpu < 32)
    {
      g_bk7258_aud_validation_diag.producer_cpu_mask |= 1u << cpu;
    }
}

static void bdacval_deadline(struct timespec *deadline, int seconds)
{
  if (clock_gettime(CLOCK_REALTIME, deadline) < 0)
    {
      memset(deadline, 0, sizeof(*deadline));
    }

  deadline->tv_sec += seconds;
}

static bool bdacval_any_outstanding(const struct bdacval_cycle_s *cycle)
{
  unsigned int i;

  for (i = 0; i < BDACVAL_BUFFERS; i++)
    {
      if (cycle->outstanding[i])
        {
          return true;
        }
    }

  return false;
}

static int bdacval_buffer_index(const struct bdacval_cycle_s *cycle,
                                const struct ap_buffer_s *apb)
{
  unsigned int i;

  for (i = 0; i < BDACVAL_BUFFERS; i++)
    {
      if (cycle->buffers[i] == apb)
        {
          return (int)i;
        }
    }

  return -ENOENT;
}

static void bdacval_fill_tone(struct bdacval_cycle_s *cycle,
                              struct ap_buffer_s *apb, bool final)
{
  int16_t *samples = (int16_t *)apb->samp;
  uint32_t count = apb->nmaxbytes / BK7258_AUD_BYTES_PER_SAMPLE;
  uint32_t sample;

  for (sample = 0; sample < count; sample++)
    {
      samples[sample] = cycle->phase <
                        CONFIG_BK7258_AUD_SAMPLE_RATE / 2u ?
                        BDACVAL_TONE_AMPLITUDE :
                        -BDACVAL_TONE_AMPLITUDE;

      cycle->phase += BDACVAL_TONE_HZ;
      if (cycle->phase >= CONFIG_BK7258_AUD_SAMPLE_RATE)
        {
          cycle->phase -= CONFIG_BK7258_AUD_SAMPLE_RATE;
        }
    }

  apb->curbyte = 0;
  apb->nbytes = apb->nmaxbytes;
  apb->nsamples = count;
  apb->flags &= ~(AUDIO_APB_FINAL | AUDIO_APB_DEQUEUED |
                  AUDIO_APB_OUTPUT_ENQUEUED |
                  AUDIO_APB_OUTPUT_PROCESS);
  if (final)
    {
      apb->flags |= AUDIO_APB_FINAL;
    }
}

static int bdacval_enqueue(int fd, struct bdacval_cycle_s *cycle,
                           unsigned int index, bool final)
{
  struct audio_buf_desc_s desc;
  uint32_t delta;

  bdacval_note_cpu();

  if (index >= BDACVAL_BUFFERS || cycle->buffers[index] == NULL ||
      cycle->outstanding[index])
    {
      return -EINVAL;
    }

  bdacval_fill_tone(cycle, cycle->buffers[index], final);
  memset(&desc, 0, sizeof(desc));
  desc.numbytes = cycle->buffers[index]->nbytes;
  desc.u.buffer = cycle->buffers[index];

  if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER,
            (unsigned long)(uintptr_t)&desc) < 0)
    {
      return bdacval_errno();
    }

  if (cycle->dequeue_pending)
    {
      delta = bdacval_tick_delta(clock_systime_ticks(),
                                 cycle->dequeue_ready_tick);
      if (delta > g_bk7258_aud_validation_diag
                    .received_dequeue_to_enqueue_max_ticks)
        {
          g_bk7258_aud_validation_diag
            .received_dequeue_to_enqueue_max_ticks = delta;
        }

      cycle->dequeue_pending = false;
    }

  cycle->outstanding[index] = true;
  cycle->submitted++;
  g_bk7258_aud_validation_diag.submitted_buffers++;
  if (final)
    {
      cycle->final_index = (int32_t)index;
      cycle->final_enqueued = true;
      g_bk7258_aud_validation_diag.final_buffers++;
    }

  return OK;
}

static int bdacval_receive(mqd_t mq, struct bdacval_cycle_s *cycle,
                           const struct timespec *deadline,
                           uint16_t *message_id, unsigned int *index)
{
  struct mq_attr attr;
  struct audio_msg_s msg;
  clock_t received_tick;
  uint32_t delta;
  ssize_t received;
  int found;

  for (; ; )
    {
      received = mq_timedreceive(mq, (char *)&msg, sizeof(msg), NULL,
                                 deadline);
      received_tick = clock_systime_ticks();
      if (received < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return bdacval_errno();
        }

      if (received != sizeof(msg))
        {
          return -EPROTO;
        }

      bdacval_note_cpu();
      if (mq_getattr(mq, &attr) == 0 && attr.mq_curmsgs >= 0 &&
          (uint32_t)attr.mq_curmsgs >
          g_bk7258_aud_validation_diag.mq_depth_after_receive_max)
        {
          g_bk7258_aud_validation_diag.mq_depth_after_receive_max =
            (uint32_t)attr.mq_curmsgs;
        }

      *message_id = msg.msg_id;
      *index = UINT32_MAX;

      switch (msg.msg_id)
        {
          case AUDIO_MSG_DEQUEUE:
            if (msg.u.ptr == NULL)
              {
                return -EPROTO;
              }

            found = bdacval_buffer_index(
              cycle, (const struct ap_buffer_s *)msg.u.ptr);
            if (found < 0 || !cycle->outstanding[found])
              {
                return -EPROTO;
              }

            if (cycle->start_tick != 0 && cycle->cycle_number >= 1 &&
                cycle->cycle_number <= BDACVAL_CYCLES)
              {
                if (cycle->last_dequeue_tick == 0)
                  {
                    g_bk7258_aud_validation_diag
                      .first_dequeue_delay_ticks[cycle->cycle_number - 1] =
                      bdacval_tick_delta(received_tick,
                                         cycle->start_tick);
                  }
                else
                  {
                    delta = bdacval_tick_delta(received_tick,
                                               cycle->last_dequeue_tick);
                    if (delta > g_bk7258_aud_validation_diag
                                  .dequeue_interval_max_ticks)
                      {
                        g_bk7258_aud_validation_diag
                          .dequeue_interval_max_ticks = delta;
                      }
                  }
              }

            cycle->last_dequeue_tick = received_tick;
            cycle->dequeue_ready_tick = received_tick;
            cycle->dequeue_pending = true;

            cycle->outstanding[found] = false;
            cycle->dequeues++;
            g_bk7258_aud_validation_diag.dequeues++;
            if (found == cycle->final_index)
              {
                cycle->final_dequeued = true;
              }

            *index = (unsigned int)found;
            return OK;

          case AUDIO_MSG_COMPLETE:
            if (bdacval_any_outstanding(cycle) ||
                (cycle->final_enqueued && !cycle->final_dequeued))
              {
                return -EPROTO;
              }

            cycle->completes++;
            g_bk7258_aud_validation_diag.completes++;
            return OK;

          case AUDIO_MSG_IOERR:
            return msg.u.data == 0 ? -EIO : -(int)msg.u.data;

          case AUDIO_MSG_UNDERRUN:
            return -EPIPE;

          default:
            break;
        }
    }
}

static int bdacval_drain(mqd_t mq, struct bdacval_cycle_s *cycle,
                         uint32_t expected_completes)
{
  struct timespec deadline;
  uint16_t message_id;
  unsigned int index;
  int ret;

  bdacval_deadline(&deadline, BDACVAL_TIMEOUT_SEC);
  while (bdacval_any_outstanding(cycle) ||
         cycle->completes < expected_completes)
    {
      bdacval_set_stage(BDACVAL_STAGE_DRAIN);
      ret = bdacval_receive(mq, cycle, &deadline, &message_id, &index);
      if (ret < 0)
        {
          return ret;
        }
    }

  return cycle->completes == expected_completes ? OK : -EPROTO;
}

static int bdacval_cycle(uint32_t cycle_number)
{
  struct bdacval_cycle_s cycle;
  struct audio_caps_s query;
  struct audio_caps_desc_s caps;
  struct ap_buffer_info_s info;
  struct audio_buf_desc_s desc;
  struct bk7258_aud_diag_s driver_diag;
  struct mq_attr attr;
  struct timespec deadline;
  clock_t start_ioctl_tick;
  clock_t stream_start_tick = 0;
  uint16_t message_id;
  unsigned int index;
  unsigned int i;
  mqd_t mq = (mqd_t)-1;
  bool mq_registered = false;
  bool reserved = false;
  bool cleanup_required = false;
  bool clean = false;
  int fd = -1;
  int ret = OK;
  int cleanup_ret;

  memset(&cycle, 0, sizeof(cycle));
  cycle.final_index = -1;
  cycle.cycle_number = cycle_number;

  bdacval_set_stage(BDACVAL_STAGE_OPEN);
  fd = open(BDACVAL_DEVPATH, O_RDWR | O_CLOEXEC);
  if (fd < 0)
    {
      ret = bdacval_errno();
      goto out;
    }

  memset(&query, 0, sizeof(query));
  query.ac_len = sizeof(query);
  query.ac_type = AUDIO_TYPE_QUERY;
  query.ac_subtype = AUDIO_TYPE_QUERY;
  bdacval_set_stage(BDACVAL_STAGE_CAPS);
  if (ioctl(fd, AUDIOIOC_GETCAPS,
            (unsigned long)(uintptr_t)&query) < 0)
    {
      ret = bdacval_errno();
      goto out;
    }

  if ((query.ac_controls.b[0] & AUDIO_TYPE_OUTPUT) == 0 ||
      (query.ac_format.hw & (1 << (AUDIO_FMT_PCM - 1))) == 0)
    {
      ret = -EPROTO;
      goto out;
    }

#ifdef CONFIG_BK7258_AUD_DAC_EQ
  bdacval_set_stage(BDACVAL_STAGE_EQ_CONFIG);
  ret = bdacval_eq_pre_reserve(fd);
  if (ret < 0)
    {
      goto out;
    }
#endif

  bdacval_set_stage(BDACVAL_STAGE_RESERVE);
  cleanup_required = true;
  bdacval_sample_monotonic();
  if (ioctl(fd, AUDIOIOC_RESERVE, 0) < 0)
    {
      ret = bdacval_errno();
      goto out;
    }

  bdacval_sample_monotonic();

  reserved = true;
#ifdef CONFIG_BK7258_AUD_DAC_EQ
  bdacval_set_stage(BDACVAL_STAGE_EQ_CONFIG);
  ret = bdacval_eq_configure_reserved(fd);
  if (ret < 0)
    {
      goto out;
    }
#endif

  memset(&caps, 0, sizeof(caps));
  caps.caps.ac_len = sizeof(caps.caps);
  caps.caps.ac_type = AUDIO_TYPE_OUTPUT;
  caps.caps.ac_subtype = AUDIO_FMT_PCM;
  caps.caps.ac_channels = 1;
  caps.caps.ac_controls.hw[0] = CONFIG_BK7258_AUD_SAMPLE_RATE;
  caps.caps.ac_controls.b[3] =
    CONFIG_BK7258_AUD_SAMPLE_RATE >> 16;
  caps.caps.ac_controls.b[2] = BK7258_AUD_BITS_PER_SAMPLE;

  bdacval_set_stage(BDACVAL_STAGE_CONFIGURE);
  if (ioctl(fd, AUDIOIOC_CONFIGURE,
            (unsigned long)(uintptr_t)&caps) < 0)
    {
      ret = bdacval_errno();
      goto out;
    }

  memset(&info, 0, sizeof(info));
  bdacval_set_stage(BDACVAL_STAGE_BUFFER_INFO);
  if (ioctl(fd, AUDIOIOC_GETBUFFERINFO,
            (unsigned long)(uintptr_t)&info) < 0)
    {
      ret = bdacval_errno();
      goto out;
    }

  if (info.nbuffers != BDACVAL_BUFFERS ||
      info.buffer_size != CONFIG_BK7258_AUD_FRAME_SAMPLES *
                          BK7258_AUD_BYTES_PER_SAMPLE)
    {
      ret = -EPROTO;
      goto out;
    }

  memset(&attr, 0, sizeof(attr));
  attr.mq_maxmsg = info.nbuffers + 8;
  attr.mq_msgsize = sizeof(struct audio_msg_s);
  (void)mq_unlink(BDACVAL_MQNAME);

  bdacval_set_stage(BDACVAL_STAGE_MESSAGE_QUEUE);
  mq = mq_open(BDACVAL_MQNAME, O_RDWR | O_CREAT, 0644, &attr);
  if (mq == (mqd_t)-1)
    {
      ret = bdacval_errno();
      goto out;
    }

  if (ioctl(fd, AUDIOIOC_REGISTERMQ, (unsigned long)mq) < 0)
    {
      ret = bdacval_errno();
      goto out;
    }

  mq_registered = true;
  bdacval_set_stage(BDACVAL_STAGE_ALLOCATE);

  for (i = 0; i < BDACVAL_BUFFERS; i++)
    {
      memset(&desc, 0, sizeof(desc));
      desc.numbytes = info.buffer_size;
      desc.u.pbuffer = &cycle.buffers[i];
      if (ioctl(fd, AUDIOIOC_ALLOCBUFFER,
                (unsigned long)(uintptr_t)&desc) < 0 ||
          cycle.buffers[i] == NULL)
        {
          ret = cycle.buffers[i] == NULL ? -ENOMEM : bdacval_errno();
          goto out;
        }
    }

  bdacval_set_stage(BDACVAL_STAGE_PRIME);
  for (i = 0; i < BDACVAL_BUFFERS; i++)
    {
      ret = bdacval_enqueue(fd, &cycle, i, false);
      if (ret < 0)
        {
          goto out;
        }
    }

  bdacval_set_stage(BDACVAL_STAGE_START);
  bdacval_sample_monotonic();
  start_ioctl_tick = clock_systime_ticks();
  if (ioctl(fd, AUDIOIOC_START, 0) < 0)
    {
      ret = bdacval_errno();
      goto out;
    }

  stream_start_tick = clock_systime_ticks();
  bdacval_sample_monotonic();
  cycle.start_tick = stream_start_tick;
  if (cycle_number >= 1 && cycle_number <= BDACVAL_CYCLES)
    {
      g_bk7258_aud_validation_diag.start_ioctl_ticks[cycle_number - 1] =
        bdacval_tick_delta(stream_start_tick, start_ioctl_tick);
    }

#ifdef CONFIG_BK7258_AUD_DAC_EQ
  bdacval_set_stage(BDACVAL_STAGE_EQ_CONFIG);
  ret = bdacval_eq_expect_running(fd);
  if (ret < 0)
    {
      goto out;
    }
#endif

  bdacval_deadline(&deadline, BDACVAL_TIMEOUT_SEC);

  for (; ; )
    {
      bdacval_set_stage(BDACVAL_STAGE_RECEIVE);
      ret = bdacval_receive(mq, &cycle, &deadline, &message_id, &index);
      if (ret < 0)
        {
          goto out;
        }

      if (message_id == AUDIO_MSG_COMPLETE)
        {
          if (cycle_number != 2 || !cycle.final_enqueued ||
              !cycle.final_dequeued || bdacval_any_outstanding(&cycle))
            {
              ret = -EPROTO;
              goto out;
            }

          break;
        }

      if (message_id != AUDIO_MSG_DEQUEUE)
        {
          continue;
        }

      if (cycle_number == 1 &&
          cycle.dequeues >= BDACVAL_TARGET_DEQUEUES)
        {
          bdacval_set_stage(BDACVAL_STAGE_STOP);
          if (ioctl(fd, AUDIOIOC_STOP, 0) < 0)
            {
              ret = bdacval_errno();
              goto out;
            }

          g_bk7258_aud_validation_diag.manual_stop_cycles++;
          ret = bdacval_drain(mq, &cycle, 1);
          if (ret < 0)
            {
              goto out;
            }

          break;
        }

      if (cycle_number == 2 &&
          cycle.dequeues >= BDACVAL_TARGET_DEQUEUES &&
          !cycle.final_enqueued)
        {
          ret = bdacval_enqueue(fd, &cycle, index, true);
          if (ret < 0)
            {
              goto out;
            }

          continue;
        }

      if (!cycle.final_enqueued)
        {
          ret = bdacval_enqueue(fd, &cycle, index, false);
          if (ret < 0)
            {
              goto out;
            }
        }
    }

  if (cycle_number == 2)
    {
      ret = bdacval_drain(mq, &cycle, 1);
      if (ret < 0)
        {
          goto out;
        }

      g_bk7258_aud_validation_diag.natural_drain_cycles++;
    }

  if (bdacval_any_outstanding(&cycle))
    {
      ret = -EBUSY;
      goto out;
    }

  if (cycle_number >= 1 && cycle_number <= BDACVAL_CYCLES)
    {
      g_bk7258_aud_validation_diag.cycle_ticks[cycle_number - 1] =
        bdacval_tick_delta(clock_systime_ticks(), stream_start_tick);
    }

  bdacval_set_stage(BDACVAL_STAGE_RELEASE);
  bdacval_sample_monotonic();
  if (ioctl(fd, AUDIOIOC_RELEASE, 0) < 0)
    {
      ret = bdacval_errno();
      goto out;
    }

  reserved = false;
  cleanup_required = false;
  bdacval_sample_monotonic();

  /* Preserve each session's complete DVFS transition rather than relying
   * only on the final overwritten driver snapshot.
   */

  ret = bk7258_aud_get_diag(&driver_diag);
  if (ret < 0)
    {
      goto out;
    }

  if (cycle_number < 1 || cycle_number > BDACVAL_CYCLES)
    {
      ret = -ERANGE;
      goto out;
    }

  g_bk7258_aud_validation_diag
    .cycle_frequency_before_vote[cycle_number - 1] =
    driver_diag.frequency_before_vote;
  g_bk7258_aud_validation_diag
    .cycle_frequency_active[cycle_number - 1] =
    driver_diag.perf_frequency;
  g_bk7258_aud_validation_diag
    .cycle_frequency_after_release[cycle_number - 1] =
    driver_diag.frequency_after_release;

  if (driver_diag.frequency_before_vote != BDACVAL_IDLE_CPU_HZ ||
      driver_diag.perf_frequency != BDACVAL_ACTIVE_CPU_HZ ||
      driver_diag.frequency_after_release != BDACVAL_IDLE_CPU_HZ)
    {
      ret = -EPROTO;
      goto out;
    }

#ifdef CONFIG_BK7258_AUD_DAC_EQ
  if (driver_diag.eq_shadow_valid != 0 ||
      driver_diag.eq_requested != 0 || driver_diag.eq_applied != 0 ||
      (driver_diag.resources & BK7258_AUD_RESOURCE_EQ) != 0)
    {
      ret = -EPROTO;
      goto out;
    }
#endif

  if (ioctl(fd, AUDIOIOC_UNREGISTERMQ, (unsigned long)mq) < 0)
    {
      ret = bdacval_errno();
      goto out;
    }

  mq_registered = false;
  clean = true;
  g_bk7258_aud_validation_diag.cycles = cycle_number;

out:
  /* On an error, explicit SHUTDOWN runs while the MQ is still registered.
   * It synchronously quiesces the worker and produces every required
   * DEQUEUE before the APBs can be freed.
   */

  if (!clean && fd >= 0 && cleanup_required)
    {
      cleanup_ret = ioctl(fd, AUDIOIOC_SHUTDOWN, 0);
      if (cleanup_ret < 0 && ret == OK)
        {
          ret = bdacval_errno();
        }
      else if (cleanup_ret >= 0)
        {
          reserved = false;
          cleanup_required = false;
        }

      if (mq_registered)
        {
          struct timespec cleanup_deadline;

          bdacval_deadline(&cleanup_deadline, BDACVAL_TIMEOUT_SEC);
          while (bdacval_any_outstanding(&cycle))
            {
              cleanup_ret = bdacval_receive(mq, &cycle,
                                            &cleanup_deadline,
                                            &message_id, &index);
              if (cleanup_ret < 0)
                {
                  if (ret == OK)
                    {
                      ret = cleanup_ret;
                    }

                  break;
                }
            }
        }
    }

  if (fd >= 0 && !bdacval_any_outstanding(&cycle))
    {
      if (reserved)
        {
          cleanup_ret = ioctl(fd, AUDIOIOC_RELEASE, 0);
          if (cleanup_ret < 0 && ret == OK)
            {
              ret = bdacval_errno();
            }
          else if (cleanup_ret >= 0)
            {
              reserved = false;
              cleanup_required = false;
            }
        }

      if (!reserved && !cleanup_required && mq_registered)
        {
          cleanup_ret = ioctl(fd, AUDIOIOC_UNREGISTERMQ,
                              (unsigned long)mq);
          if (cleanup_ret < 0 && ret == OK)
            {
              ret = bdacval_errno();
            }
          else if (cleanup_ret >= 0)
            {
              mq_registered = false;
            }
        }

      if (!reserved && !cleanup_required && !mq_registered)
        {
          for (i = 0; i < BDACVAL_BUFFERS; i++)
            {
              if (cycle.buffers[i] != NULL)
                {
                  memset(&desc, 0, sizeof(desc));
                  desc.u.buffer = cycle.buffers[i];
                  cleanup_ret = ioctl(fd, AUDIOIOC_FREEBUFFER,
                                      (unsigned long)(uintptr_t)&desc);
                  if (cleanup_ret < 0 && ret == OK)
                    {
                      ret = bdacval_errno();
                    }

                  cycle.buffers[i] = NULL;
                }
            }

          cleanup_ret = close(fd);
          if (cleanup_ret < 0 && ret == OK)
            {
              ret = bdacval_errno();
            }

          fd = -1;
        }
    }

  if (mq != (mqd_t)-1 && !mq_registered)
    {
      mq_close(mq);
      (void)mq_unlink(BDACVAL_MQNAME);
    }

  return ret;
}

static void bdacval_publish_driver_delta(
  const struct bk7258_aud_diag_s *before,
  const struct bk7258_aud_diag_s *after)
{
  g_bk7258_aud_validation_diag.driver_state = after->state;
  g_bk7258_aud_validation_diag.driver_first_error = after->first_error;
  g_bk7258_aud_validation_diag.driver_resources = after->resources;
  g_bk7258_aud_validation_diag.driver_enqueue_delta =
    after->enqueue_count - before->enqueue_count;
  g_bk7258_aud_validation_diag.driver_dequeue_delta =
    after->dequeue_count - before->dequeue_count;
  g_bk7258_aud_validation_diag.driver_complete_delta =
    after->complete_count - before->complete_count;
  g_bk7258_aud_validation_diag.driver_submitted_delta =
    after->submitted_bytes - before->submitted_bytes;
  g_bk7258_aud_validation_diag.driver_played_delta =
    after->played_bytes - before->played_bytes;
  g_bk7258_aud_validation_diag.driver_dma_alloc_delta =
    after->dma_alloc_count - before->dma_alloc_count;
  g_bk7258_aud_validation_diag.driver_dma_start_delta =
    after->dma_start_count - before->dma_start_count;
  g_bk7258_aud_validation_diag.driver_dma_isr_delta =
    after->dma_isr_count - before->dma_isr_count;
  g_bk7258_aud_validation_diag.driver_final_drain_delta =
    after->final_drain_count - before->final_drain_count;
  g_bk7258_aud_validation_diag.driver_dma_stop_delta =
    after->dma_stop_count - before->dma_stop_count;
  g_bk7258_aud_validation_diag.driver_dma_deinit_delta =
    after->dma_deinit_count - before->dma_deinit_count;
  g_bk7258_aud_validation_diag.driver_dma_free_delta =
    after->dma_free_count - before->dma_free_count;
  g_bk7258_aud_validation_diag.driver_dac_start_delta =
    after->dac_start_count - before->dac_start_count;
  g_bk7258_aud_validation_diag.driver_dac_stop_delta =
    after->dac_stop_count - before->dac_stop_count;
  g_bk7258_aud_validation_diag.driver_pa_on_delta =
    after->pa_on_count - before->pa_on_count;
  g_bk7258_aud_validation_diag.driver_pa_off_delta =
    after->pa_off_count - before->pa_off_count;
  g_bk7258_aud_validation_diag.driver_underrun_delta =
    after->underrun_count - before->underrun_count;
  g_bk7258_aud_validation_diag.driver_pa_enabled = after->pa_enabled;
  g_bk7258_aud_validation_diag.driver_perf_before_frequency =
    before->perf_frequency;
  g_bk7258_aud_validation_diag.driver_perf_frequency =
    after->perf_frequency;
  g_bk7258_aud_validation_diag.driver_frequency_vote_delta =
    after->frequency_vote_count - before->frequency_vote_count;
  g_bk7258_aud_validation_diag.driver_frequency_release_delta =
    after->frequency_release_count - before->frequency_release_count;
  g_bk7258_aud_validation_diag.driver_frequency_before_vote =
    after->frequency_before_vote;
  g_bk7258_aud_validation_diag.driver_frequency_after_release =
    after->frequency_after_release;
  g_bk7258_aud_validation_diag.driver_dma_isr_cpu_mask =
    after->dma_isr_cpu_mask;
  g_bk7258_aud_validation_diag.driver_dma_isr_cpu_count[0] =
    after->dma_isr_cpu_count[0] - before->dma_isr_cpu_count[0];
  g_bk7258_aud_validation_diag.driver_dma_isr_cpu_count[1] =
    after->dma_isr_cpu_count[1] - before->dma_isr_cpu_count[1];
  g_bk7258_aud_validation_diag.driver_dma_isr_min_cycles[0] =
    after->dma_isr_min_cycles[0];
  g_bk7258_aud_validation_diag.driver_dma_isr_min_cycles[1] =
    after->dma_isr_min_cycles[1];
  g_bk7258_aud_validation_diag.driver_dma_isr_max_cycles[0] =
    after->dma_isr_max_cycles[0];
  g_bk7258_aud_validation_diag.driver_dma_isr_max_cycles[1] =
    after->dma_isr_max_cycles[1];
  g_bk7258_aud_validation_diag.driver_dma_isr_interval_ticks =
    after->dma_isr_interval_ticks - before->dma_isr_interval_ticks;
  g_bk7258_aud_validation_diag.driver_dma_isr_interval_count =
    after->dma_isr_interval_count - before->dma_isr_interval_count;
  g_bk7258_aud_validation_diag.driver_tick_regression_delta =
    after->tick_regression_count - before->tick_regression_count;
  g_bk7258_aud_validation_diag.driver_worker_wake_delta =
    after->worker_wake_count - before->worker_wake_count;
  g_bk7258_aud_validation_diag.driver_worker_cpu_mask =
    after->worker_cpu_mask;
  g_bk7258_aud_validation_diag.driver_worker_priority =
    after->worker_priority;
  g_bk7258_aud_validation_diag.driver_sem_backlog_max =
    after->sem_backlog_max;
  g_bk7258_aud_validation_diag.driver_sem_backlog_events_delta =
    after->sem_backlog_events - before->sem_backlog_events;
  g_bk7258_aud_validation_diag.driver_worker_service_count_delta =
    after->worker_service_count - before->worker_service_count;
  g_bk7258_aud_validation_diag.driver_worker_service_max_cycles =
    after->worker_service_max_cycles;
  g_bk7258_aud_validation_diag.driver_worker_service_max_ticks =
    after->worker_service_max_ticks;
  g_bk7258_aud_validation_diag
    .driver_worker_service_cpu_migrations_delta =
    after->worker_service_cpu_migrations -
    before->worker_service_cpu_migrations;
  g_bk7258_aud_validation_diag
    .driver_worker_isr_during_service_count_delta =
    after->worker_isr_during_service_count -
    before->worker_isr_during_service_count;
  g_bk7258_aud_validation_diag.driver_worker_isr_during_service_max =
    after->worker_isr_during_service_max;
  g_bk7258_aud_validation_diag.driver_callback_count_delta =
    after->dequeue_callback_count - before->dequeue_callback_count;
  g_bk7258_aud_validation_diag.driver_callback_max_cycles =
    after->dequeue_callback_max_cycles;
  g_bk7258_aud_validation_diag.driver_callback_max_ticks =
    after->dequeue_callback_max_ticks;
  g_bk7258_aud_validation_diag.driver_callback_cpu_migrations_delta =
    after->dequeue_callback_cpu_migrations -
    before->dequeue_callback_cpu_migrations;
  g_bk7258_aud_validation_diag.first_underrun_isr_sequence =
    after->first_underrun_isr_sequence;
  g_bk7258_aud_validation_diag.first_underrun_worker_wake =
    after->first_underrun_worker_wake;
  g_bk7258_aud_validation_diag.first_underrun_sem_value =
    after->first_underrun_sem_value;
  g_bk7258_aud_validation_diag.first_underrun_outstanding =
    after->first_underrun_outstanding;
  g_bk7258_aud_validation_diag.first_underrun_enqueue_count =
    after->first_underrun_enqueue_count;
  g_bk7258_aud_validation_diag.first_underrun_dequeue_count =
    after->first_underrun_dequeue_count;
  g_bk7258_aud_validation_diag.first_underrun_consume_sequence =
    after->first_underrun_consume_sequence;
  g_bk7258_aud_validation_diag.first_underrun_produce_sequence =
    after->first_underrun_produce_sequence;
  g_bk7258_aud_validation_diag.first_underrun_cpu =
    after->first_underrun_cpu;
#ifdef CONFIG_BK7258_AUD_DAC_EQ
  g_bk7258_aud_validation_diag.driver_eq_shadow_valid =
    after->eq_shadow_valid;
  g_bk7258_aud_validation_diag.driver_eq_requested =
    after->eq_requested;
  g_bk7258_aud_validation_diag.driver_eq_applied =
    after->eq_applied;
  g_bk7258_aud_validation_diag.driver_eq_config_delta =
    after->eq_config_count - before->eq_config_count;
  g_bk7258_aud_validation_diag.driver_eq_apply_delta =
    after->eq_apply_count - before->eq_apply_count;
  g_bk7258_aud_validation_diag.driver_eq_deconfig_delta =
    after->eq_deconfig_count - before->eq_deconfig_count;
  g_bk7258_aud_validation_diag.driver_eq_reject_delta =
    after->eq_reject_count - before->eq_reject_count;
  g_bk7258_aud_validation_diag.driver_eq_readback_delta =
    after->eq_readback_count - before->eq_readback_count;
  g_bk7258_aud_validation_diag.driver_eq_readback_fail_delta =
    after->eq_readback_fail_count - before->eq_readback_fail_count;
  g_bk7258_aud_validation_diag.driver_eq_last_config_hash =
    after->eq_last_config_hash;
#endif
}

static int bdacval_check_driver(
  const struct bk7258_aud_diag_s *before,
  const struct bk7258_aud_diag_s *after)
{
  bdacval_publish_driver_delta(before, after);

  if (after->magic != BK7258_AUD_DIAG_MAGIC ||
      after->version != BK7258_AUD_DIAG_VERSION ||
      after->size != sizeof(*after) ||
      after->state != BK7258_AUD_DIAG_RESET ||
      after->first_error != 0 || after->resources != 0 ||
      after->pa_enabled != 0 ||
      before->perf_frequency != BDACVAL_IDLE_CPU_HZ ||
      after->perf_frequency != BDACVAL_ACTIVE_CPU_HZ ||
      after->frequency_before_vote != BDACVAL_IDLE_CPU_HZ ||
      after->frequency_after_release != BDACVAL_IDLE_CPU_HZ)
    {
      return -EPROTO;
    }

#ifdef CONFIG_BK7258_AUD_DAC_EQ
  if (after->eq_shadow_valid != 0 || after->eq_requested != 0 ||
      after->eq_applied != 0 ||
      (after->resources & BK7258_AUD_RESOURCE_EQ) != 0 ||
      after->eq_last_config_hash !=
        g_bk7258_aud_validation_diag.expected_eq_config_hash)
    {
      return -EPROTO;
    }
#endif

  if (g_bk7258_aud_validation_diag.driver_enqueue_delta == 0 ||
      g_bk7258_aud_validation_diag.driver_enqueue_delta !=
      g_bk7258_aud_validation_diag.driver_dequeue_delta ||
      g_bk7258_aud_validation_diag.driver_complete_delta !=
      BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_submitted_delta == 0 ||
      g_bk7258_aud_validation_diag.driver_played_delta == 0 ||
      g_bk7258_aud_validation_diag.driver_played_delta >
      g_bk7258_aud_validation_diag.driver_submitted_delta)
    {
      return -EPROTO;
    }

  if (g_bk7258_aud_validation_diag.driver_dma_alloc_delta !=
      BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_dma_start_delta !=
      BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_dma_isr_delta == 0 ||
      g_bk7258_aud_validation_diag.driver_final_drain_delta !=
      BK7258_AUD_FINAL_DRAIN_FRAMES ||
      g_bk7258_aud_validation_diag.driver_dma_stop_delta !=
      BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_dma_deinit_delta !=
      BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_dma_free_delta !=
      BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_dac_start_delta !=
      BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_dac_stop_delta !=
      BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_pa_on_delta !=
      BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_pa_off_delta !=
      BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_frequency_vote_delta !=
      BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_frequency_release_delta !=
      BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_worker_priority !=
      CONFIG_BK7258_AUD_WORKER_PRIORITY ||
      g_bk7258_aud_validation_diag.producer_priority <=
      g_bk7258_aud_validation_diag.driver_worker_priority ||
      g_bk7258_aud_validation_diag.driver_underrun_delta != 0 ||
      g_bk7258_aud_validation_diag.driver_tick_regression_delta != 0 ||
      g_bk7258_aud_validation_diag.tick_regression_count != 0 ||
      g_bk7258_aud_validation_diag.monotonic_regression_count != 0 ||
      g_bk7258_aud_validation_diag.monotonic_sample_errors != 0)
    {
      return -EPROTO;
    }

#ifdef CONFIG_BK7258_AUD_DAC_EQ
  if (g_bk7258_aud_validation_diag.eq_pre_reserve_rejects !=
        BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.eq_invalid_rejects !=
        6u * BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.eq_runtime_rejects !=
        BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_eq_config_delta !=
        BDACVAL_EQ_CONFIGS_PER_CYCLE * BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_eq_apply_delta !=
        BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_eq_deconfig_delta !=
        BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_eq_reject_delta !=
        BDACVAL_EQ_REJECTS_PER_CYCLE * BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_eq_readback_delta !=
        BDACVAL_EQ_READBACKS_PER_CYCLE * BDACVAL_CYCLES ||
      g_bk7258_aud_validation_diag.driver_eq_readback_fail_delta != 0 ||
      g_bk7258_aud_validation_diag.driver_eq_shadow_valid != 0 ||
      g_bk7258_aud_validation_diag.driver_eq_requested != 0 ||
      g_bk7258_aud_validation_diag.driver_eq_applied != 0 ||
      g_bk7258_aud_validation_diag.driver_eq_last_config_hash !=
        g_bk7258_aud_validation_diag.expected_eq_config_hash)
    {
      return -EPROTO;
    }
#endif

  return OK;
}

static int bdacval_thread(int argc, char **argv)
{
  struct sched_param param;
  struct bk7258_aud_diag_s before;
  struct bk7258_aud_diag_s after;
  uint32_t cycle;
  bool before_valid = false;
  bool driver_published = false;
  int diag_ret;
  int ret;

  (void)argc;
  (void)argv;

  memset((void *)&g_bk7258_aud_validation_diag, 0,
         sizeof(g_bk7258_aud_validation_diag));
  g_bdacval_monotonic_max_ns = 0;
  g_bdacval_monotonic_valid = false;
  g_bk7258_aud_validation_diag.magic = BDACVAL_MAGIC;
  g_bk7258_aud_validation_diag.version = BDACVAL_VERSION;
  g_bk7258_aud_validation_diag.size =
    sizeof(g_bk7258_aud_validation_diag);
  g_bk7258_aud_validation_diag.stage = BDACVAL_STAGE_INIT;
  g_bk7258_aud_validation_diag.tick_frequency = CLK_TCK;
  memset(&param, 0, sizeof(param));
  if (sched_getparam(0, &param) == 0)
    {
      g_bk7258_aud_validation_diag.producer_priority =
        (uint32_t)param.sched_priority;
    }

  bdacval_note_cpu();
  bdacval_sample_monotonic();
  bdacval_publish_state(BDACVAL_RUNNING);

  usleep(BDACVAL_START_DELAY_US);
  ret = bk7258_aud_get_diag(&before);
  if (ret < 0)
    {
      goto done;
    }

  before_valid = true;

  for (cycle = 1; cycle <= BDACVAL_CYCLES; cycle++)
    {
      ret = bdacval_cycle(cycle);
      if (ret < 0)
        {
          goto done;
        }

      syslog(LOG_INFO, "BDACVAL cycle=%" PRIu32 "/%u PASS mode=%s\n",
             cycle, BDACVAL_CYCLES,
             cycle == 1 ? "stop" : "final");
    }

  bdacval_set_stage(BDACVAL_STAGE_DIAGNOSTIC);
  ret = bk7258_aud_get_diag(&after);
  if (ret < 0)
    {
      goto done;
    }

  ret = bdacval_check_driver(&before, &after);
  driver_published = true;

done:
  /* A failed lifecycle cycle has already completed its synchronous cleanup.
   * Publish the remaining driver state before the terminal validation state
   * even when execution never reached the normal diagnostic stage.
   */

  if (before_valid && !driver_published)
    {
      diag_ret = bk7258_aud_get_diag(&after);
      if (diag_ret == OK)
        {
          bdacval_publish_driver_delta(&before, &after);
        }
      else if (ret == OK)
        {
          ret = diag_ret;
        }
    }

  g_bk7258_aud_validation_diag.result = ret;
  if (ret == OK)
    {
      syslog(LOG_INFO,
             "BDACVAL PASS cycles=%u buffers=%" PRIu32
             " dma_isr=%" PRIu32 " played=%" PRIu32
             " pa=%" PRIu32 "/%" PRIu32 "\n",
             BDACVAL_CYCLES,
             g_bk7258_aud_validation_diag.driver_enqueue_delta,
             g_bk7258_aud_validation_diag.driver_dma_isr_delta,
             g_bk7258_aud_validation_diag.driver_played_delta,
             g_bk7258_aud_validation_diag.driver_pa_on_delta,
             g_bk7258_aud_validation_diag.driver_pa_off_delta);
      bdacval_publish_state(BDACVAL_PASSED);
    }
  else
    {
      syslog(LOG_ERR,
             "BDACVAL FAIL stage=%" PRIu32 " cycle=%" PRIu32
             " ret=%d resources=0x%08" PRIx32 "\n",
             g_bk7258_aud_validation_diag.stage,
             g_bk7258_aud_validation_diag.cycles, ret,
             g_bk7258_aud_validation_diag.driver_resources);
      bdacval_publish_state(BDACVAL_FAILED);
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_aud_validation_start(void)
{
  int priority;
  int ret;

  /* Match the bounded feeder priority used by the standard NuttX nxplayer
   * and nxlooper clients.  The lower-half refills a completed DMA slot before
   * publishing DEQUEUE, then runs one level below this producer so the newly
   * woken feeder can return one buffer before another DMA token forms a
   * continuous worker chain.  The selected profile verifies the complete
   * producer > worker > transport ordering at runtime.
   */

  priority = sched_get_priority_max(SCHED_FIFO) - 9;
  ret = kthread_create("bdac-validate", priority,
                       BDACVAL_STACKSIZE, bdacval_thread, NULL);
  if (ret < 0)
    {
      memset((void *)&g_bk7258_aud_validation_diag, 0,
             sizeof(g_bk7258_aud_validation_diag));
      g_bk7258_aud_validation_diag.magic = BDACVAL_MAGIC;
      g_bk7258_aud_validation_diag.version = BDACVAL_VERSION;
      g_bk7258_aud_validation_diag.size =
        sizeof(g_bk7258_aud_validation_diag);
      g_bk7258_aud_validation_diag.result = ret;
      g_bk7258_aud_validation_diag.stage = BDACVAL_STAGE_INIT;
      bdacval_publish_state(BDACVAL_FAILED);
      return ret;
    }

  return OK;
}

#endif /* CONFIG_BK7258_AUD_LIFECYCLE_VALIDATION */
