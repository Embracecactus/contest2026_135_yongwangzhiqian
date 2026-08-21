/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/include/bk7258_boot_slot.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARDS_ARM_BK7258_INCLUDE_BK7258_BOOT_SLOT_H
#define __BOARDS_ARM_BK7258_INCLUDE_BK7258_BOOT_SLOT_H

enum bk7258_boot_slot_e
{
  BK7258_BOOT_SLOT_INVALID = -1,
  BK7258_BOOT_SLOT_PRIMARY = 0,
  BK7258_BOOT_SLOT_SECONDARY = 1
};

/* Return the physical CP/AP pair selected by BL2.  A secondary result is
 * accepted only when every retained remap register matches the generated
 * layout contract; an unexpected enabled remap fails closed. */

enum bk7258_boot_slot_e bk7258_boot_active_slot(void);

#endif /* __BOARDS_ARM_BK7258_INCLUDE_BK7258_BOOT_SLOT_H */
