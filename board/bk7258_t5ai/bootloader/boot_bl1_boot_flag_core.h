/* SPDX-License-Identifier: Apache-2.0 */
/* Pure decoder for the public Beken BL1 boot_flag record.
 *
 * This module only decodes and validates bytes supplied by its caller.  It
 * does not choose a Flash address, read or write Flash, or touch OTP/eFuse.
 */
#ifndef BK7258_BOOT_BL1_BOOT_FLAG_CORE_H
#define BK7258_BOOT_BL1_BOOT_FLAG_CORE_H

#include <stddef.h>
#include <stdint.h>

#define BK7258_BL1_BOOT_FLAG_MAGIC       0x4c725463u
#define BK7258_BL1_BOOT_FLAG_RECORD_SIZE 0x28u
#define BK7258_BL1_BOOT_FLAG_PRIMARY     1u
#define BK7258_BL1_BOOT_FLAG_SECONDARY   2u

struct bk7258_bl1_boot_flag_s
{
  uint32_t magic;
  uint32_t boot_flag;
  uint32_t primary_manifest_addr;
  uint32_t recovery_manifest_addr;
  uint32_t pll_ena;
  uint32_t security_boot_supported;
  uint32_t security_boot_ena;
  uint32_t security_boot_print_dis;
  uint32_t jtag_dis;
  uint32_t sw_fih_delay_ena;
};

_Static_assert(sizeof(struct bk7258_bl1_boot_flag_s) ==
               BK7258_BL1_BOOT_FLAG_RECORD_SIZE,
               "BL1 boot_flag ABI must remain exactly 0x28 bytes");

struct bk7258_bl1_boot_flag_layout_s
{
  uint32_t primary_manifest_addr;
  uint32_t recovery_manifest_addr;
};

enum bk7258_bl1_boot_flag_parse_status_e
{
  BK7258_BL1_BOOT_FLAG_PARSE_ABSENT = 0,
  BK7258_BL1_BOOT_FLAG_PARSE_ERASED,
  BK7258_BL1_BOOT_FLAG_PARSE_INVALID,
  BK7258_BL1_BOOT_FLAG_PARSE_VALID
};

/* Decode exactly the recovered 10 little-endian words.  The input length
 * must be exactly 0x28; callers reading a larger partition must first select
 * the record itself.  On failure, the output is left unchanged. */
int bk7258_bl1_boot_flag_decode(const uint8_t *record, size_t size,
                              struct bk7258_bl1_boot_flag_s *control);

/* Validate a decoded record against caller-supplied, already-proven layout
 * addresses.  No default addresses are invented in this layer. */
int bk7258_bl1_boot_flag_validate(
  const struct bk7258_bl1_boot_flag_s *control,
  const struct bk7258_bl1_boot_flag_layout_s *layout);

/* Fail-closed decode + validation.  Output is zeroed unless VALID is
 * returned, so policy callers cannot accidentally consume invalid fields. */
enum bk7258_bl1_boot_flag_parse_status_e
bk7258_bl1_boot_flag_parse(
  const uint8_t *record, size_t size,
  const struct bk7258_bl1_boot_flag_layout_s *layout,
  struct bk7258_bl1_boot_flag_s *control);

#endif /* BK7258_BOOT_BL1_BOOT_FLAG_CORE_H */
