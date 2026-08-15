/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/include/
 * bk7258_image_layout.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 logical-board image and product-partition contract.
 ****************************************************************************/

#ifndef __ARCH_BOARD_BK7258_IMAGE_LAYOUT_H
#define __ARCH_BOARD_BK7258_IMAGE_LAYOUT_H

#include <nuttx/config.h>

#include <assert.h>

#include <arch/chip/bk7258_memorymap.h>
#include <arch/board/bk7258_partition_layout.h>

/* CPU-visible executable addresses use FLASH_XIP_BASE plus logical offsets.
 * Executable images use the 34/32 CRC expansion; bk_flash_* data accesses use
 * raw physical offsets.  Never apply that conversion to LittleFS.
 */

#define BK7258_BOOT_FLASH_OFFSET         BK7258_ROLE_BOOT_LOGICAL_OFFSET
#define BK7258_BOOT_FLASH_SIZE           BK7258_ROLE_BOOT_LOGICAL_SIZE
#define BK7258_CP_FLASH_OFFSET           BK7258_ROLE_SLOT_A_CP_LOGICAL_OFFSET
#define BK7258_CP_FLASH_SIZE             BK7258_ROLE_SLOT_A_CP_LOGICAL_SIZE
#define BK7258_AP_FLASH_OFFSET           BK7258_ROLE_SLOT_A_AP_LOGICAL_OFFSET
#define BK7258_AP_FLASH_SIZE             BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE

#define BK7258_CP_FLASH_ADDR             \
  (BK7258_FLASH_XIP_BASE + BK7258_CP_FLASH_OFFSET)
#define BK7258_AP_FLASH_ADDR             \
  (BK7258_FLASH_XIP_BASE + BK7258_AP_FLASH_OFFSET)

#ifdef CONFIG_BK7258_MCUBOOT_IMAGE
#  define BK7258_AP_VECTOR_ADDR          (BK7258_AP_FLASH_ADDR + 0x200u)
#else
#  define BK7258_AP_VECTOR_ADDR          BK7258_AP_FLASH_ADDR
#endif

/* BL2 is a transient loader with a dedicated stored-image role and a
 * conservative 64-KiB SRAM execution window.
 */

#define BK7258_BL2_FLASH_ADDR            BK7258_ROLE_BL2_XIP_START
#define BK7258_BL2_EXEC_RAM_BASE         0x28020000u
#define BK7258_BL2_COPY_SIZE             0x00002000u
#define BK7258_BL2_EXEC_RAM_SIZE         0x00010000u
#define BK7258_BL2_EXEC_RAM_END          \
  (BK7258_BL2_EXEC_RAM_BASE + BK7258_BL2_EXEC_RAM_SIZE)
#define BK7258_BL2_DATA_RAM_BASE         BK7258_BL2_EXEC_RAM_BASE
#define BK7258_BL2_DATA_RAM_SIZE         BK7258_BL2_EXEC_RAM_SIZE

#define BK7258_CRC_PHYSICAL_OFFSET(n)    \
  (((n) / BK7258_FLASH_CRC_DATA_SIZE) * BK7258_FLASH_CRC_TOTAL_SIZE)
#define BK7258_CP_PHYSICAL_OFFSET        \
  BK7258_CRC_PHYSICAL_OFFSET(BK7258_CP_FLASH_OFFSET)
#define BK7258_AP_PHYSICAL_OFFSET        \
  BK7258_CRC_PHYSICAL_OFFSET(BK7258_AP_FLASH_OFFSET)

#define BK7258_BOOT_RAW_PHYSICAL_START   BK7258_ROLE_BOOT_OFFSET
#define BK7258_BOOT_RAW_PHYSICAL_SIZE    BK7258_ROLE_BOOT_SIZE
#define BK7258_CP_RAW_PHYSICAL_START     BK7258_ROLE_SLOT_A_CP_OFFSET
#define BK7258_CP_RAW_PHYSICAL_SIZE      BK7258_ROLE_SLOT_A_CP_SIZE
#define BK7258_AP_RAW_PHYSICAL_START     BK7258_ROLE_SLOT_A_AP_OFFSET
#define BK7258_AP_RAW_PHYSICAL_SIZE      BK7258_ROLE_SLOT_A_AP_SIZE
#define BK7258_AB_SECONDARY_START        BK7258_ROLE_SLOT_B_PAIR_OFFSET
#define BK7258_AB_SECONDARY_SIZE         BK7258_ROLE_SLOT_B_PAIR_SIZE
#define BK7258_USR_CONFIG_START          BK7258_ROLE_VENDOR_CONFIG_OFFSET
#define BK7258_USR_CONFIG_SIZE           BK7258_ROLE_VENDOR_CONFIG_SIZE

