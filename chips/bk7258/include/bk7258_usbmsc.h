/****************************************************************************
 * chips/bk7258/include/bk7258_usbmsc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CherryUSB MSC transport backed by a NuttX block device.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBMSC_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBMSC_H

#include <nuttx/config.h>

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(CONFIG_BK7258_USBMSC) && defined(CONFIG_BK7258_AP_CORE)

int bk7258_usbmsc_initialize(const char *blockdev);
int bk7258_usbmsc_uninitialize(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBMSC_H */
