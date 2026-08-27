/****************************************************************************
 * tests/bk7258/mocks/arch/chip/bk7258_can.h
 *
 * Host mock of chips/bk7258/include/bk7258_can.h: the public API the
 * board bring-up code would call.  The lower-half only consumes
 * bk7258_can_initialize()/uninitialize().
 ****************************************************************************/

#ifndef __MOCK_ARCH_CHIP_BK7258_CAN_H
#define __MOCK_ARCH_CHIP_BK7258_CAN_H

#include <nuttx/can/can.h>

struct bk7258_can_priv_s;

int bk7258_can_initialize(FAR struct can_dev_s **dev);
int bk7258_can_uninitialize(FAR struct can_dev_s *dev);

#endif /* __MOCK_ARCH_CHIP_BK7258_CAN_H */
