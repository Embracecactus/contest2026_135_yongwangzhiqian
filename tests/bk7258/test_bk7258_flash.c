/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * Host contract tests for the chip-owned BK7258 raw Flash service.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <arch/chip/bk7258_flash.h>
#include <arch/chip/bk7258_image_layout.h>
#include <arch/chip/bk7258_storage_guard.h>

#include <driver/flash.h>
#include <driver/flash_partition.h>

#include <nuttx/mtd/mtd.h>
#include <nuttx/mutex.h>

#include "bk7258_flash_mtd.h"

#define TEST_FLASH_SIZE 0x00800000u

static uint32_t g_flash_id;
static uint32_t g_flash_size;
static int g_driver_result;
static int g_read_result;
static int g_erase_result;
static int g_write_result;
static int g_update_result;
static int g_protect_none_result;
static int g_protect_restore_result;
static int g_driver_calls;
static int g_size_calls;
static int g_read_calls;
static int g_erase_calls;
static int g_write_calls;
static int g_update_calls;
static int g_partition_info_calls;
static int g_protect_none_calls;
static int g_protect_restore_calls;
static int g_guard_lock_result;
static int g_guard_lock_calls;
static int g_guard_unlock_calls;
static enum bk7258_storage_guard_e g_guard_owner;
static bool g_guard_write_access;
static bool g_partition_present;
static bk_logic_partition_t g_partition_info;

static uint8_t g_mtd_buffer[BK7258_FLASH_ERASE_SIZE];

int bk7258_storage_guard_lock(enum bk7258_storage_guard_e owner,
                              bool write_access, uint32_t timeout_ms)
{
  assert(timeout_ms == 0u);
  g_guard_lock_calls++;
  g_guard_owner = owner;
  g_guard_write_access = write_access;
  return g_guard_lock_result;
}

void bk7258_storage_guard_unlock(void)
{
  g_guard_unlock_calls++;
}

bk_err_t bk_flash_driver_init(void)
{
  g_driver_calls++;
  return g_driver_result;
}

uint32_t bk_flash_get_id(void)
{
  return g_flash_id;
}

uint32_t bk_flash_get_current_total_size(void)
{
  g_size_calls++;
  return g_flash_size;
}

bk_err_t bk_flash_read_bytes(uint32_t address, uint8_t *buffer,
                             uint32_t size)
{
  assert(address < TEST_FLASH_SIZE);
  assert(buffer != NULL && size != 0u);
  g_read_calls++;
  return g_read_result;
}

bk_err_t bk_flash_erase_sector(uint32_t address)
{
  assert(address < TEST_FLASH_SIZE);
  g_erase_calls++;
  return g_erase_result;
}

bk_err_t bk_flash_write_bytes(uint32_t address,
                              const uint8_t *buffer, uint32_t size)
{
  assert(address < TEST_FLASH_SIZE);
  assert(buffer != NULL && size != 0u);
  g_write_calls++;
  return g_write_result;
}

bk_err_t bk_flash_set_protect_type(flash_protect_type_t type)
{
  if (type == FLASH_PROTECT_NONE)
    {
      g_protect_none_calls++;
      return g_protect_none_result;
    }

  assert(type == FLASH_UNPROTECT_LAST_BLOCK);
  g_protect_restore_calls++;
  return g_protect_restore_result;
}

bk_err_t bk_spec_flash_write_bytes(bk_partition_t partition,
                                    const uint8_t *buffer,
                                    uint32_t size, uint32_t offset)
{
  assert(partition == 7u);
  assert(offset == 4u);
  assert(buffer != NULL && size == 8u);
  g_update_calls++;
  return g_update_result;
}

bk_logic_partition_t *bk_flash_partition_get_info(
  bk_partition_t partition)
{
  g_partition_info_calls++;
  return partition == 7u && g_partition_present ? &g_partition_info : NULL;
}

