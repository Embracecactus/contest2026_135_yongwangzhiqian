/****************************************************************************
 * board/bk7258/chip/include/bk7258_usbserial_ch34x.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_USBSERIAL_CH34X_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_USBSERIAL_CH34X_H

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Register the standard NuttX USB-host class for the single supported
 * Winchiphead CH340/CH341 identity (VID 0x1a86, PID 0x7523).  The class
 * registers /dev/ttyUSB0 when that device is enumerated.  Calling this
 * function more than once is harmless.
 *
 * The lower half intentionally exposes only the protocol settings that have
 * been implemented and checked: 115200 baud, 8 data bits, no parity, one
 * stop bit, and no hardware/software flow control.  Other termios settings
 * are rejected with -ENOTSUP rather than silently changing the device.
 */

int bk7258_usbserial_ch34x_initialize(void);

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_USBSERIAL_CH34X_H */
