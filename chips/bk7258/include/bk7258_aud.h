/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_aud.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 analog speaker playback lower half.
 *
 * This AP-only wrapper owns the AUD DAC submodule and one repeat-mode GDMA
 * channel.  It publishes mono signed 16-bit PCM through NuttX's standard
 * audio upper half.  The selected physical board owns the amplifier GPIO,
 * polarity and delays; the dedicated bk7258_mic lower half separately owns
 * capture.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AUD_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AUD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AUD_DAC_EQ
#  include <nuttx/fs/ioctl.h>
#endif

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default /dev/audioN device name. */

#ifndef CONFIG_BK7258_AUD_DEVNAME
#  define CONFIG_BK7258_AUD_DEVNAME    "pcm0p"
#endif

#define BK7258_AUD_BITS_PER_SAMPLE     16u
#define BK7258_AUD_BYTES_PER_SAMPLE    2u
#define BK7258_AUD_FINAL_DRAIN_FRAMES  2u

#ifdef CONFIG_BK7258_AUD_DAC_EQ
#  define BK7258_AUD_EQ_CONFIG_VERSION       1u
#  define BK7258_AUD_EQ_BANK_COUNT           4u
#  define BK7258_AUD_EQ_COEFF_COUNT          5u
#  define BK7258_AUD_EQ_COEFF_A1             0u
#  define BK7258_AUD_EQ_COEFF_A2             1u
#  define BK7258_AUD_EQ_COEFF_B0             2u
#  define BK7258_AUD_EQ_COEFF_B1             3u
#  define BK7258_AUD_EQ_COEFF_B2             4u
#  define BK7258_AUD_EQ_COEFF_MIN            (-2097152)
#  define BK7258_AUD_EQ_COEFF_MAX            2097151
#  define BK7258_AUD_EQ_FLAG_ENABLE          (1u << 0)
#  define BK7258_AUD_EQ_FLAG_MASK            BK7258_AUD_EQ_FLAG_ENABLE

/* Private lower-half ioctl.  AUDIO_FU_EQUALIZER remains unadvertised. */

#  define BK7258_AUDIOIOC_SET_DAC_EQ         _AUDIOIOC(0x80)
#endif

#define BK7258_AUD_DIAG_MAGIC          0x43414442u /* "BDAC" */
#ifdef CONFIG_BK7258_AUD_DAC_EQ
#  define BK7258_AUD_DIAG_VERSION      6u
#else
#  define BK7258_AUD_DIAG_VERSION      5u
#endif
#define BK7258_AUD_DIAG_CPU_SLOTS      2u

#define BK7258_AUD_RESOURCE_DAC        (1u << 0)
#define BK7258_AUD_RESOURCE_DMA        (1u << 1)
#define BK7258_AUD_RESOURCE_RING       (1u << 2)
#define BK7258_AUD_RESOURCE_WORKER     (1u << 3)
#define BK7258_AUD_RESOURCE_STREAM     (1u << 4)
#define BK7258_AUD_RESOURCE_PA         (1u << 5)
#define BK7258_AUD_RESOURCE_FREQUENCY  (1u << 6)
#ifdef CONFIG_BK7258_AUD_DAC_EQ
#  define BK7258_AUD_RESOURCE_EQ       (1u << 7)
#endif

#ifdef CONFIG_BK7258_AUD_DAC_EQ

/* The immutable v3.1.1.9 HAL keeps only bits 21:0 of every coefficient.
 * Values in this ABI must therefore be sign-extended signed-22 raw words.
 * No Q format or filter-stability policy is implied by this transport ABI.
 */

struct bk7258_aud_eq_config_s
{
  uint16_t version;
  uint16_t size;
  uint32_t flags;
  int32_t  coeff[BK7258_AUD_EQ_BANK_COUNT][BK7258_AUD_EQ_COEFF_COUNT];
};

#endif

