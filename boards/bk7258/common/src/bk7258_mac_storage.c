/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/
 * bk7258_mac_storage.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-owned mapping from the BK7258 Bluetooth/Wi-Fi MAC service to the
 * product partition contract.  Radio lifecycle and address derivation stay
 * chip-owned; only SYS_RF/SYS_NET placement and write policy live here.
 ****************************************************************************/

#include <nuttx/config.h>

#if !defined(CONFIG_BK7258_AP_CORE) && \
    (defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET))

#include <errno.h>
#include <stdint.h>

#include <arch/chip/bk7258_image_layout.h>
#include <arch/chip/bk7258_bt_ipc.h>
#include <arch/chip/bk7258_sdk_abi.h>

#include <common/bk_err.h>

#include "bk7258_internal.h"

static int bk7258_mac_store_resolve(enum bk7258_bt_mac_store_e store,
                                    uint32_t *partition,
                                    uint32_t *expected_start,
                                    uint32_t *expected_length)
{
  switch (store)
    {
      case BK7258_BT_MAC_STORE_BACKUP:
        *partition = BK7258_PARTITION_SYS_RF_INDEX;
        *expected_start = BK7258_PARTITION_SYS_RF_OFFSET;
        *expected_length = BK7258_PARTITION_SYS_RF_SIZE;
        return OK;

      case BK7258_BT_MAC_STORE_NETWORK:
        *partition = BK7258_PARTITION_SYS_NET_INDEX;
        *expected_start = BK7258_PARTITION_SYS_NET_OFFSET;
        *expected_length = BK7258_PARTITION_SYS_NET_SIZE;
        return OK;

      default:
        return -EINVAL;
    }
}

static int bk7258_mac_store_validate(enum bk7258_bt_mac_store_e store,
                                     uint32_t offset, uint32_t length,
                                     uint32_t *partition)
{
  struct bk7258_sdk_partition_s *info;
  uint32_t expected_start;
  uint32_t expected_length;
  int ret;

  ret = bk7258_mac_store_resolve(store, partition, &expected_start,
                                 &expected_length);
  if (ret < 0)
    {
      return ret;
    }

  info = bk_flash_partition_get_info(*partition);
  if (info == NULL || info->start != expected_start ||
      info->length != expected_length ||
      offset > info->length || length > info->length - offset)
    {
      return -EINVAL;
    }

  return OK;
}

static int bk7258_mac_store_read(enum bk7258_bt_mac_store_e store,
                                 uint32_t offset, uint8_t *buffer,
                                 uint32_t length)
{
  uint32_t partition;
  int ret;

  ret = bk7258_mac_store_validate(store, offset, length, &partition);
  if (ret < 0)
    {
      return ret;
    }

  return bk_flash_partition_read(partition, buffer, offset, length) == BK_OK ?
         OK : -EIO;
}

static int bk7258_mac_store_write(enum bk7258_bt_mac_store_e store,
                                  uint32_t offset, const uint8_t *buffer,
                                  uint32_t length)
{
  uint32_t partition;
  int ret;

  ret = bk7258_mac_store_validate(store, offset, length, &partition);
  if (ret < 0)
    {
      return ret;
    }

  if (store == BK7258_BT_MAC_STORE_NETWORK)
    {
      /* Match SDK save_net_info(WIFI_MAC_ITEM): retain the remainder of the
       * SYS_NET sector across its erase/rewrite transaction.
       */

      return bk_spec_flash_write_bytes(partition, buffer, length, offset) ==
             BK_OK ? OK : -EIO;
    }

  return bk_flash_partition_write(partition, buffer, offset, length) == BK_OK ?
         OK : -EIO;
}

static const struct bk7258_bt_mac_storage_ops_s g_bk7258_mac_storage_ops =
{
  .read = bk7258_mac_store_read,
  .write = bk7258_mac_store_write,
};

int bk7258_mac_storage_initialize(void)
{
  return bk7258_bt_mac_storage_register(&g_bk7258_mac_storage_ops);
}

#endif
