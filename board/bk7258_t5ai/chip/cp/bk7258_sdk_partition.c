/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/cp/
 * bk7258_sdk_partition.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Project-owned v3.1.1.9 Flash partition wrapper.
 *
 * The official SDK archives embed the partition table used when those
 * archives were built.  Linker --wrap keeps the archives immutable while
 * routing every public partition operation through the repository-generated
 * CSV layout.  The raw Flash driver remains the official SDK implementation.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <arch/chip/bk7258_partition_layout.h>

#include <driver/flash.h>

#ifdef CONFIG_BK7258_FLASH_MTD
#  include "bk7258_flash_guard.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_FLASH_API_MAGIC_CODE       0x12345678u
#define BK7258_SDK_PARTITION_OWNER_FLASH  0u
#define BK7258_SDK_PARTITION_READ         (1u << 0)
#define BK7258_SDK_PARTITION_WRITE        (1u << 1)
#define BK7258_SDK_PARTITION_EXECUTE      (1u << 2)
#define BK7258_SDK_READ_ALIGNMENT         32u

#define BK7258_SDK_PARTITION_ENTRY(id, name, start, length, execute, read, \
                                   write)                                  \
  [id] =                                                                  \
    {                                                                     \
      BK7258_SDK_PARTITION_OWNER_FLASH, name, start, length,               \
      ((execute) ? BK7258_SDK_PARTITION_EXECUTE : 0u) |                    \
      ((read) ? BK7258_SDK_PARTITION_READ : 0u) |                          \
      ((write) ? BK7258_SDK_PARTITION_WRITE : 0u)                          \
    },

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Binary-compatible with SDK bk_logic_partition_t.  The exported SDK bundle
 * intentionally omits partitions_gen.h, so including flash_partition.h is
 * not possible from the team overlay.
 */

