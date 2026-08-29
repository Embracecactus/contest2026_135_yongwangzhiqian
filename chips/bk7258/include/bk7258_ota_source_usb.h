/****************************************************************************
 * chips/bk7258/include/bk7258_ota_source_usb.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_USB_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_USB_H

#include <nuttx/config.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CONFIG_BK7258_OTA_SOURCE_USB

/* Start the native-USB OTA source.  It registers the CDC-ACM transport with
 * the chip USB device lower half and pins its worker to the AP primary CPU.
 * The board supplies no binding: the descriptors and endpoints are chip-owned
 * because the transport is identical on every board that wires USB0.
 */

int bk7258_ota_source_usb_initialize(void);

#endif /* CONFIG_BK7258_OTA_SOURCE_USB */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_USB_H */
