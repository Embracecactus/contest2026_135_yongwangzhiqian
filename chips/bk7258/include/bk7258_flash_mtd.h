/****************************************************************************
 * chips/bk7258/include/bk7258_flash_mtd.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 raw-Flash MTD lower-half contract.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_FLASH_MTD_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_FLASH_MTD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <nuttx/mtd/mtd.h>

#include <stdint.h>

#include <arch/chip/bk7258_storage_guard.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bk7258_flash_mtd_config_s
{
  uint32_t base;
  uint32_t size;
  enum bk7258_storage_guard_e owner;
  FAR const char *name;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_flash_mtd_initialize
 *
 * Description:
 *   Create (or return the singleton) MTD lower half for one caller-selected
 *   raw-Flash range.  The caller owns partition geometry and mutation policy;
 *   this chip service owns bounds checking, storage locking and controller
 *   access.  Initialization performs no erase or write.
 *
 * Returned Value:
 *   Pointer to the mtd_dev_s instance, or NULL if the flash id did not match
 *   the expected 8 MiB part.
 *
 ****************************************************************************/

#ifdef CONFIG_BK7258_FLASH_MTD
FAR struct mtd_dev_s *
bk7258_flash_mtd_initialize(
  FAR const struct bk7258_flash_mtd_config_s *config);

#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_FLASH_MTD_H */
