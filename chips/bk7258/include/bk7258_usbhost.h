/****************************************************************************
 * chips/bk7258/include/bk7258_usbhost.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBHOST_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBHOST_H

#include <nuttx/compiler.h>
#include <nuttx/usb/usbhost.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Return the AP USB host connection object.  The caller owns the NuttX
 * waiter/enumeration policy and registers the enabled NuttX USB host classes.
 * The BK7258 SDK CherryUSB stack is deliberately not used as an upper layer;
 * the implementation only adapts its immutable HCD/pipe/URB ABI.  Board
 * linking wraps usbh_initialize(), usbh_deinitialize(), and the root-hub
 * event queue so bk_usb_open()/bk_usb_close() perform the official SDK
 * power/PHY sequence while CherryUSB upper-layer entry points are redirected
 * to this adapter's HCD-only lifecycle and ISR-deferred event path.
 *
 * This controller has one physical root port.  No board VBUS or external hub
 * policy is implied by this API; that policy belongs to board integration.
 */

FAR struct usbhost_connection_s *bk7258_usbhost_initialize(void);
int bk7258_usbhost_uninitialize(void);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBHOST_H */
