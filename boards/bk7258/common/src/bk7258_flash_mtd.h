/****************************************************************************
 * contest2026_135_yongwangzhiqian/boards/bk7258/common/src/bk7258_flash_mtd.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 product-partition MTD composition.
 *
 * The data MTD covers LittleFS.  A NuttX MCUboot BL2 build can additionally
 * create two read-only MTD partitions for the physical A/B image pairs.
 *
 * read/erase/bwrite use the board-verified flash-controller sequence with an
 * option-A SR0 block-protect policy (cleared around each op, restored
 * afterwards, so the boot/app region keeps its hardware protection outside
 * the op window).
 ****************************************************************************/

#ifndef __BOARDS_ARM_BK7258_SRC_BK7258_FLASH_MTD_H
#define __BOARDS_ARM_BK7258_SRC_BK7258_FLASH_MTD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <nuttx/mtd/mtd.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_flash_mtd_initialize
 *
 * Description:
 *   Create (or return the singleton) MTD device instance for the 1 MiB data
 *   partition.  Reads the JEDEC id once to confirm the 8 MiB NOR.  Performs
 *   no erase, no write, and no status-register change at init time.
 *
 * Returned Value:
 *   Pointer to the mtd_dev_s instance, or NULL if the flash id did not match
 *   the expected 8 MiB part.
 *
 ****************************************************************************/

#ifdef CONFIG_BK7258_FLASH_MTD
FAR struct mtd_dev_s *bk7258_flash_mtd_initialize(void);

#endif

#endif /* __BOARDS_ARM_BK7258_SRC_BK7258_FLASH_MTD_H */
