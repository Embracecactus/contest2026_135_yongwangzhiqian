/*
 * test_bl2_flash_map.c - host tests for the BK7258 board flash-map backend
 * (bk7258_bl2_flash_map.c).
 *
 * The real implementation is compiled from a throwaway patched copy whose
 * boot_wdt.h include points at the framework-patched header (REG32 routed
 * to mock_reg32_ref, ARM barrier neutralized).  The XIP window at
 * 0x02000000 is backed by the framework mock_flash mmap; reads dereference
 * real host memory exactly like the board's flash_area_read().
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "flash_map_backend/flash_map_backend.h"
#include <bootutil/bootutil_public.h>
#include "bk7258_bl2_abi.h"
#include <bk7258_partitions.h>

#include "mock_boot_flash.h"
#include "mock_reg32.h"
#include "mock_flash.h"

/* Alias kept source-compatible by the flash-map backend but not declared in
 * the board ABI header; the test pins its behavior through the header API. */
void bk7258_bl2_primary_only(bool enabled);

/* WDT_APB_CTRL in the patched boot_wdt.h; BL2_WDT_PERIOD = 60000. */
#define TEST_WDT_APB_CTRL 0x44800010u
#define TEST_WDT_AON_CTRL 0x44000600u
#define TEST_BL2_WDT_PERIOD 60000u

static uint8_t *g_xip;

static void prefill_slot(uint8_t id, uint8_t pattern)
{
  const struct flash_area *fa;
  uint32_t host_off;
  uint32_t i;

  assert_int_equal(flash_area_open(id, &fa), 0);
  host_off = fa->fa_off - BK7258_HOST_FLASH_XIP_BASE;
  for (i = 0; i < fa->fa_size; i++)
    {
      g_xip[host_off + i] = pattern;
    }
}

static int setup(void **state)
{
  (void)state;
  g_xip = mock_flash_map();
  assert_non_null(g_xip);
  mock_flash_erase_fill(0, BK7258_HOST_FLASH_SIZE);
  mock_boot_flash_reset();
  mock_reg32_reset();
  bk7258_bl2_set_slot_limit(BK7258_BL2_SLOTS_BOTH);
  return 0;
}

static int teardown(void **state)
{
  (void)state;
  mock_flash_unmap();
  return 0;
}

static void test_open_all_slots_and_close(void **state)
{
  const struct flash_area *fa;
  int id;

  (void)state;
  for (id = 0; id < 4; id++)
    {
      assert_int_equal(flash_area_open((uint8_t)id, &fa), 0);
      assert_non_null(fa);
      assert_int_equal(flash_area_get_id(fa), (uint8_t)id);
      assert_int_equal(flash_area_get_device_id(fa), 0);
      flash_area_close(fa);
    }
}

static void test_open_slot_geometry(void **state)
{
  const struct flash_area *fa;

  (void)state;
  assert_int_equal(flash_area_open(0, &fa), 0);
  assert_int_equal(flash_area_get_off(fa), BK7258_ARTIFACT_CP_XIP_START);
  assert_int_equal(flash_area_get_size(fa), BK7258_ARTIFACT_CP_LOGICAL_SIZE);

  assert_int_equal(flash_area_open(1, &fa), 0);
  assert_int_equal(flash_area_get_off(fa), BK7258_BL2_B_CP_XIP_START);
  assert_int_equal(flash_area_get_size(fa), BK7258_ARTIFACT_CP_LOGICAL_SIZE);

  assert_int_equal(flash_area_open(2, &fa), 0);
  assert_int_equal(flash_area_get_off(fa), BK7258_ARTIFACT_AP_XIP_START);
  assert_int_equal(flash_area_get_size(fa), BK7258_ARTIFACT_AP_LOGICAL_SIZE);

  assert_int_equal(flash_area_open(3, &fa), 0);
  assert_int_equal(flash_area_get_off(fa), BK7258_BL2_B_AP_XIP_START);
  assert_int_equal(flash_area_get_size(fa), BK7258_ARTIFACT_AP_LOGICAL_SIZE);
}

static void test_open_invalid_id(void **state)
{
  const struct flash_area *fa = NULL;

  (void)state;
  assert_int_equal(flash_area_open(4u, &fa), -1);
  assert_int_equal(flash_area_open(255u, &fa), -1);
  assert_null(fa);
}

static void test_open_null_fa(void **state)
{
  (void)state;
  assert_int_equal(flash_area_open(0u, NULL), -1);
}

