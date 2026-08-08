/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/include/
 * bk7258_aud.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) Audio front-end (AUD) — NuttX audio_lowerhalf_s wrapper.
 *
 * Wraps the official Beken bk_aud_* SDK API (DAC playback + ADC/DMIC
 * capture) as a NuttX audio lower half.  Both T5-AI Core and T5-AI Board
 * expose the audio pins, so this is the primary analog audio path
 * (microphone in, speaker out).  I2S (bk7258_i2s) is the digital-codec
 * companion that is delivered separately.
 *
 * AP role: the 54 bk_aud_* symbols live exclusively in the AP libdriver.a
 * (verified with `nm`; CP exports zero).  So this driver is AP-only, like
 * I2S and the other wrappers.
 *
 * Architecture (modelled on drivers/audio/audio_null.c):
 *   - The full audio_ops_s vtable is implemented.  allocbuffer/freebuffer
 *     are left NULL so the upper half uses its default buffer pool.
 *   - enqueuebuffer() posts AUDIO_MSG_ENQUEUE on a per-device message
 *     queue; a worker thread drains it and performs the actual PCM
 *     transfer to/from the AUD hardware FIFO, then reports completion via
 *     dev->upper(AUDIO_CALLBACK_COMPLETE).
 *   - DAC output: bk_aud_dac_get_fifo_addr() returns the TX FIFO register
 *     address; the worker writes 32-bit samples there.
 *   - ADC capture: bk_aud_adc_get_fifo_data() reads one sample per call;
 *     the worker polls it.
 *
 * SDK semantics (verified in aud_*_driver.c):
 *   - bk_aud_driver_init() then bk_aud_dac_init()/bk_aud_adc_init() are
 *     the standard bring-up; each requires the driver already init'd.
 *   - bk_aud_dac_start()/adc_start() enable the block; stop() disables it.
 *   - bk_aud_dac_set_samp_rate()/bk_aud_adc_set_samp_rate() select the
 *     sample rate; bk_aud_dac_set_chl()/bk_aud_adc_set_chl() the channel.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AUD_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AUD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default /dev/audioN device name. */

#ifndef CONFIG_BK7258_AUD_DEVNAME
#  define CONFIG_BK7258_AUD_DEVNAME    "audio0"
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_AUD
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_aud_initialize
 *
 * Description:
 *   Construct the BK7258 audio lower half (DAC + ADC) and register it at
 *   /dev/audioN via audio_register().  The hardware is brought up lazily
 *   by the first start(); this only constructs the driver and publishes
 *   the node.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.  Calling it twice is
 *   harmless and returns OK.
 *
 ****************************************************************************/

int bk7258_aud_initialize(void);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_AUD */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AUD_H */
