/****************************************************************************
 * chips/bk7258/include/bk7258_usbcdc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 USB device CDC-ACM serial gadget.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBCDC_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBCDC_H

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

#if defined(CONFIG_BK7258_USBCDC) && defined(CONFIG_BK7258_AP_CORE)

/* Register the CherryUSB device controller and /dev/ttyGS0 CDC-ACM serial
 * gadget.  The MUSB controller is owned exclusively by either the host or
 * the device wrapper; enabling both is rejected at build time.
 */

int bk7258_usbcdc_initialize(void);
int bk7258_usbcdc_uninitialize(void);

#endif /* CONFIG_BK7258_USBCDC && CONFIG_BK7258_AP_CORE */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBCDC_H */
