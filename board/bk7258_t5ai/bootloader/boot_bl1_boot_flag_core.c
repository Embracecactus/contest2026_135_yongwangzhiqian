/* SPDX-License-Identifier: Apache-2.0 */

#include "boot_bl1_boot_flag_core.h"

static uint32_t get_le32(const uint8_t *value)
{
  return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
         ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int bytes_are_ff(const uint8_t *value, size_t size)
{
  size_t index;

  for (index = 0; index < size; index++)
    {
      if (value[index] != 0xffu)
        {
          return 0;
        }
    }

  return 1;
}

static void control_clear(struct bk7258_bl1_boot_flag_s *control)
{
  control->magic = 0u;
  control->boot_flag = 0u;
  control->primary_manifest_addr = 0u;
  control->recovery_manifest_addr = 0u;
  control->pll_ena = 0u;
  control->security_boot_supported = 0u;
  control->security_boot_ena = 0u;
  control->security_boot_print_dis = 0u;
  control->jtag_dis = 0u;
  control->sw_fih_delay_ena = 0u;
}

static int is_boolean(uint32_t value)
{
  return value <= 1u;
}

int bk7258_bl1_boot_flag_decode(const uint8_t *record, size_t size,
                              struct bk7258_bl1_boot_flag_s *control)
{
  if (record == (const uint8_t *)0 || control == (void *)0 ||
      size != BK7258_BL1_BOOT_FLAG_RECORD_SIZE)
    {
      return -1;
    }

  control->magic = get_le32(record + 0x00u);
  control->boot_flag = get_le32(record + 0x04u);
  control->primary_manifest_addr = get_le32(record + 0x08u);
  control->recovery_manifest_addr = get_le32(record + 0x0cu);
  control->pll_ena = get_le32(record + 0x10u);
  control->security_boot_supported = get_le32(record + 0x14u);
  control->security_boot_ena = get_le32(record + 0x18u);
  control->security_boot_print_dis = get_le32(record + 0x1cu);
  control->jtag_dis = get_le32(record + 0x20u);
  control->sw_fih_delay_ena = get_le32(record + 0x24u);
  return 0;
}

int bk7258_bl1_boot_flag_validate(
  const struct bk7258_bl1_boot_flag_s *control,
  const struct bk7258_bl1_boot_flag_layout_s *layout)
{
  if (control == (const void *)0 || layout == (const void *)0 ||
      control->magic != BK7258_BL1_BOOT_FLAG_MAGIC ||
      (control->boot_flag != BK7258_BL1_BOOT_FLAG_PRIMARY &&
       control->boot_flag != BK7258_BL1_BOOT_FLAG_SECONDARY) ||
      layout->primary_manifest_addr == 0u ||
      layout->recovery_manifest_addr == 0u ||
      layout->primary_manifest_addr == layout->recovery_manifest_addr ||
      (layout->primary_manifest_addr & 3u) != 0u ||
      (layout->recovery_manifest_addr & 3u) != 0u ||
      control->primary_manifest_addr != layout->primary_manifest_addr ||
      control->recovery_manifest_addr != layout->recovery_manifest_addr ||
      !is_boolean(control->pll_ena) ||
      !is_boolean(control->security_boot_supported) ||
      !is_boolean(control->security_boot_ena) ||
      !is_boolean(control->security_boot_print_dis) ||
      !is_boolean(control->jtag_dis) ||
      !is_boolean(control->sw_fih_delay_ena) ||
      (control->security_boot_ena != 0u &&
       control->security_boot_supported == 0u))
    {
      return 0;
    }

  return 1;
}

enum bk7258_bl1_boot_flag_parse_status_e
bk7258_bl1_boot_flag_parse(
  const uint8_t *record, size_t size,
  const struct bk7258_bl1_boot_flag_layout_s *layout,
  struct bk7258_bl1_boot_flag_s *control)
{
  struct bk7258_bl1_boot_flag_s decoded;

  if (control == (void *)0)
    {
      return BK7258_BL1_BOOT_FLAG_PARSE_INVALID;
    }

  control_clear(control);
  if (record == (const uint8_t *)0 || size == 0u)
    {
      return BK7258_BL1_BOOT_FLAG_PARSE_ABSENT;
    }

  if (size != BK7258_BL1_BOOT_FLAG_RECORD_SIZE)
    {
      return BK7258_BL1_BOOT_FLAG_PARSE_INVALID;
    }

  if (bytes_are_ff(record, BK7258_BL1_BOOT_FLAG_RECORD_SIZE))
    {
      return BK7258_BL1_BOOT_FLAG_PARSE_ERASED;
    }

  if (bk7258_bl1_boot_flag_decode(record, size, &decoded) < 0 ||
      !bk7258_bl1_boot_flag_validate(&decoded, layout))
    {
      return BK7258_BL1_BOOT_FLAG_PARSE_INVALID;
    }

  *control = decoded;
  return BK7258_BL1_BOOT_FLAG_PARSE_VALID;
}
