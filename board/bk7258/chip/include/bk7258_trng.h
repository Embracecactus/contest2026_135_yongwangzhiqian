/****************************************************************************
 * board/bk7258/chip/include/bk7258_trng.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_TRNG_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_TRNG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Initialize the SDK TRNG singleton used by the NuttX random devices.
 *
 * The SDK driver is a global resource and its init API is idempotent.  The
 * lower half deliberately has no deinitialize entry point: the SDK startup
 * path and other AP clients may share this singleton, while each random
 * device read starts and stops the hardware through bk_fill_rand().
 */

int bk7258_trng_initialize(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_TRNG_H */
