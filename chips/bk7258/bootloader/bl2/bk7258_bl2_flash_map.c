/* SPDX-License-Identifier: Apache-2.0 */
/* Board flash-map ABI for the bare-metal, direct-XIP MCUboot BL2. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "flash_map_backend/flash_map_backend.h"
#include <bootutil/bootutil_public.h>
#include "bk7258_bl2_abi.h"
#include "boot_flash.h"
#include "boot_wdt.h"

#define FLASH_CP_PRIMARY 0
#define FLASH_CP_SECONDARY 1
#define FLASH_AP_PRIMARY 2
#define FLASH_AP_SECONDARY 3
static uint8_t g_bk7258_bl2_flash_sector[BK7258_FLASH_ERASE_SIZE];

/* MCUboot normally selects each image ID independently.  The BK7258 board
 * stores CP and AP as two image IDs, but they are one launchable pair: the CP
 * code always releases the AP through the selected pair's remapped A window.
 * Restricting the visible headers to one slot per boot_go() call turns the
 * board contract into an atomic pair without changing upstream bootutil. */
static int g_bk7258_bl2_slot_limit = BK7258_BL2_SLOTS_BOTH;

void bk7258_bl2_set_slot_limit(int slot)
{
  if (slot == BK7258_BL2_SLOT_PRIMARY ||
      slot == BK7258_BL2_SLOT_SECONDARY)
    {
      g_bk7258_bl2_slot_limit = slot;
    }
  else
    {
      g_bk7258_bl2_slot_limit = BK7258_BL2_SLOTS_BOTH;
    }
}

/* Kept as a source-compatible alias for the earlier malformed-AP retry path. */
void bk7258_bl2_primary_only(bool enabled)
{
  bk7258_bl2_set_slot_limit(enabled ? BK7258_BL2_SLOT_PRIMARY :
                            BK7258_BL2_SLOTS_BOTH);
}

static const struct flash_area g_cp_primary =
{
  FLASH_CP_PRIMARY, 0, 0, BK7258_ARTIFACT_CP_XIP_START,
  BK7258_ARTIFACT_CP_LOGICAL_SIZE
};

static const struct flash_area g_cp_secondary =
{
  FLASH_CP_SECONDARY, 0, 0, BK7258_BL2_B_CP_XIP_START,
  BK7258_ARTIFACT_CP_LOGICAL_SIZE
};

static const struct flash_area g_ap_primary =
{
  FLASH_AP_PRIMARY, 0, 0, BK7258_ARTIFACT_AP_XIP_START,
  BK7258_ARTIFACT_AP_LOGICAL_SIZE
};

static const struct flash_area g_ap_secondary =
{
  FLASH_AP_SECONDARY, 0, 0, BK7258_BL2_B_AP_XIP_START,
  BK7258_ARTIFACT_AP_LOGICAL_SIZE
};

int flash_area_open(uint8_t id, const struct flash_area **fa)
{
  if (fa == NULL)
    {
      return -1;
    }

  if (id == FLASH_CP_PRIMARY)
    {
      *fa = &g_cp_primary;
      return 0;
    }
  if (id == FLASH_CP_SECONDARY)
    {
      *fa = &g_cp_secondary;
      return 0;
    }
  if (id == FLASH_AP_PRIMARY)
    {
      *fa = &g_ap_primary;
      return 0;
    }
  if (id == FLASH_AP_SECONDARY)
    {
      *fa = &g_ap_secondary;
      return 0;
    }
  return -1;
}

void flash_area_close(const struct flash_area *fa)
{
  (void)fa;
}

