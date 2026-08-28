/****************************************************************************
 * chips/bk7258/include/bk7258_irda.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 IrDA (NEC infrared receiver) character-device contract.
 *
 * The v3.1.1.9 CP SDK owns the NEC decode path (middleware/driver/irda,
 * exported in cp/libs/libdriver.a); this board wrapper publishes it to user
 * space as /dev/irda0 and exposes the same IRDA_CMD_* controls.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_IRDA_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_IRDA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/fs/ioctl.h>

#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_IRDA_DEVPATH       "/dev/irda0"

/* Key type values returned by /dev/irda0 read().  Each read returns one
 * uint32_t encoded as GENERATE_KEY(type, value): byte0 = IR key code,
 * byte1 = inverted key code, byte3 = key type.
 */

#define BK7258_IRDA_KEY_SHORT     0
#define BK7258_IRDA_KEY_LONG      1
#define BK7258_IRDA_KEY_HOLD      2

/* Board ioctl commands (board-local base 0x5d00). */

#define BKIOC_IRDA_ACTIVE         _IOC(0x5d00, 0x01)  /* arg: bool enable NEC */
#define BKIOC_IRDA_SET_POLARITY   _IOC(0x5d00, 0x02)  /* arg: uint8_t polarity */
#define BKIOC_IRDA_SET_CLK        _IOC(0x5d00, 0x03)  /* arg: uint16_t clock div */
#define BKIOC_IRDA_SET_INT_MASK   _IOC(0x5d00, 0x04)  /* arg: uint8_t mask */
#define BKIOC_IRDA_SET_USERCODE   _IOC(0x5d00, 0x05)  /* arg: uint16_t usercode */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_BK7258_IRDA) && !defined(CONFIG_BK7258_AP_CORE)

/* Register /dev/irda0.  CP owns the IR receiver hardware and NEC decoder. */

int bk7258_irda_initialize(void);

#endif /* CONFIG_BK7258_IRDA && !CONFIG_BK7258_AP_CORE */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_IRDA_H */