static void reset_mutation_results(void)
{
  g_erase_result = BK_OK;
  g_write_result = BK_OK;
  g_update_result = BK_OK;
  g_protect_none_result = BK_OK;
  g_protect_restore_result = BK_OK;
  g_erase_calls = 0;
  g_write_calls = 0;
  g_update_calls = 0;
  g_protect_none_calls = 0;
  g_protect_restore_calls = 0;
}

static void test_mtd_block_ranges(void)
{
  FAR struct mtd_dev_s *mtd;
  struct mtd_geometry_s geometry;
  int reads;
  int writes;
  int erases;
  int locks;
  int unlocks;

  mtd = bk7258_flash_mtd_initialize();
  assert(mtd != NULL);
  assert(mtd->ioctl(mtd, MTDIOC_GEOMETRY,
                    (unsigned long)(uintptr_t)&geometry) == 0);
  assert(geometry.blocksize == BK7258_FLASH_ERASE_SIZE);
  assert(geometry.neraseblocks > 1u);

  g_guard_lock_result = 0;
  locks = g_guard_lock_calls;
  unlocks = g_guard_unlock_calls;
  reads = g_read_calls;
  assert(mtd->bread(mtd, (off_t)geometry.neraseblocks - 1, 1u,
                    g_mtd_buffer) == 1);
  assert(g_read_calls == reads + 1);
  assert(g_guard_lock_calls == locks + 1 &&
         g_guard_unlock_calls == unlocks + 1);
  assert(g_guard_owner == BK7258_STORAGE_GUARD_DATA &&
         !g_guard_write_access);

  /* This pair wraps a host-size_t addition in the old check.  It must fail
   * before lock acquisition or raw Flash access.
   */
#if SIZE_MAX > INT64_MAX
  locks = g_guard_lock_calls;
  unlocks = g_guard_unlock_calls;
  reads = g_read_calls;
  erases = g_erase_calls;
  assert(mtd->bread(mtd, (off_t)INT64_MAX,
                    SIZE_MAX - (size_t)INT64_MAX + 1u,
                    g_mtd_buffer) == -EINVAL);
  assert(mtd->erase(mtd, (off_t)INT64_MAX,
                    SIZE_MAX - (size_t)INT64_MAX + 1u) == -EINVAL);
  assert(g_guard_lock_calls == locks && g_guard_unlock_calls == unlocks &&
         g_read_calls == reads && g_erase_calls == erases);
#endif

  /* nblocks cannot be represented as a uint32_t byte count.  The helper
   * rejects it before narrowing or entering the write transaction.
   */
  locks = g_guard_lock_calls;
  unlocks = g_guard_unlock_calls;
  writes = g_write_calls;
  assert(mtd->bwrite(mtd, 0,
                     (size_t)UINT32_MAX / BK7258_FLASH_ERASE_SIZE + 1u,
                     g_mtd_buffer) == -EINVAL);
  assert(g_guard_lock_calls == locks && g_guard_unlock_calls == unlocks &&
         g_write_calls == writes);
}

