/****************************************************************************
 * chips/bk7258/include/bk7258_storage_config.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Versioned BK7258 storage topology and serialization configuration.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_STORAGE_CONFIG_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_STORAGE_CONFIG_H

#include <nuttx/compiler.h>

#include <stdint.h>

#include <arch/chip/bk7258_boot_slot.h>
#include <arch/chip/bk7258_ota.h>

#define BK7258_STORAGE_CONFIG_VERSION        2u
#define BK7258_RADIO_STORAGE_CONFIG_VERSION  1u
#define BK7258_OTA_LAYOUT_VERSION            1u
#define BK7258_OTA_SLOT_COUNT                2u
#define BK7258_OTA_IMAGE_COUNT               2u
#define BK7258_RESET_MARKER_ERASE_SIZE       BK7258_FLASH_SECTOR_SIZE

struct bk7258_ota_storage_image_s
{
  uint32_t raw_offset;
  uint32_t raw_size;
};

struct bk7258_ota_layout_s
{
  uint16_t version;
  uint16_t size;
  uint32_t erase_size;
  uint32_t crc_data_size;
  uint32_t crc_total_size;
  struct bk7258_boot_slot_map_s remap;
  struct bk7258_ota_storage_image_s
    slot[BK7258_OTA_SLOT_COUNT][BK7258_OTA_IMAGE_COUNT];
  uint32_t active_xip_start[BK7258_OTA_IMAGE_COUNT];
  uint32_t active_logical_size[BK7258_OTA_IMAGE_COUNT];
  uint8_t layout_sha256[BK7258_OTA_SHA256_SIZE];
};

struct bk7258_storage_partition_s
{
  uint32_t partition;
  uint32_t start;
  uint32_t size;
};

struct bk7258_storage_region_s
{
  uint32_t start;
  uint32_t size;
};

struct bk7258_radio_storage_config_s
{
  uint16_t version;
  uint16_t size;
  struct bk7258_storage_partition_s backup;
  struct bk7258_storage_partition_s network;
};

struct bk7258_storage_config_s
{
  uint16_t version;
  uint16_t size;
  FAR const struct bk7258_ota_layout_s *ota_layout;
  FAR const struct bk7258_radio_storage_config_s *radio_storage;
  FAR const struct bk7258_storage_region_s *data_storage;
  uint32_t reset_marker_address;
  uint32_t reset_marker_erase_size;
};

/* The selected board supplies one immutable, statically allocated config.
 * The chip validates each optional topology domain before publishing it to
 * its consumer.  Domain records contain data only; controller operations,
 * synchronization and SDK ABI calls remain chip-owned.
 */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_STORAGE_CONFIG_H */
