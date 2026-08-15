/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_flash_mtd.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 on-chip Flash MTD lower-halves.
 *
 * The data MTD covers LittleFS.  A NuttX MCUboot BL2 build can additionally
 * create two read-only MTD partitions for the physical A/B image pairs.
 *
 * read/erase/bwrite use the board-verified flash-controller sequence with an
 * option-A SR0 block-protect policy (cleared around each op, restored
 * afterwards, so the boot/app region keeps its hardware protection outside
 * the op window).
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASH_MTD_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASH_MTD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/mtd/mtd.h>

enum bk7258_mcuboot_mtd_slot_e
{
  BK7258_MCUBOOT_MTD_SLOT_PRIMARY = 0,
  BK7258_MCUBOOT_MTD_SLOT_SECONDARY = 1
};

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

/* Return a bounds-checked, read-only MTD child for a signed image pair. */

#ifdef CONFIG_MCUBOOT_BOOTLOADER
FAR struct mtd_dev_s *
bk7258_mcuboot_mtd_get(enum bk7258_mcuboot_mtd_slot_e slot);
#endif
#endif

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASH_MTD_H */
