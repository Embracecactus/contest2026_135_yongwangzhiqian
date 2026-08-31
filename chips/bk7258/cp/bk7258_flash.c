/****************************************************************************
 * chips/bk7258/cp/bk7258_flash.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <debug.h>

#include <nuttx/mutex.h>

#include <arch/chip/bk7258_flash.h>

#include <bk7258_partitions.h>
#include <driver/flash.h>
#include <driver/flash_partition.h>

#include "bk7258_sdk_partition.h"

#define BK7258_FLASH_ID_GD25WQ64E 0x00c86517u
#define BK7258_FLASH_ID_GD25Q64E  0x00c84017u
#define BK7258_FLASH_ID_W25Q64    0x000b4017u
#define BK7258_FLASH_ID_TH25Q64   0x00cd6017u

static mutex_t g_bk7258_flash_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_flash_ready;
static uint32_t g_bk7258_flash_size;

struct bk7258_layout_partition_s
{
  uint32_t start;
  uint32_t size;
};

#define BK7258_LAYOUT_PARTITION_ROW(id, name, offset, length, execute, read, \
                                    write) \
  [id] = {offset, length},

static const struct bk7258_layout_partition_s
  g_bk7258_layout_partitions[BK7258_PARTITION_COUNT] =
{
  BK7258_PARTITION_FOREACH(BK7258_LAYOUT_PARTITION_ROW)
};

static bool bk7258_layout_partition_valid(uint32_t partition)
{
  return partition < BK7258_PARTITION_COUNT &&
         (BK7258_PARTITION_VALID_MASK & (1u << partition)) != 0u;
}

static bool bk7258_flash_id_supported(uint32_t id)
{
  id &= 0x00ffffffu;
  return id == BK7258_FLASH_ID_GD25WQ64E ||
         id == BK7258_FLASH_ID_GD25Q64E ||
         id == BK7258_FLASH_ID_W25Q64 ||
         id == BK7258_FLASH_ID_TH25Q64;
}

static int bk7258_flash_initialize_locked(void)
{
  uint32_t size;

  if (g_bk7258_flash_ready)
    {
      return 0;
    }

  if (bk_flash_driver_init() != BK_OK ||
      !bk7258_flash_id_supported(bk_flash_get_id()))
    {
      return -ENODEV;
    }

  size = bk_flash_get_current_total_size();
  if (size == 0u)
    {
      return -ENODEV;
    }

  g_bk7258_flash_size = size;
  g_bk7258_flash_ready = true;
  return 0;
}

static bool bk7258_flash_range_valid(uint32_t address, size_t nbytes)
{
  if (nbytes == 0u)
    {
      return false;
    }

#if SIZE_MAX > UINT32_MAX
  if (nbytes > UINT32_MAX)
    {
      return false;
    }
#endif

  return address < g_bk7258_flash_size &&
         nbytes <= g_bk7258_flash_size - address;
}

static int bk7258_flash_mutation_finish(int ret)
{
  if (bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK) != BK_OK)
    {
      _err("bk7258: Flash protection restore failed\n");
      if (ret == 0)
        {
          ret = -EIO;
        }
    }

  return ret;
}

int bk7258_flash_initialize(void)
{
  int ret;

  ret = nxmutex_lock(&g_bk7258_flash_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_flash_initialize_locked();

  nxmutex_unlock(&g_bk7258_flash_lock);
  return ret;
}

int bk7258_flash_read(uint32_t address, FAR void *buffer, size_t nbytes)
{
  int ret;

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_flash_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_flash_initialize_locked();
  if (ret == 0 && !bk7258_flash_range_valid(address, nbytes))
    {
      ret = -EINVAL;
    }
  else if (ret == 0 &&
           bk_flash_read_bytes(address, buffer, (uint32_t)nbytes) != BK_OK)
    {
      ret = -EIO;
    }

  nxmutex_unlock(&g_bk7258_flash_lock);
  return ret;
}

int bk7258_flash_erase_sector(uint32_t address)
{
  int ret;

  ret = nxmutex_lock(&g_bk7258_flash_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_flash_initialize_locked();
  if (ret == 0 &&
      (address >= g_bk7258_flash_size ||
       address % BK7258_FLASH_SECTOR_SIZE != 0u))
    {
      ret = -EINVAL;
    }
  else if (ret == 0)
    {
      if (bk_flash_set_protect_type(FLASH_PROTECT_NONE) != BK_OK ||
          bk_flash_erase_sector(address) != BK_OK)
        {
          ret = -EIO;
        }

      ret = bk7258_flash_mutation_finish(ret);
    }

  nxmutex_unlock(&g_bk7258_flash_lock);
  return ret;
}

int bk7258_flash_write(uint32_t address, FAR const void *buffer,
                       size_t nbytes)
{
  int ret;

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_flash_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_flash_initialize_locked();
  if (ret == 0 && !bk7258_flash_range_valid(address, nbytes))
    {
      ret = -EINVAL;
    }
  else if (ret == 0)
    {
      if (bk_flash_set_protect_type(FLASH_PROTECT_NONE) != BK_OK ||
          bk_flash_write_bytes(address, buffer,
                               (uint32_t)nbytes) != BK_OK)
        {
          ret = -EIO;
        }

      ret = bk7258_flash_mutation_finish(ret);
    }

  nxmutex_unlock(&g_bk7258_flash_lock);
  return ret;
}

int bk7258_flash_partition_get_info(
  uint32_t partition, FAR struct bk7258_flash_partition_info_s *result)
{
  FAR const struct bk7258_layout_partition_s *info;

  if (result == NULL)
    {
      return -EINVAL;
    }

  if (!bk7258_layout_partition_valid(partition))
    {
      return -ENOENT;
    }

  info = &g_bk7258_layout_partitions[partition];
  if (info->size == 0u || info->start > UINT32_MAX - info->size)
    {
      return -EINVAL;
    }

  result->start = info->start;
  result->size = info->size;
  return 0;
}

int bk7258_flash_partition_update(uint32_t partition, uint32_t offset,
                                  FAR const void *buffer, size_t nbytes)
{
  uint32_t sdk_partition;
  int ret;

  if (buffer == NULL || nbytes == 0u ||
#if SIZE_MAX > UINT32_MAX
      nbytes > UINT32_MAX ||
#endif
      nbytes > UINT32_MAX - offset)
    {
      return -EINVAL;
    }

  ret = bk7258_sdk_partition_from_layout(partition, &sdk_partition);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&g_bk7258_flash_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_flash_initialize_locked();
  if (ret == 0)
    {
      if (bk_flash_set_protect_type(FLASH_PROTECT_NONE) != BK_OK ||
          bk_spec_flash_write_bytes((bk_partition_t)sdk_partition, buffer,
                                    (uint32_t)nbytes, offset) != BK_OK)
        {
          ret = -EIO;
        }

      ret = bk7258_flash_mutation_finish(ret);
    }

  nxmutex_unlock(&g_bk7258_flash_lock);
  return ret;
}