enum bk7258_aud_diag_state_e
{
  BK7258_AUD_DIAG_RESET = 0,
  BK7258_AUD_DIAG_RESERVED,
  BK7258_AUD_DIAG_CONFIGURED,
  BK7258_AUD_DIAG_RUNNING,
  BK7258_AUD_DIAG_DRAINED,
  BK7258_AUD_DIAG_FAULT
};

struct bk7258_aud_diag_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t state;
  int32_t  first_error;
  uint32_t samplerate;
  uint32_t channels;
  uint32_t bits;
  uint32_t digital_gain;
  uint32_t analog_gain;
  uint32_t resources;
  uint32_t enqueue_count;
  uint32_t dequeue_count;
  uint32_t complete_count;
  uint32_t submitted_bytes;
  uint32_t played_bytes;
  uint32_t dma_alloc_count;
  uint32_t dma_start_count;
  uint32_t dma_isr_count;
  uint32_t final_drain_count;
  uint32_t dma_stop_count;
  uint32_t dma_deinit_count;
  uint32_t dma_free_count;
  uint32_t dac_start_count;
  uint32_t dac_stop_count;
  uint32_t pa_on_count;
  uint32_t pa_off_count;
  uint32_t underrun_count;
  uint32_t pa_enabled;

  /* perf_frequency is the frequency at which the measured DMA-service
   * cycle counters ran.  It deliberately retains the active session value
   * after RELEASE so post-mortem cycle conversion remains correct.
   */

  uint32_t perf_frequency;
  uint32_t frequency_vote_count;
  uint32_t frequency_release_count;
  uint32_t frequency_before_vote;
  uint32_t frequency_after_release;
  uint32_t dma_isr_cpu_mask;
  uint32_t dma_isr_cpu_count[BK7258_AUD_DIAG_CPU_SLOTS];
  uint32_t dma_isr_min_cycles[BK7258_AUD_DIAG_CPU_SLOTS];
  uint32_t dma_isr_max_cycles[BK7258_AUD_DIAG_CPU_SLOTS];
  uint32_t dma_isr_interval_ticks;
  uint32_t dma_isr_interval_count;
  uint32_t tick_regression_count;
  uint32_t worker_wake_count;
  uint32_t worker_cpu_mask;
  uint32_t worker_priority;
  uint32_t sem_backlog_max;
  uint32_t sem_backlog_events;
  uint32_t worker_service_count;
  uint32_t worker_service_max_cycles;
  uint32_t worker_service_max_ticks;
  uint32_t worker_service_cpu_migrations;
  uint32_t worker_isr_during_service_count;
  uint32_t worker_isr_during_service_max;
  uint32_t dequeue_callback_count;
  uint32_t dequeue_callback_max_cycles;
  uint32_t dequeue_callback_max_ticks;
  uint32_t dequeue_callback_cpu_migrations;
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
  uint32_t eq_shadow_valid;
  uint32_t eq_requested;
  uint32_t eq_applied;
  uint32_t eq_config_count;
  uint32_t eq_apply_count;
  uint32_t eq_deconfig_count;
  uint32_t eq_reject_count;
  uint32_t eq_last_config_hash;
  uint32_t eq_readback_count;
  uint32_t eq_readback_fail_count;
#endif
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_AUD
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_aud_initialize
 *
 * Description:
 *   Construct the BK7258 DAC playback lower half and register it at
 *   /dev/audio/<CONFIG_BK7258_AUD_DEVNAME>.  Initialization claims the board
 *   PA GPIO in its inactive state; reserve() owns the DVFS vote, while the
 *   DMA, DAC, ring and worker remain lazy until start().
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.  Calling it twice is
 *   harmless and returns OK.
 *
 ****************************************************************************/

int bk7258_aud_initialize(void);

int bk7258_aud_get_diag(struct bk7258_aud_diag_s *diag);

#ifdef CONFIG_BK7258_AUD_LIFECYCLE_VALIDATION
int bk7258_aud_validation_start(void);
#endif

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_AUD */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AUD_H */