int flash_area_read(const struct flash_area *fa, uint32_t off,
                    void *dst, uint32_t len)
{
  volatile const uint8_t *src;
  uint8_t *out = dst;
  uint32_t i;

  if (fa == NULL || dst == NULL || off > fa->fa_size ||
      len > fa->fa_size - off)
    {
      return -1;
    }

  if ((g_bk7258_bl2_slot_limit == BK7258_BL2_SLOT_PRIMARY &&
       (fa->fa_id == FLASH_CP_SECONDARY || fa->fa_id == FLASH_AP_SECONDARY)) ||
      (g_bk7258_bl2_slot_limit == BK7258_BL2_SLOT_SECONDARY &&
       (fa->fa_id == FLASH_CP_PRIMARY || fa->fa_id == FLASH_AP_PRIMARY)))
    {
      /* A hidden slot must look erased, not unreadable.  bootutil reads slot 0
       * first and treats a slot-0 read error as fatal, while an erased header
       * is the normal "no image" result for either slot. */
      for (i = 0; i < len; i++)
        {
          out[i] = 0xffu;
        }
      return 0;
    }

  /* Image hashing is performed in this callback and can outlive the BL1
   * watchdog interval on the 34/32-decoded XIP path. */
  boot_wdt_feed_period(BL2_WDT_PERIOD);

  src = (volatile const uint8_t *)(uintptr_t)(fa->fa_off + off);
  for (i = 0; i < len; i++)
    {
      out[i] = src[i];
    }
  return 0;
}

static int bk7258_bl2_raw_area(const struct flash_area *fa,
                               uint32_t *base, uint32_t *size)
{
  if (fa == NULL || base == NULL || size == NULL)
    {
      return -1;
    }

  switch (fa->fa_id)
    {
      case FLASH_CP_PRIMARY:
        *base = BK7258_CP_RAW_PHYSICAL_START;
        *size = BK7258_CP_RAW_PHYSICAL_SIZE;
        return 0;
      case FLASH_CP_SECONDARY:
        *base = BK7258_AB_SECONDARY_CP_RAW_START;
        *size = BK7258_CP_RAW_PHYSICAL_SIZE;
        return 0;
      case FLASH_AP_PRIMARY:
        *base = BK7258_AP_RAW_PHYSICAL_START;
        *size = BK7258_AP_RAW_PHYSICAL_SIZE;
        return 0;
      case FLASH_AP_SECONDARY:
        *base = BK7258_AB_SECONDARY_AP_RAW_START;
        *size = BK7258_AP_RAW_PHYSICAL_SIZE;
        return 0;
      default:
        return -1;
    }
}

static bool bk7258_bl2_area_visible(const struct flash_area *fa)
{
  if (fa == NULL)
    {
      return false;
    }

  if (g_bk7258_bl2_slot_limit == BK7258_BL2_SLOT_PRIMARY)
    {
      return fa->fa_id == FLASH_CP_PRIMARY || fa->fa_id == FLASH_AP_PRIMARY;
    }

  if (g_bk7258_bl2_slot_limit == BK7258_BL2_SLOT_SECONDARY)
    {
      return fa->fa_id == FLASH_CP_SECONDARY ||
             fa->fa_id == FLASH_AP_SECONDARY;
    }

  return false;
}

static uint32_t bk7258_bl2_copy_done_off(const struct flash_area *fa)
{
  uint32_t magic = fa->fa_size - BOOT_MAGIC_SZ;
  uint32_t image_ok = (magic - BOOT_MAX_ALIGN) & ~(BOOT_MAX_ALIGN - 1u);

  return image_ok - BOOT_MAX_ALIGN;
}

static uint16_t bk7258_bl2_crc16(const uint8_t *data)
{
  uint16_t crc = 0xffffu;
  uint32_t index;
  uint32_t bit;

  for (index = 0; index < BK7258_FLASH_CRC_DATA_SIZE; index++)
    {
      crc ^= (uint16_t)data[index] << 8;
      for (bit = 0; bit < 8u; bit++)
        {
          crc = (uint16_t)((crc << 1) ^
            ((crc & 0x8000u) != 0u ? 0x8005u : 0u));
        }
    }

  return crc;
}

