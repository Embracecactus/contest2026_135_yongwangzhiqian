/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/include/
 * bk7258_saradc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) SARADC — NuttX adc_dev_s lower-half wrapper.
 *
 * Wraps the official Beken bk_adc_* SDK API (AP role, polling single-shot
 * mode) as a NuttX ADC lower half.  The BK7258 SARADC is a 12-bit
 * successive-approximation converter with 16 channels (ADC_0..ADC_15).
 *
 * AP/CP distribution: the bk_adc_* / bk_saradc_* symbols live exclusively in
 * the AP libdriver.a (18 symbols verified with nm); the CP archive ships
 * the headers but zero symbols.  So this driver is AP-only, like I2C/SPI.
 *
 * IMPORTANT SDK SEMANTICS:
 *   - Every bk_adc_* call internally re-runs bk_saradc_driver_init() and
 *     talks to the SARADC service via mailbox IPC (mb_ipc_send/recv).  The
 *     SARADC service must therefore be up on the partner core (the SDK
 *     owns that bring-up); this wrapper does not start it.
 *   - bk_adc_single_read() performs a single-shot conversion of the
 *     currently selected channel and returns the raw 16-bit sample.
 *   - bk_adc_init(chan) selects the channel; bk_adc_set_channel(chan)
 *     switches it later without re-init.
 *   - Calibration is applied by the SDK; bk_adc_single_read() already
 *     returns the calibrated value.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SARADC_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SARADC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default SARADC channel (ADC_0..ADC_15). */

#ifndef CONFIG_BK7258_SARADC_CHAN
#  define CONFIG_BK7258_SARADC_CHAN      0
#endif

/* Default /dev/adcN device name. */

#ifndef CONFIG_BK7258_SARADC_DEVNAME
#  define CONFIG_BK7258_SARADC_DEVNAME   "adc0"
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_SARADC
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_saradc_initialize
 *
 * Description:
 *   Construct a NuttX ADC lower-half for the BK7258 SARADC channel
 *   CONFIG_BK7258_SARADC_CHAN and register it at /dev/adcN
 *   (N = CONFIG_BK7258_SARADC_BUS).  No hardware is touched until the upper
 *   half opens the device (ao_setup); this only constructs the lower-half
 *   and publishes the node.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.  Calling it twice is
 *   harmless and returns OK.
 *
 ****************************************************************************/

int bk7258_saradc_initialize(void);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_SARADC */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SARADC_H */
