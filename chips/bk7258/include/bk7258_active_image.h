/****************************************************************************
 * chips/bk7258/include/bk7258_active_image.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_ACTIVE_IMAGE_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_ACTIVE_IMAGE_H

#include <nuttx/config.h>

#include <arch/chip/bk7258_mcuboot_format.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CONFIG_BK7258_OTA_MANAGER
/* Read the immutable MCUboot header at the AP's active mapped XIP address.
 * BL2 has already admitted the running CP/AP pair before CP starts AP.  This
 * accessor performs no Flash transaction, lock or cross-core request.
 */

int bk7258_active_ap_image_version(
  struct bk7258_mcuboot_version_s *version);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_ACTIVE_IMAGE_H */
