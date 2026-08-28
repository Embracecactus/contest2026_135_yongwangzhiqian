/****************************************************************************
 * boards/bk7258/common/src/bk7258_storage_config.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-owned BK7258 storage topology and Flash serialization policy.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

#include <arch/chip/bk7258_storage_config.h>
#include <arch/chip/bk7258_image_layout.h>

#include "bk7258_internal.h"

_Static_assert(BK7258_FLASH_CRC_DATA_SIZE == BK7258_OTA_CRC_DATA_SIZE,
               "board and OTA engine CRC data sizes differ");
_Static_assert(BK7258_FLASH_CRC_TOTAL_SIZE == BK7258_OTA_CRC_TOTAL_SIZE,
               "board and OTA engine CRC packet sizes differ");
_Static_assert(BK7258_FLASH_ERASE_SIZE == BK7258_OTA_ERASE_SIZE,
               "board and OTA engine erase sizes differ");
_Static_assert(BK7258_RESET_MARKER_SIZE ==
               BK7258_RESET_MARKER_ERASE_SIZE,
               "reset marker must own exactly one erase sector");
_Static_assert(BK7258_RESET_MARKER_START % BK7258_RESET_MARKER_ERASE_SIZE ==
               0u, "reset marker sector must be erase-aligned");

static const struct bk7258_ota_layout_s g_bk7258_ota_layout =
{
  .version = BK7258_OTA_LAYOUT_VERSION,
  .size = sizeof(struct bk7258_ota_layout_s),
  .erase_size = BK7258_FLASH_ERASE_SIZE,
  .crc_data_size = BK7258_FLASH_CRC_DATA_SIZE,
  .crc_total_size = BK7258_FLASH_CRC_TOTAL_SIZE,
  .remap =
  {
    .version = BK7258_BOOT_SLOT_MAP_VERSION,
    .size = sizeof(struct bk7258_boot_slot_map_s),
    .secondary_begin = BK7258_AB_REMAP_BEGIN,
    .secondary_end = BK7258_AB_REMAP_END,
    .secondary_offset = BK7258_AB_REMAP_OFFSET,
  },
  .slot =
  {
    [BK7258_BOOT_SLOT_PRIMARY] =
    {
      [BK7258_OTA_IMAGE_CP] =
      {
        .raw_offset = BK7258_CP_RAW_PHYSICAL_START,
        .raw_size = BK7258_CP_RAW_PHYSICAL_SIZE,
      },
      [BK7258_OTA_IMAGE_AP] =
      {
        .raw_offset = BK7258_AP_RAW_PHYSICAL_START,
        .raw_size = BK7258_AP_RAW_PHYSICAL_SIZE,
      },
    },
    [BK7258_BOOT_SLOT_SECONDARY] =
    {
      [BK7258_OTA_IMAGE_CP] =
      {
        .raw_offset = BK7258_AB_SECONDARY_CP_RAW_START,
        .raw_size = BK7258_CP_RAW_PHYSICAL_SIZE,
      },
      [BK7258_OTA_IMAGE_AP] =
      {
        .raw_offset = BK7258_AB_SECONDARY_AP_RAW_START,
        .raw_size = BK7258_AP_RAW_PHYSICAL_SIZE,
      },
    },
  },
  .active_xip_start =
  {
    [BK7258_OTA_IMAGE_CP] = BK7258_ARTIFACT_CP_XIP_START,
    [BK7258_OTA_IMAGE_AP] = BK7258_ARTIFACT_AP_XIP_START,
  },
  .active_logical_size =
  {
    [BK7258_OTA_IMAGE_CP] = BK7258_ARTIFACT_CP_LOGICAL_SIZE,
    [BK7258_OTA_IMAGE_AP] = BK7258_ARTIFACT_AP_LOGICAL_SIZE,
  },
  .layout_sha256 = BK7258_LAYOUT_SHA256_BYTES,
};

#if defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
static const struct bk7258_radio_storage_config_s
  g_bk7258_radio_storage =
{
  .version = BK7258_RADIO_STORAGE_CONFIG_VERSION,
  .size = sizeof(struct bk7258_radio_storage_config_s),
  .backup =
  {
    .partition = BK7258_PARTITION_SYS_RF_INDEX,
    .start = BK7258_PARTITION_SYS_RF_OFFSET,
    .size = BK7258_PARTITION_SYS_RF_SIZE,
  },
  .network =
  {
    .partition = BK7258_PARTITION_SYS_NET_INDEX,
    .start = BK7258_PARTITION_SYS_NET_OFFSET,
    .size = BK7258_PARTITION_SYS_NET_SIZE,
  },
};
#endif

#ifdef BK7258_STORAGE_TOPOLOGY_ONCHIP_PERSISTENT
static const struct bk7258_storage_region_s g_bk7258_data_storage =
{
  .start = BK7258_DATA_RAW_PHYSICAL_OFFSET,
  .size = BK7258_DATA_RAW_PHYSICAL_SIZE,
};
#endif

const struct bk7258_storage_config_s g_bk7258_board_storage_config =
{
  .version = BK7258_STORAGE_CONFIG_VERSION,
  .size = sizeof(struct bk7258_storage_config_s),
  .ota_layout = &g_bk7258_ota_layout,
#if defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
  .radio_storage = &g_bk7258_radio_storage,
#endif
#ifdef BK7258_STORAGE_TOPOLOGY_ONCHIP_PERSISTENT
  .data_storage = &g_bk7258_data_storage,
#endif
  .reset_marker_address = BK7258_RESET_MARKER_START,
  .reset_marker_erase_size = BK7258_RESET_MARKER_SIZE,
};
