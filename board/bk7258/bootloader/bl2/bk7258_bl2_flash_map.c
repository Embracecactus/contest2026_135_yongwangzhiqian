/* Board flash-map ABI for the bare-metal, direct-XIP MCUboot BL2. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "flash_map_backend/flash_map_backend.h"
#include "bk7258_bl2_abi.h"
#include "../boot_wdt.h"

#define FLASH_CP_PRIMARY 0
#define FLASH_CP_SECONDARY 1
#define FLASH_AP_PRIMARY 2
#define FLASH_AP_SECONDARY 3

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
  FLASH_CP_PRIMARY, 0, 0, BK7258_ROLE_SLOT_A_CP_XIP_START,
  BK7258_ROLE_SLOT_A_CP_LOGICAL_SIZE
};

static const struct flash_area g_cp_secondary =
{
  FLASH_CP_SECONDARY, 0, 0, BK7258_BL2_B_CP_XIP_START,
  BK7258_ROLE_SLOT_A_CP_LOGICAL_SIZE
};

static const struct flash_area g_ap_primary =
{
  FLASH_AP_PRIMARY, 0, 0, BK7258_ROLE_SLOT_A_AP_XIP_START,
  BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE
};

static const struct flash_area g_ap_secondary =
{
  FLASH_AP_SECONDARY, 0, 0, BK7258_BL2_B_AP_XIP_START,
  BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE
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

int flash_area_write(const struct flash_area *fa, uint32_t off,
                     const void *src, uint32_t len)
{
  (void)fa;
  (void)off;
  (void)src;
  (void)len;
  return -1;
}

int flash_area_erase(const struct flash_area *fa, uint32_t off, uint32_t len)
{
  (void)fa;
  (void)off;
  (void)len;
  return -1;
}

uint32_t flash_area_align(const struct flash_area *fa)
{
  (void)fa;
  return 4;
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