static int bk7258_bl2_raw_replace(uint32_t address,
                                  const uint8_t *source, uint32_t length)
{
  uint8_t verify[BK7258_FLASH_CRC_DATA_SIZE];
  uint32_t end;

  if (source == NULL || length == 0u || address >= BK7258_FLASH_SIZE ||
      length > BK7258_FLASH_SIZE - address)
    {
      return -1;
    }

  end = address + length;
  while (address < end)
    {
      uint32_t sector = address & ~(BK7258_FLASH_ERASE_SIZE - 1u);
      uint32_t offset = address - sector;
      uint32_t count = BK7258_FLASH_ERASE_SIZE - offset;
      uint32_t index;

      if (count > end - address)
        {
          count = end - address;
        }

      if (bk7258_boot_flash_read(sector, g_bk7258_bl2_flash_sector,
                                 sizeof(g_bk7258_bl2_flash_sector)) < 0)
        {
          return -1;
        }

      for (index = 0; index < count; index++)
        {
          g_bk7258_bl2_flash_sector[offset + index] = source[index];
        }

      if (bk7258_boot_flash_erase(sector, BK7258_FLASH_ERASE_SIZE) < 0 ||
          bk7258_boot_flash_program(sector, g_bk7258_bl2_flash_sector,
                                    sizeof(g_bk7258_bl2_flash_sector)) < 0)
        {
          return -1;
        }

      /* The controller reports command completion before the board can rely
       * on the rewritten CRC packets.  Verify the complete RMW sector before
       * allowing MCUboot to boot an image whose copy_done mutation it owns. */
      for (index = 0; index < BK7258_FLASH_ERASE_SIZE;
           index += sizeof(verify))
        {
          uint32_t check;

          if (bk7258_boot_flash_read(sector + index, verify,
                                     sizeof(verify)) < 0)
            {
              return -1;
            }

          for (check = 0; check < sizeof(verify); check++)
            {
              if (verify[check] != g_bk7258_bl2_flash_sector[index + check])
                {
                  return -1;
                }
            }
        }

      address += count;
      source += count;
      boot_wdt_feed_period(BL2_WDT_PERIOD);
    }

  return 0;
}

int flash_area_write(const struct flash_area *fa, uint32_t off,
                     const void *src, uint32_t len)
{
  const uint8_t *input = src;
  uint32_t raw_base;
  uint32_t raw_size;

  /* Direct-XIP-revert writes exactly one board-owned field in BL2:
   * copy_done.  Magic is supplied by imgtool, image_ok is owned by the
   * runtime health-confirm path, and all other trailer/status bytes remain
   * inaccessible here. */
  if (fa == NULL || src == NULL || len != 1u ||
      off != bk7258_bl2_copy_done_off(fa) || input[0] != BOOT_FLAG_SET ||
      !bk7258_bl2_area_visible(fa) ||
      bk7258_bl2_raw_area(fa, &raw_base, &raw_size) < 0)
    {
      return -1;
    }

  while (len != 0u)
    {
      uint8_t logical[BK7258_FLASH_CRC_DATA_SIZE];
      uint8_t encoded[BK7258_FLASH_CRC_TOTAL_SIZE];
      uint32_t group = off / BK7258_FLASH_CRC_DATA_SIZE;
      uint32_t in_group = off % BK7258_FLASH_CRC_DATA_SIZE;
      uint32_t count = BK7258_FLASH_CRC_DATA_SIZE - in_group;
      uint32_t raw_offset = group * BK7258_FLASH_CRC_TOTAL_SIZE;
      uint16_t crc;
      uint32_t index;

      if (count > len)
        {
          count = len;
        }

      if (raw_offset > raw_size - BK7258_FLASH_CRC_TOTAL_SIZE ||
          flash_area_read(fa, group * BK7258_FLASH_CRC_DATA_SIZE,
                          logical, sizeof(logical)) < 0)
        {
          return -1;
        }

      for (index = 0; index < count; index++)
        {
          logical[in_group + index] = input[index];
        }

      for (index = 0; index < BK7258_FLASH_CRC_DATA_SIZE; index++)
        {
          encoded[index] = logical[index];
        }

      crc = bk7258_bl2_crc16(logical);
      encoded[BK7258_FLASH_CRC_DATA_SIZE] = (uint8_t)(crc >> 8);
      encoded[BK7258_FLASH_CRC_DATA_SIZE + 1u] = (uint8_t)crc;
      if (bk7258_bl2_raw_replace(raw_base + raw_offset, encoded,
                                 sizeof(encoded)) < 0)
        {
          /* Upstream direct-XIP deliberately treats a copy_done write error
           * as non-fatal.  That is unsafe for this board because it would
           * repeatedly boot an untracked candidate, so a hardware mutation
           * failure must reset before application handoff. */
          boot_wdt_fail_reset();
        }

      off += count;
      input += count;
      len -= count;
    }

  return 0;
}

