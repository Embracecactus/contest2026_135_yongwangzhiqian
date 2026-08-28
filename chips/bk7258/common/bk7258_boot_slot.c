/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/common/bk7258_boot_slot.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/chip/bk7258_boot_slot.h>
#include <arch/chip/bk7258_flash_remap.h>

static uint32_t bk7258_boot_slot_getreg(uintptr_t address)
{
  return *(volatile uint32_t *)address;
}

int bk7258_boot_active_slot(FAR const struct bk7258_boot_slot_map_s *map,
                            FAR enum bk7258_boot_slot_e *slot)
{
  uint32_t enabled;

  if (map == NULL || slot == NULL ||
      map->version != BK7258_BOOT_SLOT_MAP_VERSION ||
      map->size < sizeof(*map) || map->secondary_begin == 0u ||
      map->secondary_end <= map->secondary_begin ||
      map->secondary_offset == 0u)
    {
      return -EINVAL;
    }

  *slot = BK7258_BOOT_SLOT_INVALID;
  enabled = bk7258_boot_slot_getreg(BK7258_FLASH_REMAP_ENABLE_REG);

  if ((enabled & BK7258_FLASH_REMAP_ENABLE_BIT) == 0u)
    {
      *slot = BK7258_BOOT_SLOT_PRIMARY;
      return 0;
    }

  if (bk7258_boot_slot_getreg(BK7258_FLASH_REMAP_BEGIN_REG) !=
        map->secondary_begin ||
      bk7258_boot_slot_getreg(BK7258_FLASH_REMAP_END_REG) !=
        map->secondary_end ||
      bk7258_boot_slot_getreg(BK7258_FLASH_REMAP_OFFSET_REG) !=
        map->secondary_offset)
    {
      return -EILSEQ;
    }

  *slot = BK7258_BOOT_SLOT_SECONDARY;
  return 0;
}
