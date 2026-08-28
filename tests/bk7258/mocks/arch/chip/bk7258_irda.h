/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/bk7258/mocks/arch/chip/bk7258_irda.h
 *
 * Host stand-in for the board arch/chip/bk7258_irda.h contract used by
 * bk7258_irda.c: device path, key-type values and the BKIOC_IRDA_* ioctl
 * commands.
 ****************************************************************************/

#ifndef __MOCK_ARCH_CHIP_BK7258_IRDA_H
#define __MOCK_ARCH_CHIP_BK7258_IRDA_H

#include <stdint.h>

#include "nuttx/fs/ioctl.h"

#define BK7258_IRDA_DEVPATH       "/dev/irda0"

#define BK7258_IRDA_KEY_SHORT     0
#define BK7258_IRDA_KEY_LONG      1
#define BK7258_IRDA_KEY_HOLD      2

#define BKIOC_IRDA_ACTIVE         _IOC(0x5d00, 0x01)
#define BKIOC_IRDA_SET_POLARITY   _IOC(0x5d00, 0x02)
#define BKIOC_IRDA_SET_CLK        _IOC(0x5d00, 0x03)
#define BKIOC_IRDA_SET_INT_MASK   _IOC(0x5d00, 0x04)
#define BKIOC_IRDA_SET_USERCODE   _IOC(0x5d00, 0x05)

int bk7258_irda_initialize(void);

#endif /* __MOCK_ARCH_CHIP_BK7258_IRDA_H */