static void test_read_bad_params(void **state)
{
  const struct flash_area *fa;
  uint8_t buf[8];

  (void)state;
  assert_int_equal(flash_area_open(0, &fa), 0);

  assert_int_equal(flash_area_read(NULL, 0, buf, sizeof(buf)), -1);
  assert_int_equal(flash_area_read(fa, 0, NULL, sizeof(buf)), -1);
  assert_int_equal(flash_area_read(fa, fa->fa_size, buf, 1), -1);
  assert_int_equal(flash_area_read(fa, fa->fa_size + 1, buf, 1), -1);
  assert_int_equal(flash_area_read(fa, 0, buf, fa->fa_size + 1), -1);
  assert_int_equal(flash_area_read(fa, fa->fa_size - 1, buf, 2), -1);
}

static void test_read_roundtrip_primary_cp(void **state)
{
  const struct flash_area *fa;
  uint8_t buf[64];
  uint32_t host_off;
  uint32_t i;

  (void)state;
  assert_int_equal(flash_area_open(0, &fa), 0);
  host_off = fa->fa_off - BK7258_HOST_FLASH_XIP_BASE;
  for (i = 0; i < sizeof(buf); i++)
    {
      g_xip[host_off + i] = (uint8_t)(0xa0u + i);
    }

  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);
  for (i = 0; i < sizeof(buf); i++)
    {
      assert_int_equal(buf[i], (uint8_t)(0xa0u + i));
    }

  /* Mid-slot read also resolves against the same XIP view. */
  assert_int_equal(flash_area_read(fa, 0x100, buf, 4), 0);
  assert_memory_equal(buf, &g_xip[host_off + 0x100], 4);
}

static void test_read_secondary_ap(void **state)
{
  const struct flash_area *fa;
  uint8_t buf[16];
  uint32_t host_off;

  (void)state;
  assert_int_equal(flash_area_open(3, &fa), 0);
  host_off = fa->fa_off - BK7258_HOST_FLASH_XIP_BASE;
  memset(&g_xip[host_off], 0x5a, sizeof(buf));
  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);
  assert_memory_equal(buf, &g_xip[host_off], sizeof(buf));
}

static void test_read_erased_returns_ff(void **state)
{
  const struct flash_area *fa;
  uint8_t buf[16];

  (void)state;
  assert_int_equal(flash_area_open(2, &fa), 0);
  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);
  for (uint32_t i = 0; i < sizeof(buf); i++)
    {
      assert_int_equal(buf[i], 0xffu);
    }
}

static void test_read_at_tail(void **state)
{
  const struct flash_area *fa;
  uint8_t buf[16];
  uint32_t host_off;

  (void)state;
  assert_int_equal(flash_area_open(0, &fa), 0);
  host_off = fa->fa_off - BK7258_HOST_FLASH_XIP_BASE;
  memset(&g_xip[host_off + fa->fa_size - sizeof(buf)], 0x3c, sizeof(buf));
  assert_int_equal(flash_area_read(fa, fa->fa_size - sizeof(buf), buf,
                                   sizeof(buf)), 0);
  for (uint32_t i = 0; i < sizeof(buf); i++)
    {
      assert_int_equal(buf[i], 0x3cu);
    }
}

static void test_read_feeds_watchdog(void **state)
{
  const struct flash_area *fa;
  uint8_t buf[4];

  (void)state;
  assert_int_equal(flash_area_open(0, &fa), 0);
  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);

  /* boot_wdt_feed_period(BL2_WDT_PERIOD): the second (apply) key write
   * leaves key2 in the ctrl registers. */
  assert_int_equal(mock_reg32_read(TEST_WDT_APB_CTRL),
                   (0xa5u << 16) | TEST_BL2_WDT_PERIOD);
  assert_int_equal(mock_reg32_read(TEST_WDT_AON_CTRL),
                   (0xa5u << 16) | TEST_BL2_WDT_PERIOD);
}

static void test_primary_only_hides_secondary(void **state)
{
  const struct flash_area *fa;
  uint8_t buf[16];
  uint32_t i;

  (void)state;
  bk7258_bl2_set_slot_limit(BK7258_BL2_SLOT_PRIMARY);

  prefill_slot(1, 0x22);
  assert_int_equal(flash_area_open(1, &fa), 0);
  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);
  for (i = 0; i < sizeof(buf); i++)
    {
      assert_int_equal(buf[i], 0xffu);
    }

  prefill_slot(3, 0x44);
  assert_int_equal(flash_area_open(3, &fa), 0);
  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);
  for (i = 0; i < sizeof(buf); i++)
    {
      assert_int_equal(buf[i], 0xffu);
    }

  /* Primary slots still read their real content. */
  assert_int_equal(flash_area_open(0, &fa), 0);
  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);
  for (i = 0; i < sizeof(buf); i++)
    {
      assert_int_equal(buf[i], 0xffu); /* untouched, hence erased */
    }
}

