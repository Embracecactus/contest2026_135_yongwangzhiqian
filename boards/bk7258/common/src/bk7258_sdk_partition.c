/* SPDX-License-Identifier: Apache-2.0 */

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <bk7258_partitions.h>
#include <driver/flash_partition.h>
#include <driver/flash_types.h>

#include <arch/chip/bk7258_flash.h>

#if defined(CONFIG_BK7258_FLASH_MTD) || defined(CONFIG_BK7258_OTA) || \
    defined(CONFIG_BK7258_WDT_PRETIMEOUT_PANIC) || \
    defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
#  include <arch/chip/bk7258_storage_guard.h>
#endif

#define BK7258_OPTIONS(execute, read, write) \
  ((execute ? PAR_OPT_EXECUTE_EN : PAR_OPT_EXECUTE_DIS) | \
   (read ? PAR_OPT_READ_EN : PAR_OPT_READ_DIS) | \
   (write ? PAR_OPT_WRITE_EN : PAR_OPT_WRITE_DIS))

#define BK7258_PARTITION_ROW(id, name, offset, size, execute, read, write) \
  [id] = {BK_FLASH_EMBEDDED, name, offset, size, \
          BK7258_OPTIONS(execute, read, write)},

static bk_logic_partition_t g_bk7258_partitions[BK7258_PARTITION_COUNT] =
{
  BK7258_PARTITION_FOREACH(BK7258_PARTITION_ROW)
};

static bool bk7258_partition_valid(bk_partition_t partition)
{
  return partition < BK7258_PARTITION_COUNT &&
         (BK7258_PARTITION_VALID_MASK & (1u << partition)) != 0u;
}

static bool bk7258_partition_range(const bk_logic_partition_t *partition,
                                   uint32_t offset, uint32_t size)
{
  return offset <= partition->partition_length &&
         size <= partition->partition_length - offset;
}

bk_logic_partition_t *__wrap_bk_flash_partition_get_info(
    bk_partition_t partition)
{
  return bk7258_partition_valid(partition) ?
         &g_bk7258_partitions[partition] : NULL;
}

bk_err_t __wrap_bk_flash_partition_read(bk_partition_t partition,
                                        uint8_t *buffer,
                                        uint32_t offset,
                                        uint32_t size)
{
  bk_logic_partition_t *info = __wrap_bk_flash_partition_get_info(partition);
  if (info == NULL)
    {
      return BK_ERR_FLASH_PARTITION_NOT_FOUND;
    }

  if (buffer == NULL || !bk7258_partition_range(info, offset, size) ||
      (info->partition_options & PAR_OPT_READ_EN) == 0u)
    {
      return BK_ERR_FLASH_ADDR_OUT_OF_RANGE;
    }

  if (size == 0u)
    {
      return BK_OK;
    }

  return bk7258_flash_read(info->partition_start_addr + offset,
                           buffer, size) == 0 ? BK_OK : BK_FAIL;
}

bk_err_t __wrap_bk_flash_partition_write(bk_partition_t partition,
                                         const uint8_t *buffer,
                                         uint32_t offset,
                                         uint32_t size)
{
  bk_logic_partition_t *info = __wrap_bk_flash_partition_get_info(partition);
  if (info == NULL)
    {
      return BK_ERR_FLASH_PARTITION_NOT_FOUND;
    }

  if (buffer == NULL || !bk7258_partition_range(info, offset, size) ||
      (info->partition_options & PAR_OPT_WRITE_EN) == 0u)
    {
      return BK_ERR_FLASH_ADDR_OUT_OF_RANGE;
    }

  if (size == 0u)
    {
      return BK_OK;
    }

  return bk7258_flash_write(info->partition_start_addr + offset,
                            buffer, size) == 0 ? BK_OK : BK_FAIL;
}

bk_err_t __wrap_bk_flash_partition_erase(bk_partition_t partition,
                                         uint32_t offset,
                                         uint32_t size)
{
  bk_logic_partition_t *info = __wrap_bk_flash_partition_get_info(partition);
  if (info == NULL)
    {
      return BK_ERR_FLASH_PARTITION_NOT_FOUND;
    }

  if (!bk7258_partition_range(info, offset, size) ||
      (info->partition_options & PAR_OPT_WRITE_EN) == 0u)
    {
      return BK_ERR_FLASH_ADDR_OUT_OF_RANGE;
    }

  if (size == 0u)
    {
      return BK_OK;
    }

  uint32_t first = offset / BK7258_FLASH_ERASE_SIZE;
  uint32_t last = (offset + size - 1u) / BK7258_FLASH_ERASE_SIZE;
  for (uint32_t sector = first; sector <= last; sector++)
    {
      int result = bk7258_flash_erase_sector(
        info->partition_start_addr + sector * BK7258_FLASH_ERASE_SIZE);
      if (result < 0)
        {
          return BK_FAIL;
        }
    }

  return BK_OK;
}

bk_err_t __wrap_bk_flash_partition_write_perm_check_by_addr(
    uint32_t address, uint32_t size, uint32_t magic_code)
{
  (void)magic_code;
#if defined(CONFIG_BK7258_FLASH_MTD) || defined(CONFIG_BK7258_OTA) || \
    defined(CONFIG_BK7258_WDT_PRETIMEOUT_PANIC) || \
    defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
  /* Generated policy remains authoritative.  Chip storage clients receive a
   * task-scoped capability for only their configured data/radio/OTA/marker
   * range; all other writes still follow the partition table's options. */
  if (bk7258_storage_guard_write_authorized(address, size))
    {
      return BK_OK;
    }
#endif
  for (uint32_t index = 0; index < BK7258_PARTITION_COUNT; index++)
    {
      bk_logic_partition_t *info = &g_bk7258_partitions[index];
      if (address >= info->partition_start_addr &&
          address - info->partition_start_addr <= info->partition_length &&
          size <= info->partition_length -
                  (address - info->partition_start_addr))
        {
          return (info->partition_options & PAR_OPT_WRITE_EN) != 0u ?
                 BK_OK : BK_FAIL;
        }
    }

  return BK_FAIL;
}
