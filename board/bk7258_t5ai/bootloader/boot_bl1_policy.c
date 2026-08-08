#include "boot_bl1_policy.h"
#include "boot_bl1_manifest.h"

enum bk7258_bl1_boot_flag_status_e
bk7258_bl1_boot_flag_slot_order(const uint8_t *record, size_t size,
                              const struct bk7258_bl1_boot_flag_layout_s *layout,
                              uint8_t order[2])
{
  struct bk7258_bl1_boot_flag_s control;
  enum bk7258_bl1_boot_flag_parse_status_e parse_status;

  if (order == (uint8_t *)0)
    {
      return BK7258_BL1_BOOT_FLAG_INVALID;
    }

  order[0] = BK7258_BL1_SLOT_PRIMARY;
  order[1] = BK7258_BL1_SLOT_SECONDARY;

  parse_status = bk7258_bl1_boot_flag_parse(record, size, layout, &control);
  if (parse_status == BK7258_BL1_BOOT_FLAG_PARSE_ABSENT)
    {
      return BK7258_BL1_BOOT_FLAG_ABSENT;
    }

  if (parse_status == BK7258_BL1_BOOT_FLAG_PARSE_ERASED)
    {
      return BK7258_BL1_BOOT_FLAG_ERASED;
    }

  if (parse_status != BK7258_BL1_BOOT_FLAG_PARSE_VALID)
    {
      return BK7258_BL1_BOOT_FLAG_INVALID;
    }

  if (control.boot_flag == BK7258_BL1_BOOT_FLAG_SECONDARY)
    {
      order[0] = BK7258_BL1_SLOT_SECONDARY;
      order[1] = BK7258_BL1_SLOT_PRIMARY;
      return BK7258_BL1_BOOT_FLAG_VALID_SECONDARY;
    }

  return BK7258_BL1_BOOT_FLAG_VALID_PRIMARY;
}

__attribute__((weak))
uint32_t bk7258_bl1_manifest_version_floor_readonly(void)
{
  return 0u;
}

uint32_t bk7258_bl1_security_counter_decode(uint32_t bitmap)
{
  uint32_t version = 0u;

  while ((bitmap & 1u) != 0u)
    {
      version++;
      bitmap >>= 1;
    }

  return version;
}

uint32_t bk7258_bl1_manifest_effective_floor(uint32_t readonly_floor)
{
  return readonly_floor > BK7258_BL1_MANIFEST_MIN_IMAGE_VERSION ?
         readonly_floor : BK7258_BL1_MANIFEST_MIN_IMAGE_VERSION;
}

int bk7258_bl1_manifest_version_allowed(uint32_t manifest_version,
                                        uint32_t readonly_floor)
{
  return manifest_version >=
         bk7258_bl1_manifest_effective_floor(readonly_floor);
}