struct bk7258_sdk_partition_s
{
  uint32_t    owner;
  const char *description;
  uint32_t    start;
  uint32_t    length;
  uint32_t    options;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct bk7258_sdk_partition_s
  g_bk7258_sdk_partitions[BK7258_SDK_PARTITIONS_TABLE_SIZE] =
{
  BK7258_SDK_PARTITION_FOREACH(BK7258_SDK_PARTITION_ENTRY)
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bk7258_sdk_partition_valid(uint32_t partition)
{
  return partition < BK7258_SDK_PARTITIONS_TABLE_SIZE &&
         (BK7258_SDK_PARTITION_VALID_MASK & (1u << partition)) != 0;
}

static const struct bk7258_sdk_partition_s *
bk7258_sdk_partition_info(uint32_t partition)
{
  if (!bk7258_sdk_partition_valid(partition))
    {
      return NULL;
    }

  return &g_bk7258_sdk_partitions[partition];
}

static bool bk7258_sdk_partition_range(
  const struct bk7258_sdk_partition_s *partition, uint32_t offset,
  uint32_t size)
{
  return offset < partition->length && size <= partition->length - offset;
}

static const struct bk7258_sdk_partition_s *
bk7258_sdk_partition_info_by_addr(uint32_t addr)
{
  uint32_t partition;

  for (partition = 0; partition < BK7258_SDK_PARTITIONS_TABLE_SIZE;
       partition++)
    {
      const struct bk7258_sdk_partition_s *info;

      info = bk7258_sdk_partition_info(partition);
      if (info != NULL && addr >= info->start && addr - info->start < info->length)
        {
          return info;
        }
    }

  return NULL;
}

static bool bk7258_sdk_partition_guarded(
  const struct bk7258_sdk_partition_s *partition)
{
  return partition ==
           &g_bk7258_sdk_partitions[BK7258_ROLE_SLOT_B_PAIR_SDK_ID] ||
         partition ==
           &g_bk7258_sdk_partitions[BK7258_ROLE_OTA_METADATA_PRIMARY_SDK_ID] ||
         partition ==
           &g_bk7258_sdk_partitions[BK7258_ROLE_OTA_METADATA_MIRROR_SDK_ID] ||
         partition ==
           &g_bk7258_sdk_partitions[BK7258_ROLE_OTA_MANIFEST_A_SDK_ID] ||
         partition ==
           &g_bk7258_sdk_partitions[BK7258_ROLE_OTA_MANIFEST_B_SDK_ID] ||
         partition ==
           &g_bk7258_sdk_partitions[BK7258_ROLE_OTA_AUTH_POLICY_SDK_ID] ||
         partition ==
           &g_bk7258_sdk_partitions[BK7258_ROLE_LITTLEFS_SDK_ID];
}

static bk_err_t bk7258_sdk_partition_write_allowed(
  const struct bk7258_sdk_partition_s *partition)
{
  if ((partition->options & BK7258_SDK_PARTITION_WRITE) == 0 ||
      (partition->options & BK7258_SDK_PARTITION_EXECUTE) != 0 ||
      bk7258_sdk_partition_guarded(partition))
    {
      return BK_FAIL;
    }

  return BK_OK;
}

static bk_err_t bk7258_sdk_partition_write_by_addr_allowed(uint32_t addr,
                                                            uint32_t size)
{
  const struct bk7258_sdk_partition_s *info;
  uint32_t offset;

  info = bk7258_sdk_partition_info_by_addr(addr);
  if (info == NULL)
    {
      return BK_FAIL;
    }

  offset = addr - info->start;
  if (!bk7258_sdk_partition_range(info, offset, size))
    {
      return BK_ERR_FLASH_ADDR_OUT_OF_RANGE;
    }

  /* The arming marker is deliberately outside every normal or guarded SDK
   * write path.  A future reviewed migration primitive must own its one-time
   * raw-Flash protocol explicitly rather than inheriting an OTA/data guard.
   */

  if (info ==
      &g_bk7258_sdk_partitions[BK7258_ROLE_OTA_AUTH_POLICY_SDK_ID])
    {
      return BK_FAIL;
    }

#ifdef CONFIG_BK7258_FLASH_MTD
  if (bk7258_flash_guard_write_authorized(addr, size))
    {
      return BK_OK;
    }
#endif

  return bk7258_sdk_partition_write_allowed(info);
}

/****************************************************************************
 * SDK Linker Wrappers
 ****************************************************************************/

struct bk7258_sdk_partition_s *
__wrap_bk_flash_partition_get_info(uint32_t partition)
{
  return (struct bk7258_sdk_partition_s *)
    bk7258_sdk_partition_info(partition);
}

bk_err_t __wrap_bk_flash_partition_read(uint32_t partition,
                                        uint8_t *buffer, uint32_t offset,
                                        uint32_t length)
{
  const struct bk7258_sdk_partition_s *info;
  uint32_t aligned_offset;
  uint32_t aligned_end;
  uint32_t aligned_length;
  uint8_t *aligned_buffer;
  bk_err_t ret;

  if (buffer == NULL)
    {
      return BK_FAIL;
    }

  info = bk7258_sdk_partition_info(partition);
  if (info == NULL)
    {
      return BK_ERR_FLASH_PARTITION_NOT_FOUND;
    }

  if ((info->options & BK7258_SDK_PARTITION_READ) == 0 ||
      !bk7258_sdk_partition_range(info, offset, length))
    {
      return BK_ERR_FLASH_ADDR_OUT_OF_RANGE;
    }

  if (length == 0)
    {
      return BK_OK;
    }

  aligned_offset = offset & ~(BK7258_SDK_READ_ALIGNMENT - 1u);
  aligned_end = (offset + length + BK7258_SDK_READ_ALIGNMENT - 1u) &
                ~(BK7258_SDK_READ_ALIGNMENT - 1u);
  aligned_length = aligned_end - aligned_offset;
  aligned_buffer = (uint8_t *)malloc(aligned_length);
  if (aligned_buffer == NULL)
    {
      return BK_ERR_NO_MEM;
    }

  ret = bk_flash_read_bytes(info->start + aligned_offset, aligned_buffer,
                            aligned_length);
  if (ret == BK_OK)
    {
      uint32_t index;

      for (index = 0; index < length; index++)
        {
          buffer[index] = aligned_buffer[offset - aligned_offset + index];
        }
    }

  free(aligned_buffer);
  return ret;
}

bk_err_t __wrap_bk_flash_partition_write(uint32_t partition,
                                         const uint8_t *buffer,
                                         uint32_t offset, uint32_t length)
{
  const struct bk7258_sdk_partition_s *info;
  bk_err_t ret;

  if (buffer == NULL)
    {
      return BK_FAIL;
    }

  info = bk7258_sdk_partition_info(partition);
  if (info == NULL)
    {
      return BK_ERR_FLASH_PARTITION_NOT_FOUND;
    }

  if (!bk7258_sdk_partition_range(info, offset, length))
    {
      return BK_ERR_FLASH_ADDR_OUT_OF_RANGE;
    }

  if (length == 0)
    {
      return BK_OK;
    }

  ret = bk7258_sdk_partition_write_by_addr_allowed(info->start + offset,
                                                    length);
  if (ret != BK_OK)
    {
      return ret;
    }

  return bk_flash_write_bytes(info->start + offset, buffer, length);
}

bk_err_t __wrap_bk_flash_partition_erase(uint32_t partition,
                                         uint32_t offset, uint32_t size)
{
  const struct bk7258_sdk_partition_s *info;
  uint32_t first_sector;
  uint32_t last_sector;
  uint32_t sector;
  bk_err_t ret;

  info = bk7258_sdk_partition_info(partition);
  if (info == NULL)
    {
      return BK_ERR_FLASH_PARTITION_NOT_FOUND;
    }

  if (!bk7258_sdk_partition_range(info, offset, size))
    {
      return BK_ERR_FLASH_ADDR_OUT_OF_RANGE;
    }

  if (size == 0)
    {
      return BK_OK;
    }

  ret = bk7258_sdk_partition_write_by_addr_allowed(info->start + offset,
                                                    size);
  if (ret != BK_OK)
    {
      return ret;
    }

  first_sector = offset / BK7258_FLASH_ERASE_SIZE;
  last_sector = (offset + size - 1u) / BK7258_FLASH_ERASE_SIZE;
  for (sector = first_sector; sector <= last_sector; sector++)
    {
      ret = bk_flash_erase_sector(info->start +
                                  sector * BK7258_FLASH_ERASE_SIZE);
      if (ret != BK_OK)
        {
          return ret;
        }
    }

  return BK_OK;
}

bk_err_t __wrap_bk_flash_partition_write_perm_check_by_addr(
  uint32_t addr, uint32_t size, uint32_t magic_code)
{
  if (magic_code != BK7258_FLASH_API_MAGIC_CODE)
    {
      return BK_FAIL;
    }

  return bk7258_sdk_partition_write_by_addr_allowed(addr, size);
}
