/****************************************************************************
 * chips/bk7258/include/bk7258_sdmadc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SDMADC — NuttX adc_dev_s lower-half wrapper.
 *
 * Wraps the official Beken bk_sdmadc_* SDK API (AP role, polling
 * single-shot mode) as a NuttX ADC lower half.  The BK7258 SDMADC is a
 * 16-bit sigma-delta converter with multiple channels; the SDK's
 * bk_sdmadc_single_read() performs a full single-shot conversion of the
 * requested channel and returns the averaged int16_t sample.
 *
 * AP/CP distribution: the 18 bk_sdmadc_* symbols are exported by both the
 * AP and the CP libdriver.a (verified with `nm`).  This wrapper runs on
 * the AP core, consistent with the SARADC/I2C/SPI/SDIO/RTC/TIMER/GPIOE
 * wrappers.
 *
 * SDK semantics:
 *   - bk_sdmadc_driver_init() must run first (allocates the sample buffer
 *     and registers the ISR); bk_sdmadc_init() then enables the sampler
 *     and returns BK_FAIL if the driver was not initialised.
 *   - bk_sdmadc_single_read(&val, chan) internally configures the channel,
 *     starts a conversion, waits, and averages the samples into an int16_t.
 *   - bk_sdmadc_deinit() only disables sampling.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SDMADC_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SDMADC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default SDMADC channel (SDMADC_1..SDMADC_13; no SDMADC_0). */

#ifndef CONFIG_BK7258_SDMADC_CHAN
#  define CONFIG_BK7258_SDMADC_CHAN     1
#endif

/* Default /dev/adcN device name. */

#ifndef CONFIG_BK7258_SDMADC_DEVNAME
#  define CONFIG_BK7258_SDMADC_DEVNAME  "/dev/adc1"
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_SDMADC
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_sdmadc_initialize
 *
 * Description:
 *   Construct a NuttX ADC lower-half for the BK7258 SDMADC channel
 *   CONFIG_BK7258_SDMADC_CHAN and register it at /dev/adcN
 *   (N = CONFIG_BK7258_SDMADC_BUS).  No hardware is touched until the
 *   upper half opens the device (ao_setup); this only constructs the
 *   lower-half and publishes the node.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.  Calling it twice is
 *   harmless and returns OK.
 *
 ****************************************************************************/

int bk7258_sdmadc_initialize(void);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_SDMADC */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SDMADC_H */
