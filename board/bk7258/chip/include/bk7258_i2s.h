/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_i2s.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) I2S — NuttX i2s_dev_s lower-half wrapper.
 *
 * Wraps the official Beken bk_i2s_* SDK API as a NuttX I2S lower half.
 * The BK7258 I2S block drives I2S/PCM audio codecs; three GPIO groups are
 * available (GROUP_0/1/2, four pins each).
 *
 * AP role: the 33 bk_i2s_* symbols live exclusively in the AP libdriver.a
 * (verified with `nm`; CP exports zero).  So this driver is AP-only, like
 * the I2C/SPI/SDIO wrappers.
 *
 * SDK semantics:
 *   - bk_i2s_driver_init() then bk_i2s_init(group, &config) is the SDK
 *     standard bring-up; init configures the selected GPIO group itself.
 *   - bk_i2s_enable(I2S_ENABLE) arms the block for transfers.
 *   - bk_i2s_write_data()/read_data() are raw register pushes/pulls with
 *     no FIFO wait, so a reliable transfer must poll bk_i2s_get_write_ready()
 *     / bk_i2s_get_read_ready() first.  This wrapper implements the NuttX
 *     i2s_send()/i2s_receive() as synchronous polled loops and invokes the
 *     completion callback in the caller's context.
 *   - i2s_samp_rate_t is an enum (8000..88200), mapped from the NuttX Hz
 *     rate by nearest match.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_I2S_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_I2S_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default I2S GPIO group and device name. */

#ifndef CONFIG_BK7258_I2S_GPIO_GROUP
#  define CONFIG_BK7258_I2S_GPIO_GROUP  0
#endif

#ifndef CONFIG_BK7258_I2S_DEVNAME
#  define CONFIG_BK7258_I2S_DEVNAME     "i2s0"
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_I2S
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_i2s_initialize
 *
 * Description:
 *   Construct a NuttX I2S lower-half for the BK7258 I2S block (GPIO group
 *   CONFIG_BK7258_I2S_GPIO_GROUP) and return it to the caller for use
 *   with the audio stack (e.g. the audio0 upper half).  No hardware is
 *   touched until an i2s_* method is called; the block is brought up
 *   lazily on the first send/receive.
 *
 * Returned Value:
 *   A pointer to the NuttX I2S interface, or NULL on failure.
 *
 ****************************************************************************/

FAR struct i2s_dev_s *bk7258_i2s_initialize(void);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_I2S */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_I2S_H */
