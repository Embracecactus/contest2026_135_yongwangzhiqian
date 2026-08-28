/****************************************************************************
 * chips/bk7258/include/bk7258_i2c.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 I2C master — NuttX i2c_master_s lower-half wrapping the
 * official Beken bk_i2c_* SDK API.
 *
 * The I2C block is an AP-role peripheral: bk_i2c_* are only defined in the
 * AP libdriver.a (21 symbols).  The CP archive exports the headers but zero
 * symbols, so this driver is AP-only, exactly like the MIC driver.  Verified
 * with `nm ap/libs/libdriver.a` (21 T bk_i2c_*) vs `nm cp/libs/libdriver.a`
 * (0).
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_I2C_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_I2C_H

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

/* Hardware I2C unit used.  BK7258 exposes I2C_ID_0 and I2C_ID_1.  Keep the
 * hardware instance and NuttX /dev/i2cN number aligned so board/product
 * profiles do not need a second alias table.
 */

#define BK7258_I2C_UNIT                CONFIG_BK7258_I2C_BUS

/* Default bus speed.  The NuttX I2C_SPEED_* macros and the Beken
 * I2C_BAUD_RATE_* macros use the same numeric kHz values, so the per-transfer
 * i2c_msg_s::frequency maps directly onto bk_i2c_set_baud_rate().
 */

#define BK7258_I2C_BAUD_RATE_DEFAULT   100000u  /* I2C_SPEED_STANDARD */

/* Default per-call blocking timeout handed to the Beken SDK (ms). */

#define BK7258_I2C_TIMEOUT_MS_DEFAULT  1000u

/* Maximum number of i2c_msg_s segments handled in one transfer().  Kept
 * small: the SDK has no multi-message batched API, so each segment is a
 * separate SDK call.
 */

#define BK7258_I2C_MAX_MSG             8

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_I2C
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_i2c_initialize
 *
 * Description:
 *   Probe/register the BK7258 I2C master as a NuttX I2C character device at
 *   /dev/i2cN (N = CONFIG_BK7258_I2C_BUS).  No hardware is touched until the
 *   upper half calls setup(); this only constructs the lower-half and
 *   publishes the node.
 *
 *   The underlying Beken I2C driver (bk_i2c_driver_init) is reference counted
 *   so multiple buses could share it; this configured instance owns exactly
 *   one unit.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.  Calling it twice is
 *   harmless and returns OK.
 *
 ****************************************************************************/

int bk7258_i2c_initialize(void);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_I2C */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_I2C_H */