int flash_area_erase(const struct flash_area *fa, uint32_t off, uint32_t len)
{
  uint32_t raw_base;
  uint32_t raw_size;

  if (fa == NULL || off != 0u || len != fa->fa_size ||
      !bk7258_bl2_area_visible(fa) ||
      bk7258_bl2_raw_area(fa, &raw_base, &raw_size) < 0 ||
      (raw_base & (BK7258_FLASH_ERASE_SIZE - 1u)) != 0u ||
      (raw_size & (BK7258_FLASH_ERASE_SIZE - 1u)) != 0u)
    {
      return -1;
    }

  if (bk7258_boot_flash_erase(raw_base, raw_size) < 0)
    {
      boot_wdt_fail_reset();
    }

  return 0;
}

uint32_t flash_area_align(const struct flash_area *fa)
{
  (void)fa;
  return 1;
}

uint8_t flash_area_erased_val(const struct flash_area *fa)
{
  (void)fa;
  return 0xff;
}

int flash_area_get_sectors(int id, uint32_t *count, struct flash_sector *sectors)
{
  const struct flash_area *fa;


  if (count == NULL || sectors == NULL || *count == 0 ||
      flash_area_open((uint8_t)id, &fa) != 0)
    {
      return -1;
    }
  sectors[0].fs_off = 0;
  sectors[0].fs_size = fa->fa_size;
  *count = 1;
  return 0;
}

int flash_area_id_from_multi_image_slot(int image_index, int slot)
{
  if (slot != 0 && slot != 1)
    {
      return -1;
    }
  if (image_index == 0)
    {
      return slot == 0 ? FLASH_CP_PRIMARY : FLASH_CP_SECONDARY;
    }
  if (image_index == 1)
    {
      return slot == 0 ? FLASH_AP_PRIMARY : FLASH_AP_SECONDARY;
    }
  return -1;
}

int flash_area_id_from_image_slot(int slot)
{
  return slot == 0 ? FLASH_CP_PRIMARY :
    (slot == 1 ? FLASH_CP_SECONDARY : -1);
}

int flash_area_id_to_multi_image_slot(int image_index, int id)
{
  if (image_index == 0)
    {
      return id == FLASH_CP_PRIMARY ? 0 :
        (id == FLASH_CP_SECONDARY ? 1 : -1);
    }
  if (image_index == 1)
    {
      return id == FLASH_AP_PRIMARY ? 0 :
        (id == FLASH_AP_SECONDARY ? 1 : -1);
    }
  return -1;
}

int flash_area_id_from_image_offset(uint32_t offset)
{
  if (offset == g_cp_primary.fa_off)
    {
      return FLASH_CP_PRIMARY;
    }
  if (offset == g_cp_secondary.fa_off)
    {
      return FLASH_CP_SECONDARY;
    }
  if (offset == g_ap_primary.fa_off)
    {
      return FLASH_AP_PRIMARY;
    }
  return offset == g_ap_secondary.fa_off ? FLASH_AP_SECONDARY : -1;
}