int main(void)
{
  static const uint32_t supported_ids[] =
  {
    0x00c86517u,
    0x00c84017u,
    0x000b4017u,
    0x00cd6017u,
  };
  uint8_t buffer[8] = {0};
  struct bk7258_flash_partition_info_s partition;
  int calls;
  unsigned int id;

  g_driver_result = BK_OK;
  for (id = 0; id < sizeof(supported_ids) / sizeof(supported_ids[0]); id++)
    {
      g_flash_id = supported_ids[id];
      g_flash_size = 0u;
      calls = g_size_calls;
      assert(bk7258_flash_initialize() == -ENODEV);
      assert(g_size_calls == calls + 1);
    }

  g_flash_id = 0x00123456u;
  g_flash_size = TEST_FLASH_SIZE;
  calls = g_size_calls;
  assert(bk7258_flash_initialize() == -ENODEV);
  assert(g_size_calls == calls);

  g_flash_id = supported_ids[0];
  assert(bk7258_flash_initialize() == 0);
  calls = g_driver_calls;
  g_driver_result = BK_FAIL;
  assert(bk7258_flash_initialize() == 0);
  assert(g_driver_calls == calls);

  calls = g_partition_info_calls;
  assert(bk7258_flash_partition_get_info(7u, NULL) == -EINVAL);
  assert(g_partition_info_calls == calls);
  assert(bk7258_flash_partition_get_info(7u, &partition) == -ENOENT);
  g_partition_present = true;
  g_partition_info.partition_start_addr = 0x1000u;
  g_partition_info.partition_length = 0u;
  assert(bk7258_flash_partition_get_info(7u, &partition) == -EINVAL);
  g_partition_info.partition_start_addr = UINT32_MAX - 3u;
  g_partition_info.partition_length = 4u;
  assert(bk7258_flash_partition_get_info(7u, &partition) == -EINVAL);
  g_partition_info.partition_start_addr = 0x1000u;
  g_partition_info.partition_length = 0x2000u;
  assert(bk7258_flash_partition_get_info(7u, &partition) == 0);
  assert(partition.start == 0x1000u && partition.size == 0x2000u);

  assert(bk7258_flash_read(0u, NULL, sizeof(buffer)) == -EINVAL);
  assert(bk7258_flash_read(0u, buffer, 0u) == -EINVAL);
  assert(bk7258_flash_read(TEST_FLASH_SIZE, buffer, 1u) == -EINVAL);
  g_read_result = BK_FAIL;
  assert(bk7258_flash_read(0u, buffer, sizeof(buffer)) == -EIO);
  g_read_result = BK_OK;
  assert(bk7258_flash_read(TEST_FLASH_SIZE - sizeof(buffer), buffer,
                           sizeof(buffer)) == 0);
  assert(g_read_calls == 2);
  mock_mutex_fail_next(1);
  assert(bk7258_flash_read(0u, buffer, sizeof(buffer)) == -EAGAIN);
  assert(g_read_calls == 2);

  reset_mutation_results();
  g_protect_none_result = BK_FAIL;
  assert(bk7258_flash_write(0u, buffer, sizeof(buffer)) == -EIO);
  assert(g_write_calls == 0 && g_protect_none_calls == 1 &&
         g_protect_restore_calls == 1);

  reset_mutation_results();
  g_write_result = BK_FAIL;
  assert(bk7258_flash_write(0u, buffer, sizeof(buffer)) == -EIO);
  assert(g_write_calls == 1 && g_protect_restore_calls == 1);

  reset_mutation_results();
  g_protect_restore_result = BK_FAIL;
  assert(bk7258_flash_write(0u, buffer, sizeof(buffer)) == -EIO);
  assert(g_write_calls == 1 && g_protect_restore_calls == 1);

  reset_mutation_results();
  assert(bk7258_flash_write(0u, buffer, sizeof(buffer)) == 0);
  assert(g_write_calls == 1 && g_protect_none_calls == 1 &&
         g_protect_restore_calls == 1);

  reset_mutation_results();
  assert(bk7258_flash_erase_sector(TEST_FLASH_SIZE) == -EINVAL);
  assert(bk7258_flash_erase_sector(1u) == -EINVAL);
  assert(bk7258_flash_erase_sector(0x1000u) == 0);
  assert(g_erase_calls == 1 && g_protect_none_calls == 1 &&
         g_protect_restore_calls == 1);

  reset_mutation_results();
  assert(bk7258_flash_partition_update(7u, 4u, NULL,
                                       sizeof(buffer)) == -EINVAL);
  assert(bk7258_flash_partition_update(7u, 4u, buffer, 0u) == -EINVAL);
  assert(bk7258_flash_partition_update(7u, 4u, buffer,
                                       sizeof(buffer)) == 0);
  assert(g_update_calls == 1 && g_protect_none_calls == 1 &&
         g_protect_restore_calls == 1);

  reset_mutation_results();
  g_update_result = BK_FAIL;
  assert(bk7258_flash_partition_update(7u, 4u, buffer,
                                       sizeof(buffer)) == -EIO);
  assert(g_update_calls == 1 && g_protect_restore_calls == 1);

  test_mtd_block_ranges();

  puts("bk7258 raw Flash and MTD range tests: PASS");
  return 0;
}
