/****************************************************************************
 * chips/bk7258/cp/bk7258_radio_storage.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-owned SYS_RF/SYS_NET persistence.  The board supplies immutable
 * partition topology; this module validates, copies and operates on it.
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/mutex.h>

#include <arch/chip/bk7258_flash.h>
#include <arch/chip/bk7258_storage_guard.h>

#include "bk7258_radio_storage.h"

static mutex_t g_bk7258_radio_storage_lock = NXMUTEX_INITIALIZER;
static struct bk7258_radio_storage_config_s g_bk7258_radio_storage;
static bool g_bk7258_radio_storage_ready;

static bool bk7258_radio_partition_equal(
  FAR const struct bk7258_storage_partition_s *left,
  FAR const struct bk7258_storage_partition_s *right)
{
  return left->partition == right->partition &&
         left->start == right->start && left->size == right->size;
}

static int bk7258_radio_partition_validate(
  FAR const struct bk7258_storage_partition_s *partition)
{
  struct bk7258_flash_partition_info_s actual;
  int ret;

  if (partition->size == 0u ||
      partition->start > UINT32_MAX - partition->size)
    {
      return -EINVAL;
    }

  ret = bk7258_flash_partition_get_info(partition->partition, &actual);
  if (ret < 0)
    {
      return ret;
    }

  return actual.start == partition->start && actual.size == partition->size ?
         0 : -EINVAL;
}

static FAR const struct bk7258_storage_partition_s *
bk7258_radio_partition(enum bk7258_radio_store_e store)
{
  if (!__atomic_load_n(&g_bk7258_radio_storage_ready, __ATOMIC_ACQUIRE))
    {
      return NULL;
    }

  switch (store)
    {
      case BK7258_RADIO_STORE_BACKUP:
        return &g_bk7258_radio_storage.backup;

      case BK7258_RADIO_STORE_NETWORK:
        return &g_bk7258_radio_storage.network;

      default:
        return NULL;
    }
}

static int bk7258_radio_range(
  enum bk7258_radio_store_e store, uint32_t offset, uint32_t length,
  FAR const struct bk7258_storage_partition_s **partition)
{
  FAR const struct bk7258_storage_partition_s *current;

  if (partition == NULL || length == 0u)
    {
      return -EINVAL;
    }

  current = bk7258_radio_partition(store);
  if (current == NULL)
    {
      return store == BK7258_RADIO_STORE_BACKUP ||
             store == BK7258_RADIO_STORE_NETWORK ? -EAGAIN : -EINVAL;
    }

  if (offset > current->size || length > current->size - offset)
    {
      return -EINVAL;
    }

  *partition = current;
  return 0;
}

int bk7258_radio_storage_initialize(
  FAR const struct bk7258_radio_storage_config_s *config)
{
  int ret;

  if (config == NULL ||
      config->version != BK7258_RADIO_STORAGE_CONFIG_VERSION ||
      config->size < sizeof(*config) ||
      config->backup.partition == config->network.partition ||
      config->backup.start % BK7258_FLASH_SECTOR_SIZE != 0u ||
      config->backup.size % BK7258_FLASH_SECTOR_SIZE != 0u ||
      config->network.start % BK7258_FLASH_SECTOR_SIZE != 0u ||
      config->network.size % BK7258_FLASH_SECTOR_SIZE != 0u ||
      config->backup.size < BK7258_RADIO_BACKUP_MIN_SIZE ||
      config->network.size < BK7258_RADIO_NETWORK_MIN_SIZE)
    {
      return -EINVAL;
    }

  ret = bk7258_radio_partition_validate(&config->backup);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_radio_partition_validate(&config->network);
  if (ret < 0)
    {
      return ret;
    }

  if (config->backup.start < config->network.start + config->network.size &&
      config->network.start < config->backup.start + config->backup.size)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_radio_storage_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_radio_storage_ready)
    {
      ret = bk7258_radio_partition_equal(
              &g_bk7258_radio_storage.backup, &config->backup) &&
            bk7258_radio_partition_equal(
              &g_bk7258_radio_storage.network, &config->network) ?
            0 : -EALREADY;
    }
  else
    {
      g_bk7258_radio_storage = *config;
      __atomic_store_n(&g_bk7258_radio_storage_ready, true,
                       __ATOMIC_RELEASE);
      ret = 0;
    }

  nxmutex_unlock(&g_bk7258_radio_storage_lock);
  return ret;
}

int bk7258_radio_storage_read(enum bk7258_radio_store_e store,
                              uint32_t offset, FAR uint8_t *buffer,
                              uint32_t length)
{
  FAR const struct bk7258_storage_partition_s *partition;
  int ret;

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_radio_range(store, offset, length, &partition);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_RADIO,
                                  false, 0u);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_flash_read(partition->start + offset, buffer, length);
  bk7258_storage_guard_unlock();
  return ret;
}

int bk7258_radio_storage_write(enum bk7258_radio_store_e store,
                               uint32_t offset,
                               FAR const uint8_t *buffer,
                               uint32_t length)
{
  FAR const struct bk7258_storage_partition_s *partition;
  int ret;

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_radio_range(store, offset, length, &partition);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_RADIO,
                                  true, 0u);
  if (ret < 0)
    {
      return ret;
    }

  if (store == BK7258_RADIO_STORE_NETWORK)
    {
      /* Match SDK save_net_info(WIFI_MAC_ITEM): retain the remainder of the
       * SYS_NET sector across its erase/rewrite transaction.
       */

      ret = bk7258_flash_partition_update(partition->partition, offset,
                                           buffer, length);
    }
  else
    {
      ret = bk7258_flash_write(partition->start + offset, buffer, length);
    }

  bk7258_storage_guard_unlock();
  return ret;
}

#endif