static void test_secondary_only_hides_primary(void **state)
{
  const struct flash_area *fa;
  uint8_t buf[16];
  uint32_t i;

  (void)state;
  bk7258_bl2_set_slot_limit(BK7258_BL2_SLOT_SECONDARY);

  prefill_slot(0, 0x11);
  assert_int_equal(flash_area_open(0, &fa), 0);
  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);
  for (i = 0; i < sizeof(buf); i++)
    {
      assert_int_equal(buf[i], 0xffu);
    }

  /* The visible secondary slot must return its real content. */
  prefill_slot(1, 0x22);
  assert_int_equal(flash_area_open(1, &fa), 0);
  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);
  for (i = 0; i < sizeof(buf); i++)
    {
      assert_int_equal(buf[i], 0x22u);
    }
}

static void test_slot_limit_both_restores_visibility(void **state)
{
  const struct flash_area *fa;
  uint8_t buf[8];
  uint32_t i;

  (void)state;
  bk7258_bl2_set_slot_limit(BK7258_BL2_SLOT_PRIMARY);
  bk7258_bl2_set_slot_limit(BK7258_BL2_SLOTS_BOTH);

  prefill_slot(1, 0x22);
  assert_int_equal(flash_area_open(1, &fa), 0);
  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);
  for (i = 0; i < sizeof(buf); i++)
    {
      assert_int_equal(buf[i], 0x22u);
    }
}

static void test_slot_limit_invalid_resets_to_both(void **state)
{
  const struct flash_area *fa;
  uint8_t buf[8];

  (void)state;
  bk7258_bl2_set_slot_limit(42);
  prefill_slot(1, 0x22);
  assert_int_equal(flash_area_open(1, &fa), 0);
  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);
  assert_int_equal(buf[0], 0x22u);
}

static void test_primary_only_alias(void **state)
{
  const struct flash_area *fa;
  uint8_t buf[8];

  (void)state;
  bk7258_bl2_primary_only(true);
  prefill_slot(1, 0x22);
  assert_int_equal(flash_area_open(1, &fa), 0);
  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);
  assert_int_equal(buf[0], 0xffu);

  bk7258_bl2_primary_only(false);
  assert_int_equal(flash_area_read(fa, 0, buf, sizeof(buf)), 0);
  assert_int_equal(buf[0], 0x22u);
}

static uint32_t copy_done_offset(const struct flash_area *fa)
{
  uint32_t magic = fa->fa_size - BOOT_MAGIC_SZ;
  uint32_t image_ok = (magic - BOOT_MAX_ALIGN) & ~(BOOT_MAX_ALIGN - 1u);

  return image_ok - BOOT_MAX_ALIGN;
}

static uint16_t test_crc16(const uint8_t data[BK7258_FLASH_CRC_DATA_SIZE])
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

static void test_write_rejects_outside_owned_copy_done(void **state)
{
  const struct flash_area *fa;
  uint8_t flag = BOOT_FLAG_SET;
  uint32_t off;

  (void)state;
  assert_int_equal(flash_area_open(0, &fa), 0);
  off = copy_done_offset(fa);

  assert_int_equal(flash_area_write(NULL, off, &flag, 1u), -1);
  assert_int_equal(flash_area_write(fa, off, NULL, 1u), -1);
  assert_int_equal(flash_area_write(fa, off - 1u, &flag, 1u), -1);
  assert_int_equal(flash_area_write(fa, off, &flag, 2u), -1);
  flag = 0xffu;
  assert_int_equal(flash_area_write(fa, off, &flag, 1u), -1);

  flag = BOOT_FLAG_SET;
  assert_int_equal(flash_area_write(fa, off, &flag, 1u), -1);
  assert_int_equal(mock_boot_flash_erase_calls(), 0u);
  assert_int_equal(mock_boot_flash_program_calls(), 0u);
}

