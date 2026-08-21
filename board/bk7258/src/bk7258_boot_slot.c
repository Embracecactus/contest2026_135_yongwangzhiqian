/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/bk7258_boot_slot.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <arch/board/bk7258_boot_slot.h>
#include <arch/board/bk7258_image_layout.h>

static uint32_t bk7258_boot_slot_getreg(uintptr_t address)
{
  return *(volatile uint32_t *)address;
}

enum bk7258_boot_slot_e bk7258_boot_active_slot(void)
{
  uint32_t enabled = bk7258_boot_slot_getreg(BK7258_FLASH_REMAP_ENABLE_REG);

  if ((enabled & BK7258_FLASH_REMAP_ENABLE_BIT) == 0u)
    {
      return BK7258_BOOT_SLOT_PRIMARY;
    }

  if (bk7258_boot_slot_getreg(BK7258_FLASH_REMAP_BEGIN_REG) !=
        BK7258_AB_REMAP_BEGIN ||
      bk7258_boot_slot_getreg(BK7258_FLASH_REMAP_END_REG) !=
        BK7258_AB_REMAP_END ||
      bk7258_boot_slot_getreg(BK7258_FLASH_REMAP_OFFSET_REG) !=
        BK7258_AB_REMAP_OFFSET)
    {
      return BK7258_BOOT_SLOT_INVALID;
    }

  return BK7258_BOOT_SLOT_SECONDARY;
}