#define BK7258_DATA_RAW_PHYSICAL_OFFSET  BK7258_ROLE_LITTLEFS_OFFSET
#define BK7258_DATA_RAW_PHYSICAL_SIZE    BK7258_ROLE_LITTLEFS_SIZE
#define BK7258_CALIBRATION_TAIL_START    BK7258_ROLE_EASYFLASH_CP_OFFSET
#define BK7258_FLASH_RAW_SIZE            BK7258_FLASH_SIZE

/* Pinned SDK partition IDs and raw offsets are aliases of the generated
 * product layout, not chip constants.  Keep the vendor-facing names here so
 * SDK wrappers can validate the selected board contract without duplicating
 * addresses in <arch/chip>.
 */

#define BK7258_SDK_PARTITION_SYS_RF      BK7258_ROLE_CALIBRATION_RF_SDK_ID
#define BK7258_SDK_PARTITION_SYS_NET     BK7258_ROLE_CALIBRATION_NET_SDK_ID
#define BK7258_SDK_SYS_RF_START          BK7258_ROLE_CALIBRATION_RF_OFFSET
#define BK7258_SDK_SYS_NET_START         BK7258_ROLE_CALIBRATION_NET_OFFSET
#define BK7258_SDK_DATA_PARTITION_SIZE   BK7258_ROLE_CALIBRATION_NET_SIZE

static_assert(BK7258_CP_FLASH_OFFSET + BK7258_CP_FLASH_SIZE ==
              BK7258_AP_FLASH_OFFSET,
              "primary CP/AP logical windows must be contiguous");
static_assert(BK7258_CP_PHYSICAL_OFFSET ==
              BK7258_CP_RAW_PHYSICAL_START,
              "CP logical/raw address conversion drift");
static_assert(BK7258_CRC_PHYSICAL_OFFSET(BK7258_CP_FLASH_SIZE) ==
              BK7258_CP_RAW_PHYSICAL_SIZE,
              "CP logical/raw size conversion drift");
static_assert(BK7258_AP_PHYSICAL_OFFSET ==
              BK7258_AP_RAW_PHYSICAL_START,
              "AP logical/raw address conversion drift");
static_assert(BK7258_CRC_PHYSICAL_OFFSET(BK7258_AP_FLASH_SIZE) ==
              BK7258_AP_RAW_PHYSICAL_SIZE,
              "AP logical/raw size conversion drift");
static_assert(BK7258_AP_RAW_PHYSICAL_START +
              BK7258_AP_RAW_PHYSICAL_SIZE == BK7258_AB_SECONDARY_START,
              "primary pair must end at s_app");
static_assert(BK7258_CP_RAW_PHYSICAL_SIZE +
              BK7258_AP_RAW_PHYSICAL_SIZE == BK7258_AB_SECONDARY_SIZE,
              "primary and secondary pair sizes must match");
static_assert(BK7258_USR_CONFIG_START + BK7258_USR_CONFIG_SIZE <=
              BK7258_DATA_RAW_PHYSICAL_OFFSET,
              "vendor user config overlaps LittleFS");
static_assert(BK7258_DATA_RAW_PHYSICAL_OFFSET +
              BK7258_DATA_RAW_PHYSICAL_SIZE <=
              BK7258_CALIBRATION_TAIL_START,
              "LittleFS overlaps calibration tail");
static_assert(BK7258_CALIBRATION_TAIL_START < BK7258_FLASH_RAW_SIZE,
              "calibration tail must be inside Flash");
static_assert(BK7258_ROLE_CALIBRATION_RF_SIZE ==
              BK7258_ROLE_CALIBRATION_NET_SIZE,
              "SDK calibration partitions must have equal sizes");

#endif /* __ARCH_BOARD_BK7258_IMAGE_LAYOUT_H */
