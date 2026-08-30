/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * Host contract test for the BK7258 chip-owned Flash-remap decoder.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/chip/bk7258_boot_slot.h>

volatile uint32_t g_bk7258_test_remap_regs[4];

int main(void)
{
  const struct bk7258_boot_slot_map_s valid =
  {
    .version = BK7258_BOOT_SLOT_MAP_VERSION,
    .size = sizeof(struct bk7258_boot_slot_map_s),
    .secondary_begin = 0x02010000u,
    .secondary_end = 0x02400000u,
    .secondary_offset = 0x00400000u,
  };
  struct bk7258_boot_slot_map_s invalid = valid;
  enum bk7258_boot_slot_e slot = BK7258_BOOT_SLOT_INVALID;

  memset((void *)g_bk7258_test_remap_regs, 0,
         sizeof(g_bk7258_test_remap_regs));
  assert(bk7258_boot_active_slot(&valid, &slot) == 0);
  assert(slot == BK7258_BOOT_SLOT_PRIMARY);

  g_bk7258_test_remap_regs[0] = valid.secondary_begin;
  g_bk7258_test_remap_regs[1] = valid.secondary_end;
  g_bk7258_test_remap_regs[2] = valid.secondary_offset;
  g_bk7258_test_remap_regs[3] = 1u;
  assert(bk7258_boot_active_slot(&valid, &slot) == 0);
  assert(slot == BK7258_BOOT_SLOT_SECONDARY);

  g_bk7258_test_remap_regs[2]++;
  assert(bk7258_boot_active_slot(&valid, &slot) == -EILSEQ);
  assert(slot == BK7258_BOOT_SLOT_INVALID);

  invalid.secondary_end = invalid.secondary_begin;
  assert(bk7258_boot_active_slot(&invalid, &slot) == -EINVAL);
  assert(bk7258_boot_active_slot(NULL, &slot) == -EINVAL);
  assert(bk7258_boot_active_slot(&valid, NULL) == -EINVAL);

  puts("bk7258 boot-slot decoder test: PASS");
  return 0;
}
