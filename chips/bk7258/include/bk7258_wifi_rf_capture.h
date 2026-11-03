/****************************************************************************
 * chips/bk7258/include/bk7258_wifi_rf_capture.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_RF_CAPTURE_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_RF_CAPTURE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_RF_CAPTURE_MAX_WORDS 1024u

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_rf_capture_source_e
{
  BK7258_RF_CAPTURE_ADC40 = 0,
  BK7258_RF_CAPTURE_ADC80 = 1,
  BK7258_RF_CAPTURE_FEADC = 2,
  BK7258_RF_CAPTURE_FEDAC = 3,
  BK7258_RF_CAPTURE_ADC_TX = 4,
  BK7258_RF_CAPTURE_ADC_TX_POWER = 5,
  BK7258_RF_CAPTURE_RX_HOLD = 6,
  BK7258_RF_CAPTURE_FE_TX_HOLD = 7,
  BK7258_RF_CAPTURE_TSSI = 8,
  BK7258_RF_CAPTURE_RX_FORCE = 9,
  BK7258_RF_CAPTURE_TSSI_POWER = 10,
  BK7258_RF_CAPTURE_TSSI_IQ = 11,
  BK7258_RF_CAPTURE_TSSI_CALC = 12
};

struct bk7258_rf_capture_result_s
{
  uint32_t count;
  uint32_t elapsed_ms;
  uint32_t control_before;
  uint32_t control_completed;
  uint32_t restored;
  uint32_t guard_ok;
  int32_t tx_submit;
  uint32_t txmap_before;
  uint32_t txmap_during;
  uint32_t force0_before;
  uint32_t force0_during;
  uint32_t force1_before;
  uint32_t force1_during;
  uint32_t rxmap;
  uint32_t tx_trigger_words;
  uint32_t rx_trigger_words;
  uint32_t rx_trigger_mask;
  uint32_t rx_changes;
  int32_t rx_status; /* Trigger-qualified variation, not external echo proof. */
  int32_t adc_error;
  uint32_t adc_samples;
  uint32_t adc_min;
  uint32_t adc_max;
  uint32_t adc_peak_index;
  uint32_t adc_tx_seen;
  uint32_t adc_tx_min;
  uint32_t adc_tx_max;
  uint16_t adc_tx_block[32];
  uint32_t calc_before;
  uint32_t calc_during;
  uint32_t calc_after;
  uint32_t estimate_min;
  uint32_t estimate_max;
  uint32_t estimate_at_tx;
  uint32_t trx_before;
  uint32_t trx_during;
  uint32_t filter_before;
  uint32_t filter_during;
  uint32_t tx_frames;
  uint32_t tx_completed;
  uint32_t rc_before;
  uint32_t rc_during;
  uint32_t rxmap_during;
  uint32_t map_before;
  uint32_t map_during;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bk7258_wifi_rf_capture(enum bk7258_rf_capture_source_e source,
                          uint32_t count, uint32_t timeout_ms,
                          bool self_tx,
                          uint32_t *words,
                          struct bk7258_rf_capture_result_s *result);

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_RF_CAPTURE_H */