static void test_write_copy_done_encodes_one_crc_group(void **state)
{
  const struct flash_area *fa;
  uint8_t logical[BK7258_FLASH_CRC_DATA_SIZE];
  uint8_t flag = BOOT_FLAG_SET;
  uint8_t *raw = mock_boot_flash_data();
  uint32_t off;
  uint32_t raw_off;
  uint16_t crc;

  (void)state;
  assert_int_equal(flash_area_open(0, &fa), 0);
  bk7258_bl2_set_slot_limit(BK7258_BL2_SLOT_PRIMARY);
  off = copy_done_offset(fa);
  raw_off = BK7258_CP_RAW_PHYSICAL_START +
            (off / BK7258_FLASH_CRC_DATA_SIZE) *
            BK7258_FLASH_CRC_TOTAL_SIZE;

  memset(logical, 0xff, sizeof(logical));
  logical[off % BK7258_FLASH_CRC_DATA_SIZE] = BOOT_FLAG_SET;
  raw[raw_off - 1u] = 0x5au;
  raw[raw_off + BK7258_FLASH_CRC_TOTAL_SIZE] = 0xa5u;

  assert_int_equal(flash_area_write(fa, off, &flag, 1u), 0);
  assert_memory_equal(&raw[raw_off], logical, sizeof(logical));
  crc = test_crc16(logical);
  assert_int_equal(raw[raw_off + BK7258_FLASH_CRC_DATA_SIZE],
                   (uint8_t)(crc >> 8));
  assert_int_equal(raw[raw_off + BK7258_FLASH_CRC_DATA_SIZE + 1u],
                   (uint8_t)crc);
  assert_int_equal(raw[raw_off - 1u], 0x5au);
  assert_int_equal(raw[raw_off + BK7258_FLASH_CRC_TOTAL_SIZE], 0xa5u);
  assert_int_equal(mock_boot_flash_erase_calls(), 1u);
  assert_int_equal(mock_boot_flash_program_calls(), 1u);
  assert_true(mock_boot_flash_read_calls() > 1u);
}

static void test_erase_requires_visible_whole_slot(void **state)
{
  const struct flash_area *fa;
  uint8_t *raw = mock_boot_flash_data();
  uint32_t last;

  (void)state;
  assert_int_equal(flash_area_open(0, &fa), 0);
  assert_int_equal(flash_area_erase(fa, 0, fa->fa_size), -1);

  bk7258_bl2_set_slot_limit(BK7258_BL2_SLOT_PRIMARY);
  assert_int_equal(flash_area_erase(fa, 1u, fa->fa_size), -1);
  assert_int_equal(flash_area_erase(fa, 0, fa->fa_size - 1u), -1);

  last = BK7258_CP_RAW_PHYSICAL_START + BK7258_CP_RAW_PHYSICAL_SIZE - 1u;
  raw[BK7258_CP_RAW_PHYSICAL_START] = 0x00u;
  raw[last] = 0x00u;
  assert_int_equal(flash_area_erase(fa, 0, fa->fa_size), 0);
  assert_int_equal(raw[BK7258_CP_RAW_PHYSICAL_START], 0xffu);
  assert_int_equal(raw[last], 0xffu);
  assert_int_equal(mock_boot_flash_erase_calls(), 1u);
}

static void test_align_and_erased_val(void **state)
{
  const struct flash_area *fa;

  (void)state;
  assert_int_equal(flash_area_open(0, &fa), 0);
  assert_int_equal(flash_area_align(fa), 1);
  assert_int_equal(flash_area_erased_val(fa), 0xff);
}

static void test_get_sectors_ok(void **state)
{
  const struct flash_area *fa;
  struct flash_sector sectors[2];
  uint32_t count;
  int id;

  (void)state;
  for (id = 0; id < 4; id++)
    {
      assert_int_equal(flash_area_open((uint8_t)id, &fa), 0);
      count = 2;
      assert_int_equal(flash_area_get_sectors(id, &count, sectors), 0);
      assert_int_equal(count, 1);
      assert_int_equal(sectors[0].fs_off, 0);
      assert_int_equal(sectors[0].fs_size, fa->fa_size);
    }
}

static void test_get_sectors_bad_params(void **state)
{
  struct flash_sector sectors[2];
  uint32_t count;

  (void)state;
  count = 1;
  assert_int_equal(flash_area_get_sectors(0, NULL, sectors), -1);
  assert_int_equal(flash_area_get_sectors(0, &count, NULL), -1);
  count = 0;
  assert_int_equal(flash_area_get_sectors(0, &count, sectors), -1);
  count = 1;
  assert_int_equal(flash_area_get_sectors(99, &count, sectors), -1);
}

