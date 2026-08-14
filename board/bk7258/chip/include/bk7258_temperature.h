/****************************************************************************
 * board/bk7258/chip/include/bk7258_temperature.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CP-owned on-die temperature sensor interface.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_TEMPERATURE_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_TEMPERATURE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_TEMPERATURE_FLAG_RAW_VALID       (1u << 0)
#define BK7258_TEMPERATURE_FLAG_CALIBRATED      (1u << 1)

/* The immutable v3.1.1.9 CP bundle defines CONFIG_SOC_BK7236XX for BK7258
 * and uses the non-SDMADC branch of temp_detect.h.  Its one-shot API accepts
 * raw values strictly inside (10, 1365), and the transfer slope is 46 ADC
 * codes per 10 degrees C.  The raw value decreases as temperature rises.
 */

#define BK7258_TEMPERATURE_RAW_MIN              11u
#define BK7258_TEMPERATURE_RAW_MAX              1364u
#define BK7258_TEMPERATURE_LSB_PER_10C          46u

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bk7258_temperature_sample_s
{
  uint32_t generation;
  uint32_t sequence;
  uint32_t raw_code;
  uint32_t reference_raw;
  int32_t temperature_millicelsius;
  uint32_t flags;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Register the role-specific RPMsg endpoint.  On AP, a thermal zone is also
 * registered once a valid per-device 25 C reference is available.
 */

int bk7258_temperature_initialize(void);

#ifdef CONFIG_BK7258_AP_CORE
/* Perform one bounded CP measurement.  raw_code is always authoritative when
 * RAW_VALID is set.  temperature_millicelsius must only be consumed when
 * CALIBRATED is set.
 */

int bk7258_temperature_read(
  struct bk7258_temperature_sample_s *sample);

/* Supply this device's factory raw ADC code measured at 25 C.  The value is
 * per-chip calibration data, not a board-wide nominal constant.
 */

int bk7258_temperature_set_reference_raw(uint32_t reference_raw);

#ifdef CONFIG_BK7258_TEMPERATURE_VALIDATION
int bk7258_temperature_validation_start(void);
#endif
#else
/* Coordinated standby must not enter while the CP LPWORK sampler owns the
 * SDK SARADC path.
 */

bool bk7258_temperature_server_idle(void);
#endif

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_TEMPERATURE_H */
