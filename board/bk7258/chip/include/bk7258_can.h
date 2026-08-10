/****************************************************************************
 * board/bk7258/chip/include/bk7258_can.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_CAN_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_CAN_H

#include <nuttx/can/can.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Return the singleton AP CAN lower-half.  Board integration owns the
 * can_register() call and the device path.  A second owner is rejected with
 * -EBUSY; bk7258_can_uninitialize() must be called only after all NuttX CAN
 * file references have been closed.
 *
 * The v3.1.1.9 BK7258 bundle supplies one classic CAN controller.  This
 * adapter intentionally exposes standard 11-bit CAN 2.0 data frames only:
 * CAN-FD, extended identifiers, and remote frames are rejected because the
 * SDK software FIFO does not preserve enough per-frame metadata for those
 * NuttX contracts.  Loopback is controlled through the standard
 * CANIOC_{GET,SET}_CONNMODES ioctl and maps to the SDK's internal LBMI bit.
 *
 * The SDK fixes CAN0 to GPIO44 (TX), GPIO45 (RX), and GPIO46 (standby).
 * This private API does not alter pin mux or arbitrate those pins with the
 * T5 board RGB LCD; board integration must select a non-conflicting owner.
 */

int bk7258_can_initialize(FAR struct can_dev_s **dev);
int bk7258_can_uninitialize(FAR struct can_dev_s *dev);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_CAN_H */