static void test_id_from_multi_image_slot(void **state)
{
  (void)state;
  assert_int_equal(flash_area_id_from_multi_image_slot(0, 0), 0);
  assert_int_equal(flash_area_id_from_multi_image_slot(0, 1), 1);
  assert_int_equal(flash_area_id_from_multi_image_slot(1, 0), 2);
  assert_int_equal(flash_area_id_from_multi_image_slot(1, 1), 3);
  assert_int_equal(flash_area_id_from_multi_image_slot(0, 2), -1);
  assert_int_equal(flash_area_id_from_multi_image_slot(2, 0), -1);
  assert_int_equal(flash_area_id_from_multi_image_slot(2, 1), -1);
}

static void test_id_from_image_slot(void **state)
{
  (void)state;
  assert_int_equal(flash_area_id_from_image_slot(0), 0);
  assert_int_equal(flash_area_id_from_image_slot(1), 1);
  assert_int_equal(flash_area_id_from_image_slot(2), -1);
}

static void test_id_to_multi_image_slot(void **state)
{
  (void)state;
  assert_int_equal(flash_area_id_to_multi_image_slot(0, 0), 0);
  assert_int_equal(flash_area_id_to_multi_image_slot(0, 1), 1);
  assert_int_equal(flash_area_id_to_multi_image_slot(0, 2), -1);
  assert_int_equal(flash_area_id_to_multi_image_slot(0, 3), -1);
  assert_int_equal(flash_area_id_to_multi_image_slot(1, 2), 0);
  assert_int_equal(flash_area_id_to_multi_image_slot(1, 3), 1);
  assert_int_equal(flash_area_id_to_multi_image_slot(1, 0), -1);
  assert_int_equal(flash_area_id_to_multi_image_slot(2, 0), -1);
}

static void test_id_from_image_offset(void **state)
{
  const struct flash_area *fa;
  int id;

  (void)state;
  for (id = 0; id < 4; id++)
    {
      assert_int_equal(flash_area_open((uint8_t)id, &fa), 0);
      assert_int_equal(flash_area_id_from_image_offset(fa->fa_off), id);
    }
  assert_int_equal(flash_area_id_from_image_offset(0x12345678u), -1);
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test_setup_teardown(test_open_all_slots_and_close, setup, teardown),
    cmocka_unit_test_setup_teardown(test_open_slot_geometry, setup, teardown),
    cmocka_unit_test_setup_teardown(test_open_invalid_id, setup, teardown),
    cmocka_unit_test_setup_teardown(test_open_null_fa, setup, teardown),
    cmocka_unit_test_setup_teardown(test_read_bad_params, setup, teardown),
    cmocka_unit_test_setup_teardown(test_read_roundtrip_primary_cp, setup, teardown),
    cmocka_unit_test_setup_teardown(test_read_secondary_ap, setup, teardown),
    cmocka_unit_test_setup_teardown(test_read_erased_returns_ff, setup, teardown),
    cmocka_unit_test_setup_teardown(test_read_at_tail, setup, teardown),
    cmocka_unit_test_setup_teardown(test_read_feeds_watchdog, setup, teardown),
    cmocka_unit_test_setup_teardown(test_primary_only_hides_secondary, setup, teardown),
    cmocka_unit_test_setup_teardown(test_secondary_only_hides_primary, setup, teardown),
    cmocka_unit_test_setup_teardown(test_slot_limit_both_restores_visibility, setup, teardown),
    cmocka_unit_test_setup_teardown(test_slot_limit_invalid_resets_to_both, setup, teardown),
    cmocka_unit_test_setup_teardown(test_primary_only_alias, setup, teardown),
    cmocka_unit_test_setup_teardown(test_write_rejects_outside_owned_copy_done,
                                    setup, teardown),
    cmocka_unit_test_setup_teardown(test_write_copy_done_encodes_one_crc_group,
                                    setup, teardown),
    cmocka_unit_test_setup_teardown(test_erase_requires_visible_whole_slot,
                                    setup, teardown),
    cmocka_unit_test_setup_teardown(test_align_and_erased_val, setup, teardown),
    cmocka_unit_test_setup_teardown(test_get_sectors_ok, setup, teardown),
    cmocka_unit_test_setup_teardown(test_get_sectors_bad_params, setup, teardown),
    cmocka_unit_test_setup_teardown(test_id_from_multi_image_slot, setup, teardown),
    cmocka_unit_test_setup_teardown(test_id_from_image_slot, setup, teardown),
    cmocka_unit_test_setup_teardown(test_id_to_multi_image_slot, setup, teardown),
    cmocka_unit_test_setup_teardown(test_id_from_image_offset, setup, teardown),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
