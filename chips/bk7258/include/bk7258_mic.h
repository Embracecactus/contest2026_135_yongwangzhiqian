/****************************************************************************
 * chips/bk7258/include/bk7258_mic.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 on-board analog microphone capture.  The shared lower half consumes
 * the selected physical board's fixed one- or two-channel topology; runtime
 * applications may request any channel count supported by that topology.
 *
 * The AUD ADC block is an AP-role peripheral: bk_aud_adc_* / bk_i2s_* /
 * ring_buffer_* are only defined in the AP libdriver.a.  The CP archive
 * exports the headers but zero symbols, so this driver is AP-only.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_MIC_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_MIC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Sample rates natively supported by bk_aud_adc_set_samp_rate().  8k/16k/
 * 44.1k/48k map onto the hardware samp_rate_adc divider directly; the other
 * five rates go through the fractional divider.  Anything else returns
 * BK_FAIL from the SDK.
 */

#define BK7258_MIC_RATE_8000            8000u
#define BK7258_MIC_RATE_11025           11025u
#define BK7258_MIC_RATE_12000           12000u
#define BK7258_MIC_RATE_16000           16000u
#define BK7258_MIC_RATE_22050           22050u
#define BK7258_MIC_RATE_24000           24000u
#define BK7258_MIC_RATE_32000           32000u
#define BK7258_MIC_RATE_44100           44100u
#define BK7258_MIC_RATE_48000           48000u

/* The AUD ADC FIFO is fixed at 16-bit samples: register REG_0x11 documents
 * [15:0] as the left channel and [31:16] as the right channel.
 */

#define BK7258_MIC_BITS_PER_SAMPLE      16u
#define BK7258_MIC_BYTES_PER_SAMPLE     2u

/* Digital gain: adc_config0->adc_set_gain is a 6-bit field spanning
 * -45 dB..+18 dB in 1 dB steps, with 0x2d == 0 dB.
 */

#define BK7258_MIC_DIG_GAIN_MIN         0x00u
#define BK7258_MIC_DIG_GAIN_MAX         0x3fu
#define BK7258_MIC_DIG_GAIN_0DB         0x2du

/* Analog gain: SYS_ANA_REG19_MICGAIN and SYS_ANA_REG27_MICGAIN are 4-bit
 * fields for MIC1 and MIC2 respectively.  The Beken onboard_mic_stream
 * sample validates against 0x3f, which is a copy/paste of the digital-gain
 * range; the SDK masks with 0xf, so larger values are silently truncated.
 * Clamp against the real hardware limit instead.
 */

#define BK7258_MIC_ANA_GAIN_MIN         0x00u
#define BK7258_MIC_ANA_GAIN_MAX         0x0fu

/* Physical microphone topology supplied explicitly by board bring-up. */

#define BK7258_MIC_INPUT_MIC1            (1u << 0)
#define BK7258_MIC_INPUT_MIC2            (1u << 1)

struct bk7258_mic_config_s
{
  FAR const char *variant_name;
  uint32_t flags;
  uint8_t channels;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_MIC
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_mic_initialize
 *
 * Description:
 *   Register the selected board's on-board analog microphone topology as a
 *   NuttX audio lower-half at /dev/audio/<CONFIG_BK7258_MIC_DEVNAME>.  No
 *   hardware is touched until the upper half calls configure()/start().
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.  Calling it twice is
 *   harmless and returns OK.
 *
 ****************************************************************************/

int bk7258_mic_initialize(
  FAR const struct bk7258_mic_config_s *config);

#ifdef CONFIG_BK7258_MIC_LIFECYCLE_VALIDATION
int bk7258_mic_validation_start(
  FAR const struct bk7258_mic_config_s *config);
#endif

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_MIC */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_MIC_H */
