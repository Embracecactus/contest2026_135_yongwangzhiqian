/****************************************************************************
 * chips/bk7258/include/bk7258_usbmode.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 USB0 dynamic device-mode manager.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBMODE_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBMODE_H

#include <nuttx/config.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum bk7258_usbmode_e
{
  BK7258_USBMODE_NONE = 0,
  BK7258_USBMODE_CDC,
  BK7258_USBMODE_MSC,
};

#if defined(CONFIG_BK7258_USBMODE) && defined(CONFIG_BK7258_AP_CORE)

int bk7258_usbmode_initialize(void);
int bk7258_usbmode_set(enum bk7258_usbmode_e mode);
enum bk7258_usbmode_e bk7258_usbmode_get(void);
const char *bk7258_usbmode_name(enum bk7258_usbmode_e mode);

#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBMODE_H */
