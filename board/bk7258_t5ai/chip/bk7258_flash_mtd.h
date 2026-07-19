/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_flash_mtd.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) on-chip flash MTD lower-half for the Stage N5 verified data
 * partition (logical 0x00100000..0x001FFFFF, 1 MiB).
 *
 * Stage N5-D5 board-verified the raw flash erase/write/read-back/re-erase path
 * on the first 4 KiB sector of this partition.  This header exposes the NuttX
 * MTD instance built on the same proven read path.
 *
 * Status (N5-D6): read + geometry are board-verified.  erase/bwrite are
 * implemented using the N5-D5 board-verified sequence with the option-A SR0
 * block-protect policy (cleared around each op, restored afterwards, so the
 * boot/app region keeps its hardware protection outside the op window).
 * erase/bwrite are build-verified only until a board self-test or filesystem
 * mount is explicitly authorised.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASH_MTD_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASH_MTD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/mtd/mtd.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_flash_mtd_initialize
 *
 * Description:
 *   Create (or return the singleton) MTD device instance for the verified
 *   1 MiB data partition.  Reads the JEDEC id once to confirm the 8 MiB NOR
 *   and pushes a compact evidence line over UART1.  Performs no erase, no
 *   write, and no status-register change at init time.
 *
 * Returned Value:
 *   Pointer to the mtd_dev_s instance, or NULL if the flash id did not match
 *   the expected 8 MiB part.
 *
 ****************************************************************************/

#ifdef CONFIG_BK7258_FLASH_MTD
FAR struct mtd_dev_s *bk7258_flash_mtd_initialize(void);
#endif

/****************************************************************************
 * Name: bk7258_flash_mtd_selftest
 *
 * Description:
 *   Destructive one-shot write-path self-test on block 0 (first 4 KiB of the
 *   data partition): erase -> write pattern -> read-back compare -> re-erase
 *   -> verify erased.  Proves the MTD erase/bwrite path and the option-A SR0
 *   clear/restore.  Restores block 0 to 0xff on success and best-effort on
 *   failure.  Enable only for one board run.
 *
 ****************************************************************************/

#if defined(CONFIG_BK7258_FLASH_MTD) && defined(CONFIG_BK7258_FLASH_MTD_SELFTEST)
int bk7258_flash_mtd_selftest(FAR struct mtd_dev_s *dev);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASH_MTD_H */
