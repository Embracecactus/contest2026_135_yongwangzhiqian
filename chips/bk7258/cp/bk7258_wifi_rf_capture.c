/****************************************************************************
 * chips/bk7258/cp/bk7258_wifi_rf_capture.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bounded diagnostic capture at the immutable v3.1.1.9 PHY boundary.
 * Raw debug words are not labelled as IQ or environmental reflections.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_wifi_rf_capture.h>

#include <driver/adc.h>

#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Pinned la_driver.c.obj uses PHY diagnostic source zero and clock source
 * one. The length is a count of 32-bit words. CP owns the physical block.
 */

#define BKRF_LA_BASE          0x45070000u
#define BKRF_LA_RESET         (BKRF_LA_BASE + 0x08u)
#define BKRF_LA_CONTROL       (BKRF_LA_BASE + 0x0cu)
#define BKRF_LA_VALUE         (BKRF_LA_BASE + 0x10u)
#define BKRF_LA_MASK          (BKRF_LA_BASE + 0x14u)
#define BKRF_LA_ADDRESS       (BKRF_LA_BASE + 0x1cu)
#define BKRF_LA_ENABLE        (1u << 0)
#define BKRF_LA_TRIGGER       (1u << 1)
#define BKRF_LA_CLOCK         (1u << 8)
#define BKRF_GUARD_WORDS      8u
#define BKRF_SENTINEL         0xa5c35a3cu
#define BKRF_RC_TX_MAP        0x4980c088u
#define BKRF_RC_RX_ADC        (1u << 7)
#define BKRF_RC_TX_INDEX      (0x78u / sizeof(uint32_t))
#define BKRF_RC_RX_MAP        0x4980c084u
#define BKRF_CRM_FORCE0       0x4985000cu
#define BKRF_CRM_FORCE1       0x49850010u
#define BKRF_FORCE0_MASK      (1u << 27)
#define BKRF_FORCE1_MASK      ((15u << 12) | (1u << 8) | (1u << 3))
#define BKRF_RX_ANALOG_MASK   0x0000bc80u
#define BKRF_TSSI_ENABLE      ((1u << 17) | (1u << 18))
#define BKRF_TSSI_SAMPLES     32u
#define BKRF_TSSI_GUARD       0xa53cu
#define BKRF_RC_TSSI_CALC     0x4980c064u
#define BKRF_RC_FORCE         0x4980c004u
#define BKRF_RC_FORCE_INDEX   1u
#define BKRF_RC_RX_INDEX      (0x74u / sizeof(uint32_t))
#define BKRF_RC_FORCE_MAP     0x4980c07cu
#define BKRF_RC_MAP_INDEX     (0x6cu / sizeof(uint32_t))
#define BKRF_TRX_MEASURE      0x4980c200u
#define BKRF_TRX_TAP_MASK     0x00000780u
#define BKRF_TRX_POWER_TAP    0x00000380u
#define BKRF_TRX_IQ_TAP       0x00000580u
#define BKRF_TRX_FILTER       0x4980c218u
#define BKRF_TRX_FILTER_INDEX 6u
#define BKRF_TRX_CAPCAL       (1u << 3)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Prefix recovered from the SDK object's DWARF crm_funcs_t layout. Keep
 * this private; public chip consumers never see the vendor function table.
 */

struct bkrf_crm_s
{
  uint32_t (*clock_get)(void);
  uint32_t (*diag_get)(void);
  void (*diag_set)(uint32_t value);
  void (*adc_clock_set)(uint8_t value);
  void (*clock_set)(uint32_t value);
  void (*rc_force_set)(uint8_t value);
  void (*ahb_force_set)(uint8_t value);
  void (*mdm_reset)(void);
  void (*active_force_set)(uint8_t value);
  void (*rx_force_set)(uint8_t value);
};

/* Private prefix of the pinned SW_TXQ sk_buff. The SDK owns allocation,
 * queuing and completion. A standalone management buffer avoids the SDK
 * raw handler's failure path freeing an interior PBUF_RAM payload.
 */

struct bkrf_skb_s
{
  void *next;
  void *prev;
  uint8_t *payload;
  uint32_t len;
  void *descriptor;
  void *pbuf;
  void (*callback)(void *arg, bool acknowledged);
  void *arg;
  uint8_t vif;
  uint8_t sta;
};

