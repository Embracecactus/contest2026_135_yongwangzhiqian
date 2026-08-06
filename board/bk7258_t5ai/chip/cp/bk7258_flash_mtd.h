/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/cp/bk7258_flash_mtd.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) on-chip Flash MTD lower-halves.
 *
 * The data MTD covers LittleFS.  The OTA accessors create two private NuttX
 * MTD partitions for the physical A/B image pairs.  They are intentionally
 * not registered as /dev nodes: only the OTA staging adapter receives them.
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

enum bk7258_ota_mtd_slot_e
{
  BK7258_OTA_MTD_SLOT_A = 0,
  BK7258_OTA_MTD_SLOT_B = 1
};

/* Format-3 metadata is private to the OTA adapter.  The one-way policy
 * sector has no MTD accessor and therefore cannot be changed by this path. */

enum bk7258_ota_n17_mtd_region_e
{
  BK7258_OTA_N17_MTD_JOURNAL_PRIMARY = 0,
  BK7258_OTA_N17_MTD_JOURNAL_MIRROR,
  BK7258_OTA_N17_MTD_MANIFEST_A,
  BK7258_OTA_N17_MTD_MANIFEST_B,
  BK7258_OTA_N17_MTD_REGION_COUNT
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

/* Return an internal, bounds-checked MTD child for an image pair.  This does
 * not relax the Flash guard: writes still require the N15 staging gate and
 * must target the inactive physical pair.
 */

#ifdef CONFIG_BK7258_OTA_STAGING
FAR struct mtd_dev_s *
bk7258_ota_mtd_get(enum bk7258_ota_mtd_slot_e slot);

FAR struct mtd_dev_s *
bk7258_ota_n17_mtd_get(enum bk7258_ota_n17_mtd_region_e region);
#endif
#endif

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASH_MTD_H */
