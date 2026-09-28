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
#include <stdint.h>

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

/* Serialize local filesystem users with USB MSC export of the backing block
 * device.  Every successful acquire must have one matching release.
 */

int bk7258_usbmode_blockdev_acquire(void);
int bk7258_usbmode_blockdev_release(void);
/* Changes only after host-writable MSC ownership has been returned to the
 * local device successfully.  Local caches of block-device content compare
 * this value before reuse; wraparound is intentionally modulo uint32_t.
 */
uint32_t bk7258_usbmode_media_generation(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBMODE_H */