struct bkrf_la_s
{
  uint32_t reset;
  uint32_t control;
  uint32_t value;
  uint32_t mask;
  uint32_t address;
  uint32_t clock;
  uint32_t diag;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static_assert(offsetof(struct bkrf_crm_s, clock_set) == 16,
               "v3.1.1.9 CRM prefix ABI changed");
static_assert(offsetof(struct bkrf_crm_s, rx_force_set) == 36,
               "v3.1.1.9 CRM clock-force ABI changed");
static_assert(offsetof(struct bkrf_skb_s, payload) == 8 &&
              offsetof(struct bkrf_skb_s, callback) == 24 &&
              offsetof(struct bkrf_skb_s, vif) == 32,
              "v3.1.1.9 SW_TXQ prefix ABI changed");

extern const struct bkrf_crm_s *g_crm_funcs_t;
extern struct bkrf_skb_s *alloc_skb_for_tx_mgmt(uint32_t len);
extern uint8_t rwm_get_monitor_vif_idx(void);
extern int bmsg_tx_mgmt_sender(struct bkrf_skb_s *skb);
extern void os_free(void *ptr);
extern int32_t skb_cnt;
extern uint32_t BK7236_RC_RAM[];
extern uint32_t BK7236_TRX_RAM[];

static mutex_t g_bkrf_lock = NXMUTEX_INITIALIZER;
static volatile bool g_bkrf_tx_pending;
static uint32_t g_bkrf_dma[BK7258_RF_CAPTURE_MAX_WORDS + BKRF_GUARD_WORDS]
  __attribute__((aligned(32)));

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bkrf_tx_complete(void *arg, bool acknowledged)
{
  /* Broadcast completion does not require an acknowledgement. */

  g_bkrf_tx_pending = false;
}

static int bkrf_tx_wait(clock_t started, uint32_t timeout_ms)
{
  int ret;

  while (g_bkrf_tx_pending)
    {
      if (TICK2MSEC(clock_systime_ticks() - started) >= timeout_ms)
        {
          return -ETIMEDOUT;
        }

      ret = nxsig_usleep(1000);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

static int bkrf_send_frame(void)
{
  struct bkrf_skb_s *skb;
  uint8_t vif = rwm_get_monitor_vif_idx();
  uint8_t *packet;
  int ret;

  if (g_bkrf_tx_pending || vif == 0xff)
    {
      return -EBUSY;
    }

  skb = alloc_skb_for_tx_mgmt(256);
  if (skb == NULL)
    {
      return -ENOMEM;
    }

  /* Keep normal radio power/rate policy and at most one frame in flight. */

  packet = skb->payload;
  memset(packet, 0x5a, 256);
  packet[0] = 0x08;
  packet[1] = 0;
  packet[2] = packet[3] = 0;
  memset(packet + 4, 0xff, 6);
  memcpy(packet + 10, "\x02\x42\x4b\x52\x46\x01", 6);
  memcpy(packet + 16, packet + 10, 6);
  packet[22] = packet[23] = 0;
  memcpy(packet + 24, "\xaa\xaa\x03\x00\x00\x00\x88\xb5", 8);
  skb->vif = vif;
  skb->sta = 0xff;
  skb->callback = bkrf_tx_complete;
  skb->arg = (void *)&g_bkrf_tx_pending;
  g_bkrf_tx_pending = true;
  ret = bmsg_tx_mgmt_sender(skb);
  if (ret != 0)
    {
      /* A rejected queue push retains ownership at the caller. */

      g_bkrf_tx_pending = false;
      os_free(packet);
      os_free(skb);
      skb_cnt--;
    }

  return ret;
}

static int bkrf_la_prepare(uint32_t count, uint32_t diag, uint32_t trigger,
                           struct bkrf_la_s *saved)
{
  unsigned int i;

  saved->control = getreg32(BKRF_LA_CONTROL);
  if ((saved->control & BKRF_LA_ENABLE) != 0)
    {
      return -EBUSY;
    }

  saved->reset = getreg32(BKRF_LA_RESET);
  saved->value = getreg32(BKRF_LA_VALUE);
  saved->mask = getreg32(BKRF_LA_MASK);
  saved->address = getreg32(BKRF_LA_ADDRESS);
  saved->clock = g_crm_funcs_t->clock_get();
  saved->diag = g_crm_funcs_t->diag_get();
  for (i = 0; i < BK7258_RF_CAPTURE_MAX_WORDS + BKRF_GUARD_WORDS; i++)
    {
      g_bkrf_dma[i] = BKRF_SENTINEL;
    }

  up_clean_dcache((uintptr_t)g_bkrf_dma,
                  (uintptr_t)(g_bkrf_dma +
                    BK7258_RF_CAPTURE_MAX_WORDS + BKRF_GUARD_WORDS));
  putreg32(saved->reset | 1u, BKRF_LA_RESET);
  putreg32((uint32_t)(uintptr_t)g_bkrf_dma, BKRF_LA_ADDRESS);
  putreg32((saved->control & 0x8f0u) | (count << 12) |
           BKRF_LA_CLOCK | BKRF_LA_TRIGGER, BKRF_LA_CONTROL);
  putreg32(trigger, BKRF_LA_VALUE);
  putreg32(trigger, BKRF_LA_MASK);
  g_crm_funcs_t->diag_set(diag);
  return OK;
}

static bool bkrf_la_restore(const struct bkrf_la_s *saved)
{
  /* Stop the DMA producer before changing its destination. */

  modifyreg32(BKRF_LA_CONTROL, BKRF_LA_ENABLE, 0);
  UP_DSB();
  putreg32(saved->reset & ~1u, BKRF_LA_RESET);
  g_crm_funcs_t->clock_set(saved->clock);
  g_crm_funcs_t->diag_set(saved->diag);
  putreg32(saved->value, BKRF_LA_VALUE);
  putreg32(saved->mask, BKRF_LA_MASK);
  putreg32(saved->address, BKRF_LA_ADDRESS);
  putreg32(saved->control, BKRF_LA_CONTROL);
  putreg32(saved->reset, BKRF_LA_RESET);
  UP_DSB();
  return getreg32(BKRF_LA_CONTROL) == saved->control &&
         getreg32(BKRF_LA_RESET) == saved->reset &&
         getreg32(BKRF_LA_ADDRESS) == saved->address &&
         getreg32(BKRF_LA_VALUE) == saved->value &&
         getreg32(BKRF_LA_MASK) == saved->mask &&
         g_crm_funcs_t->clock_get() == saved->clock &&
         g_crm_funcs_t->diag_get() == saved->diag;
}

static int bkrf_tssi_capture(uint32_t count, uint32_t timeout_ms,
                             bool self_tx, uint32_t tap, bool calc,
                             uint32_t *words,
                             struct bk7258_rf_capture_result_s *result)
{
  struct bkrf_la_s saved_la;
  adc_config_t config;
  uint16_t samples[BKRF_TSSI_SAMPLES + BKRF_GUARD_WORDS];
  uint32_t saved_shadow;
  uint32_t saved_trx_shadow;
  uint32_t saved_filter_shadow;
  uint32_t elapsed;
  clock_t started;
  unsigned int i;
  int cleanup = OK;
  int ret;
  bool la_prepared = false;

  /* ADC8 is the chip's TSSIO input. Reuse the SDK ADC owner and interrupt
   * path, also shared by temperature measurement and the AP ADC server.
   * TSSI is a TX-side measurement, not RF ADC IQ or a confirmed echo.
   */

  result->rx_status = -ENOTSUP;
  result->rx_trigger_mask = 0;
  ret = bk_adc_acquire();
  if (ret != 0)
    {
      result->adc_error = ret;
      return -EBUSY;
    }

  ret = bk_adc_init(ADC_8);
  if (ret != 0)
    {
      result->adc_error = ret;
      bk_adc_release();
      return -EIO;
    }

  result->txmap_before = getreg32(BKRF_RC_TX_MAP);
  result->trx_before = getreg32(BKRF_TRX_MEASURE);
  result->filter_before = getreg32(BKRF_TRX_FILTER);
  result->calc_before = getreg32(BKRF_RC_TSSI_CALC);
  result->estimate_min = (result->calc_before >> 8) & 0xffu;
  result->estimate_max = result->estimate_min;
  saved_trx_shadow = BK7236_TRX_RAM[0];
  saved_filter_shadow = BK7236_TRX_RAM[BKRF_TRX_FILTER_INDEX];
  saved_shadow = BK7236_RC_RAM[BKRF_RC_TX_INDEX];
  BK7236_RC_RAM[BKRF_RC_TX_INDEX] = saved_shadow | BKRF_TSSI_ENABLE;
  putreg32(result->txmap_before | BKRF_TSSI_ENABLE, BKRF_RC_TX_MAP);
  if (tap != 0)
    {
      /* Pinned TRX0 bits: en_Pcal=9, en_IQcal=10, tssranges=7..8.
       * Select a measurement tap; retain PA bias/gain and switches.
       * This selector is a hypothesis, not antenna-response proof.
       */

      BK7236_TRX_RAM[0] = (saved_trx_shadow & ~BKRF_TRX_TAP_MASK) | tap;
      putreg32((result->trx_before & ~BKRF_TRX_TAP_MASK) | tap,
               BKRF_TRX_MEASURE);
    }

  if (tap == BKRF_TRX_IQ_TAP)
    {
      /* Factory IQ measurement clears capcal_sel in TRX6. This only
       * selects an internal measurement input, not antenna RX during TX.
       */

      BK7236_TRX_RAM[BKRF_TRX_FILTER_INDEX] =
        saved_filter_shadow & ~BKRF_TRX_CAPCAL;
      putreg32(result->filter_before & ~BKRF_TRX_CAPCAL, BKRF_TRX_FILTER);
    }

  if (calc)
    {
      /* Pinned calibration start/stop directly change RC0x19 bit 0.
       * No shadow setter or estimate update contract was found. Observe
       * bits 8..15 without interpreting them as calibrated RF power.
       */

      modifyreg32(BKRF_RC_TSSI_CALC, 0, 1u);
    }

  UP_DSB();

  memset(&config, 0, sizeof(config));
  config.chan = ADC_8;
  config.adc_mode = ADC_CONTINUOUS_MODE;
  config.clk = 2600000;
  config.src_clk = ADC_SCLK_XTAL_26M;
  config.saturate_mode = ADC_SATURATE_MODE_3;
  config.steady_ctrl = 7;
  config.vol_div = ADC_VOL_DIV_1;
  ret = bk_adc_set_config(&config);
  result->adc_error = ret;
  ret = ret == 0 ? OK : -EIO;
  if (ret == OK)
    {
      /* The existing FEDAC DMA trigger marks a real TX waveform, rather
       * than submission or its delayed callback. Check completion at ADC
       * block boundaries only: this is not sample-accurate alignment.
       * Arm the same observer for baseline captures to retain its load.
       */

      ret = bkrf_la_prepare(BK7258_RF_CAPTURE_MAX_WORDS, 0x00065958u,
                            0x10010000u, &saved_la);
      la_prepared = ret == OK;
      if (la_prepared)
        {
          result->control_before = saved_la.control;
          UP_DSB();
          modifyreg32(BKRF_LA_CONTROL, 0, BKRF_LA_ENABLE);
        }
    }

  if (ret == OK && tap == BKRF_TRX_IQ_TAP)
    {
      result->adc_error = bk_adc_enable_bypass_clalibration();
      ret = result->adc_error == 0 ? OK : -EIO;
    }

  if (ret == OK)
    {
      result->adc_error = bk_adc_start();
      ret = result->adc_error == 0 ? OK : -EIO;
    }

  result->guard_ok = 1;
  started = clock_systime_ticks();
  while (ret == OK && (result->count < count ||
                      (self_tx && g_bkrf_tx_pending)))
    {
      elapsed = TICK2MSEC(clock_systime_ticks() - started);
      if (elapsed >= timeout_ms)
        {
          ret = -ETIMEDOUT;
          break;
        }

      for (i = 0; i < BKRF_TSSI_SAMPLES + BKRF_GUARD_WORDS; i++)
        {
          samples[i] = BKRF_TSSI_GUARD;
        }

      /* The SDK continuous ADC remains enabled between block reads. Its
       * ISR stops delivery after 32 samples; read_raw rearms that delivery.
       * Calling start again instead returns BUSY, even under this lease.
       */

      if (self_tx && result->tx_frames == 0)
        {
          result->tx_submit = bkrf_send_frame();
          if (result->tx_submit != 0)
            {
              ret = -EIO;
              break;
            }

          result->tx_frames++;
        }

      result->adc_error = bk_adc_read_raw(samples, BKRF_TSSI_SAMPLES,
                                          timeout_ms - elapsed);
      if (result->adc_error != 0)
        {
          ret = -EIO;
          break;
        }

      for (i = BKRF_TSSI_SAMPLES;
           i < BKRF_TSSI_SAMPLES + BKRF_GUARD_WORDS; i++)
        {
          if (samples[i] != BKRF_TSSI_GUARD)
            {
              result->guard_ok = 0;
              ret = -EIO;
            }
        }

      if (ret == OK)
        {
          bool tx_block = result->adc_tx_seen == 0 &&
            (getreg32(BKRF_LA_CONTROL) & BKRF_LA_ENABLE) == 0;
          uint32_t estimate = (getreg32(BKRF_RC_TSSI_CALC) >> 8) & 0xffu;

          if (estimate < result->estimate_min)
            {
              result->estimate_min = estimate;
            }

          if (estimate > result->estimate_max)
            {
              result->estimate_max = estimate;
            }

          if (tx_block)
            {
              result->adc_tx_seen = result->adc_samples + BKRF_TSSI_SAMPLES;
              result->adc_tx_min = samples[0];
              result->adc_tx_max = samples[0];
              result->estimate_at_tx = estimate;
              memcpy(result->adc_tx_block, samples,
                     sizeof(result->adc_tx_block));
            }

          /* Keep the latest window until TX confirmation. Raw queuing can
           * precede the actual packet by tens of milliseconds; stopping at
           * the first count samples can miss its entire power envelope.
           */

          if (result->count == count)
            {
              memmove(words, words + BKRF_TSSI_SAMPLES,
                      (count - BKRF_TSSI_SAMPLES) * sizeof(*words));
              result->count -= BKRF_TSSI_SAMPLES;
            }

          for (i = 0; i < BKRF_TSSI_SAMPLES; i++)
            {
              words[result->count++] = samples[i];
              if (result->adc_samples + i == 0 ||
                  samples[i] < result->adc_min)
                {
                  result->adc_min = samples[i];
                }

              if (result->adc_samples + i == 0 ||
                  samples[i] > result->adc_max)
                {
                  result->adc_max = samples[i];
                  result->adc_peak_index = result->adc_samples + i;
                }

              if (tx_block)
                {
                  if (samples[i] < result->adc_tx_min)
                    {
                      result->adc_tx_min = samples[i];
                    }

                  if (samples[i] > result->adc_tx_max)
                    {
                      result->adc_tx_max = samples[i];
                    }
                }
            }

          result->adc_samples += BKRF_TSSI_SAMPLES;
        }
    }

  if (self_tx && result->tx_frames != 0)
    {
      int txret = bkrf_tx_wait(started, timeout_ms);

      result->tx_completed = txret == OK;
      if (ret == OK)
        {
          ret = txret;
        }
    }

  result->elapsed_ms = TICK2MSEC(clock_systime_ticks() - started);
  result->txmap_during = getreg32(BKRF_RC_TX_MAP);
  result->trx_during = getreg32(BKRF_TRX_MEASURE);
  result->filter_during = getreg32(BKRF_TRX_FILTER);
  result->calc_during = getreg32(BKRF_RC_TSSI_CALC);
  if (calc)
    {
      /* Restore only the control bit. Preserve the live estimate and all
       * unrelated fields instead of writing an old status snapshot.
       */

      modifyreg32(BKRF_RC_TSSI_CALC, 1u, result->calc_before & 1u);
      if ((result->calc_during & 1u) == 0)
        {
          ret = -EIO;
        }
    }

  result->calc_after = getreg32(BKRF_RC_TSSI_CALC);
  if ((result->calc_after & 1u) != (result->calc_before & 1u))
    {
      cleanup = -EIO;
    }

  if (la_prepared)
    {
      result->control_completed = getreg32(BKRF_LA_CONTROL);
      if (!bkrf_la_restore(&saved_la))
        {
          cleanup = -EIO;
        }

      up_invalidate_dcache((uintptr_t)g_bkrf_dma,
                          (uintptr_t)(g_bkrf_dma +
                            BK7258_RF_CAPTURE_MAX_WORDS + BKRF_GUARD_WORDS));
      for (i = 0; i < BK7258_RF_CAPTURE_MAX_WORDS; i++)
        {
          if (g_bkrf_dma[i] != BKRF_SENTINEL &&
              (g_bkrf_dma[i] & 0x10000000u) != 0)
            {
              result->tx_trigger_words++;
            }
        }

      for (; i < BK7258_RF_CAPTURE_MAX_WORDS + BKRF_GUARD_WORDS; i++)
        {
          if (g_bkrf_dma[i] != BKRF_SENTINEL)
            {
              result->guard_ok = 0;
              cleanup = -EIO;
            }
        }

      if (self_tx && (result->adc_tx_seen == 0 ||
                     result->tx_trigger_words == 0))
        {
          ret = -ENODATA;
        }
    }

  if (bk_adc_stop() != 0)
    {
      cleanup = -EIO;
    }

  if (tap == BKRF_TRX_IQ_TAP &&
      bk_adc_disable_bypass_clalibration() != 0)
    {
      cleanup = -EIO;
    }

  if (bk_adc_deinit(ADC_8) != 0)
    {
      cleanup = -EIO;
    }

  BK7236_RC_RAM[BKRF_RC_TX_INDEX] = saved_shadow;
  putreg32(result->txmap_before, BKRF_RC_TX_MAP);
  if (tap != 0)
    {
      BK7236_TRX_RAM[0] = saved_trx_shadow;
      putreg32(result->trx_before, BKRF_TRX_MEASURE);
    }

  if (tap == BKRF_TRX_IQ_TAP)
    {
      BK7236_TRX_RAM[BKRF_TRX_FILTER_INDEX] = saved_filter_shadow;
      putreg32(result->filter_before, BKRF_TRX_FILTER);
    }

  UP_DSB();
  if (bk_adc_release() != 0)
    {
      cleanup = -EIO;
    }

  result->restored = cleanup == OK &&
                     getreg32(BKRF_RC_TX_MAP) == result->txmap_before &&
                     BK7236_RC_RAM[BKRF_RC_TX_INDEX] == saved_shadow;
  if (tap != 0)
    {
      result->restored = result->restored &&
        getreg32(BKRF_TRX_MEASURE) == result->trx_before &&
        BK7236_TRX_RAM[0] == saved_trx_shadow;
      if (result->trx_during !=
          ((result->trx_before & ~BKRF_TRX_TAP_MASK) | tap))
        {
          ret = -EIO;
        }
    }

  if (tap == BKRF_TRX_IQ_TAP)
    {
      result->restored = result->restored &&
        getreg32(BKRF_TRX_FILTER) == result->filter_before &&
        BK7236_TRX_RAM[BKRF_TRX_FILTER_INDEX] == saved_filter_shadow;
      if (result->filter_during !=
          (result->filter_before & ~BKRF_TRX_CAPCAL))
        {
          ret = -EIO;
        }
    }

  if (!result->restored || result->txmap_during !=
      (result->txmap_before | BKRF_TSSI_ENABLE))
    {
      ret = -EIO;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_wifi_rf_capture(enum bk7258_rf_capture_source_e source,
                          uint32_t count, uint32_t timeout_ms,
                          bool self_tx,
                          uint32_t *words,
                          struct bk7258_rf_capture_result_s *result)
{
  static const uint32_t diagnostics[] =
  {
    0x00065352u, 0x00055352u, 0x00065554u, 0x00065958u,
    0x00065952u, 0x00065952u, 0x00065952u, 0x00065954u,
    0, 0x00065952u
  };

  struct bkrf_la_s saved_la;
  uint32_t saved_tx_shadow = 0;
  uint32_t saved_rx_shadow = 0;
  uint32_t saved_rc_shadow = 0;
  uint32_t saved_map_shadow = 0;
  uint32_t rx_enable_mask = BKRF_RC_RX_ADC;
  uint32_t trigger;
  bool tssi_source = source == BK7258_RF_CAPTURE_TSSI ||
                     source == BK7258_RF_CAPTURE_TSSI_POWER ||
                     source == BK7258_RF_CAPTURE_TSSI_IQ ||
                     source == BK7258_RF_CAPTURE_TSSI_CALC;
  clock_t started;
  unsigned int i;
  int ret;

  if (words == NULL || result == NULL ||
      (unsigned int)source > BK7258_RF_CAPTURE_TSSI_CALC ||
      (source >= BK7258_RF_CAPTURE_ADC_TX_POWER &&
       !tssi_source && !self_tx) ||
      (tssi_source && count % BKRF_TSSI_SAMPLES != 0) ||
      count == 0 || count > BK7258_RF_CAPTURE_MAX_WORDS ||
      timeout_ms < 10 || timeout_ms > 500)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  result->rx_status = source >= BK7258_RF_CAPTURE_ADC_TX ?
                      -ENODATA : -ENOTSUP;
  result->rx_trigger_mask = source == BK7258_RF_CAPTURE_FE_TX_HOLD ?
                            0x1000u : 0x400u;
  if (g_crm_funcs_t == NULL)
    {
      return -ENODEV;
    }

  ret = nxmutex_trylock(&g_bkrf_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (tssi_source)
    {
      uint32_t tap = source >= BK7258_RF_CAPTURE_TSSI_IQ ? BKRF_TRX_IQ_TAP :
                    source == BK7258_RF_CAPTURE_TSSI_POWER ?
                    BKRF_TRX_POWER_TAP : 0;

      ret = bkrf_tssi_capture(count, timeout_ms, self_tx,
                             tap, source == BK7258_RF_CAPTURE_TSSI_CALC,
                             words, result);
      nxmutex_unlock(&g_bkrf_lock);
      return ret;
    }

  if (source == BK7258_RF_CAPTURE_RX_FORCE)
    {
      result->rc_before = getreg32(BKRF_RC_FORCE);
      if ((result->rc_before & 1u) != 0)
        {
          nxmutex_unlock(&g_bkrf_lock);
          return -EBUSY;
        }
    }

  /* Follow the pinned LA setup, but retain all modified register values.
   * Do not force RF switches, PA power, calibration or persistent storage.
   */

  trigger = source >= BK7258_RF_CAPTURE_ADC_TX ? 0x10000000u :
            source >= BK7258_RF_CAPTURE_FEADC ? 0x10001000u : 0x400u;
  if (source == BK7258_RF_CAPTURE_RX_FORCE)
    {
      /* Force TX holds its ready bit high even before a packet. Also
       * require one DAC data bit, absent from the measured idle word.
       */

      trigger |= 0x00010000u;
    }

  ret = bkrf_la_prepare(count, diagnostics[source], trigger, &saved_la);
  result->control_before = saved_la.control;
  if (ret < 0)
    {
      nxmutex_unlock(&g_bkrf_lock);
      return ret;
    }

  if (source <= BK7258_RF_CAPTURE_ADC80 ||
      source >= BK7258_RF_CAPTURE_ADC_TX)
    {
      g_crm_funcs_t->adc_clock_set(source == BK7258_RF_CAPTURE_ADC80);
    }

  /* RC_REG0x22 is the TX-mode map: pinned calibration start writes 0x88.
   * RX hold takes ADC, RSSI, reference, I-to-V, mixer and LNA enables from
   * the current RX map. Preserve both RF switches, PA and shared LPF route:
   * shorting the TX antenna path into the receiver is not a proven mode.
   * The SDK keeps a packed shadow table, restored alongside the register.
   */

  result->rxmap = getreg32(BKRF_RC_RX_MAP);
  if (source == BK7258_RF_CAPTURE_RX_HOLD ||
      source == BK7258_RF_CAPTURE_FE_TX_HOLD ||
      source == BK7258_RF_CAPTURE_RX_FORCE)
    {
      rx_enable_mask |= result->rxmap & BKRF_RX_ANALOG_MASK;
    }

  if (source >= BK7258_RF_CAPTURE_ADC_TX_POWER)
    {
      result->txmap_before = getreg32(BKRF_RC_TX_MAP);
      saved_tx_shadow = BK7236_RC_RAM[BKRF_RC_TX_INDEX];
      BK7236_RC_RAM[BKRF_RC_TX_INDEX] = saved_tx_shadow | rx_enable_mask;
      putreg32(result->txmap_before | rx_enable_mask, BKRF_RC_TX_MAP);
    }

  /* The SDK's CRM ABI holds RC, active, AHB and DSSS RX clocks in registers
   * separate from ADC frequency selection. Hold them only for this window.
   * Clock availability does not establish RF antenna-side RX during TX.
   */

  if (source >= BK7258_RF_CAPTURE_RX_HOLD)
    {
      result->force0_before = getreg32(BKRF_CRM_FORCE0);
      result->force1_before = getreg32(BKRF_CRM_FORCE1);
      g_crm_funcs_t->rc_force_set(1);
      g_crm_funcs_t->ahb_force_set(1);
      g_crm_funcs_t->active_force_set(15);
      g_crm_funcs_t->rx_force_set(1);
    }

  /* The pinned TX-filter calibration sets RC force_en/RX/TX together and
   * makes RX/TX maps identical. Isolate that request-control difference:
   * use the TX map plus RX analog enables, leaving the TX antenna
   * route, PA setting, LPF route and RF calibration values unchanged.
   * This can establish an internal sampling path, never an antenna echo.
   */

  if (source == BK7258_RF_CAPTURE_RX_FORCE)
    {
      uint32_t txmap = result->txmap_before | rx_enable_mask;
      uint32_t control = (result->rc_before & ~2u) | 13u;

      saved_rx_shadow = BK7236_RC_RAM[BKRF_RC_RX_INDEX];
      saved_rc_shadow = BK7236_RC_RAM[BKRF_RC_FORCE_INDEX];
      saved_map_shadow = BK7236_RC_RAM[BKRF_RC_MAP_INDEX];
      result->map_before = getreg32(BKRF_RC_FORCE_MAP);
      BK7236_RC_RAM[BKRF_RC_RX_INDEX] = txmap;
      putreg32(txmap, BKRF_RC_RX_MAP);
      BK7236_RC_RAM[BKRF_RC_MAP_INDEX] = txmap;
      putreg32(txmap, BKRF_RC_FORCE_MAP);
      BK7236_RC_RAM[BKRF_RC_FORCE_INDEX] = control;
      putreg32(control, BKRF_RC_FORCE);
      UP_DSB();
    }

  started = clock_systime_ticks();
  UP_DSB();
  modifyreg32(BKRF_LA_CONTROL, 0, BKRF_LA_ENABLE);
  ret = OK;
  if (self_tx)
    {
      result->tx_submit = bkrf_send_frame();
      if (result->tx_submit != 0)
        {
          ret = result->tx_submit < 0 ? result->tx_submit : -EIO;
        }
      else
        {
          result->tx_frames = 1;
        }
    }

  while (ret == OK &&
         (getreg32(BKRF_LA_CONTROL) & BKRF_LA_ENABLE) != 0)
    {
      if (TICK2MSEC(clock_systime_ticks() - started) >= timeout_ms)
        {
          ret = -ETIMEDOUT;
          break;
        }

      ret = nxsig_usleep(1000);
      if (ret < 0)
        {
          break;
        }
    }

  if (self_tx && result->tx_frames != 0)
    {
      int txret = bkrf_tx_wait(started, timeout_ms);

      result->tx_completed = txret == OK;
      if (ret == OK)
        {
          ret = txret;
        }
    }

  result->elapsed_ms = TICK2MSEC(clock_systime_ticks() - started);
  result->control_completed = getreg32(BKRF_LA_CONTROL);
  if (source == BK7258_RF_CAPTURE_RX_FORCE)
    {
      result->rc_during = getreg32(BKRF_RC_FORCE);
      result->rxmap_during = getreg32(BKRF_RC_RX_MAP);
      result->map_during = getreg32(BKRF_RC_FORCE_MAP);

      /* Hold shutdown while returning the two maps to independent modes. */

      putreg32((result->rc_before & ~12u) | 3u, BKRF_RC_FORCE);
      UP_DSB();
    }

  if (source >= BK7258_RF_CAPTURE_ADC_TX_POWER)
    {
      result->txmap_during = getreg32(BKRF_RC_TX_MAP);
    }

  if (source >= BK7258_RF_CAPTURE_RX_HOLD)
    {
      result->force0_during = getreg32(BKRF_CRM_FORCE0);
      result->force1_during = getreg32(BKRF_CRM_FORCE1);
    }

  /* Disable the DMA producer before restoring its destination or exposing
   * the static buffer. UART output happens later, with interrupts enabled.
   */

  result->restored = bkrf_la_restore(&saved_la);
  if (source >= BK7258_RF_CAPTURE_ADC_TX_POWER)
    {
      BK7236_RC_RAM[BKRF_RC_TX_INDEX] = saved_tx_shadow;
      putreg32(result->txmap_before, BKRF_RC_TX_MAP);
      UP_DSB();
    }

  if (source == BK7258_RF_CAPTURE_RX_FORCE)
    {
      BK7236_RC_RAM[BKRF_RC_RX_INDEX] = saved_rx_shadow;
      putreg32(result->rxmap, BKRF_RC_RX_MAP);
      BK7236_RC_RAM[BKRF_RC_MAP_INDEX] = saved_map_shadow;
      putreg32(result->map_before, BKRF_RC_FORCE_MAP);
      BK7236_RC_RAM[BKRF_RC_FORCE_INDEX] = saved_rc_shadow;
      putreg32(result->rc_before, BKRF_RC_FORCE);
      UP_DSB();
    }

  if (source >= BK7258_RF_CAPTURE_RX_HOLD)
    {
      putreg32(result->force1_before, BKRF_CRM_FORCE1);
      putreg32(result->force0_before, BKRF_CRM_FORCE0);
      UP_DSB();
    }

  if (source >= BK7258_RF_CAPTURE_ADC_TX_POWER)
    {
      result->restored = result->restored &&
        getreg32(BKRF_RC_TX_MAP) == result->txmap_before &&
        BK7236_RC_RAM[BKRF_RC_TX_INDEX] == saved_tx_shadow;
      if (result->txmap_during != (result->txmap_before | rx_enable_mask))
        {
          ret = -EIO;
        }
    }

  if (source == BK7258_RF_CAPTURE_RX_FORCE)
    {
      result->restored = result->restored &&
        getreg32(BKRF_RC_FORCE) == result->rc_before &&
        getreg32(BKRF_RC_RX_MAP) == result->rxmap &&
        getreg32(BKRF_RC_FORCE_MAP) == result->map_before &&
        BK7236_RC_RAM[BKRF_RC_MAP_INDEX] == saved_map_shadow &&
        BK7236_RC_RAM[BKRF_RC_RX_INDEX] == saved_rx_shadow &&
        BK7236_RC_RAM[BKRF_RC_FORCE_INDEX] == saved_rc_shadow;
      if (result->rc_during != ((result->rc_before & ~2u) | 13u) ||
          result->rxmap_during != (result->txmap_before | rx_enable_mask) ||
          result->map_during != (result->txmap_before | rx_enable_mask))
        {
          ret = -EIO;
        }
    }

  if (source >= BK7258_RF_CAPTURE_RX_HOLD)
    {
      result->restored = result->restored &&
        getreg32(BKRF_CRM_FORCE0) == result->force0_before &&
        getreg32(BKRF_CRM_FORCE1) == result->force1_before;
      if (result->force0_during !=
          (result->force0_before | BKRF_FORCE0_MASK) ||
          result->force1_during !=
          (result->force1_before | BKRF_FORCE1_MASK))
        {
          ret = -EIO;
        }
    }

  up_invalidate_dcache((uintptr_t)g_bkrf_dma,
                       (uintptr_t)(g_bkrf_dma +
                         BK7258_RF_CAPTURE_MAX_WORDS + BKRF_GUARD_WORDS));
  result->guard_ok = 1;
  for (i = count; i < BK7258_RF_CAPTURE_MAX_WORDS + BKRF_GUARD_WORDS; i++)
    {
      if (g_bkrf_dma[i] != BKRF_SENTINEL)
        {
          result->guard_ok = 0;
          break;
        }
    }

  if (!result->restored || !result->guard_ok)
    {
      ret = -EIO;
    }

  if (ret == OK)
    {
      memcpy(words, g_bkrf_dma, count * sizeof(*words));
      result->count = count;
      if (source >= BK7258_RF_CAPTURE_ADC_TX)
        {
          uint32_t previous = 0;
          uint32_t data_mask = result->rx_trigger_mask - 1;
          bool have_previous = false;

          /* Compare the vendor trigger bits and the raw bits below them.
           * RF ADC uses bit 10, FE ADC uses bit 12. These are diagnostic
           * predicates, not a verified IQ decoder or external echo proof.
           */

          for (i = 0; i < count; i++)
            {
              if ((words[i] & 0x10000000u) == 0)
                {
                  have_previous = false;
                  continue;
                }

              result->tx_trigger_words++;
              if ((words[i] & result->rx_trigger_mask) == 0)
                {
                  have_previous = false;
                  continue;
                }

              result->rx_trigger_words++;
              if (have_previous && previous != (words[i] & data_mask))
                {
                  result->rx_changes++;
                }

              previous = words[i] & data_mask;
              have_previous = true;
            }

          if (result->rx_trigger_words >= 2 && result->rx_changes != 0)
            {
              result->rx_status = OK;
            }
        }
    }

  nxmutex_unlock(&g_bkrf_lock);
  return ret;
}
