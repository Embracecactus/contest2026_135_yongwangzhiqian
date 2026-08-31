/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __MOCK_DRIVER_FLASH_PARTITION_H
#define __MOCK_DRIVER_FLASH_PARTITION_H

#include <stdint.h>

#include <driver/flash.h>
#include <driver/flash_types.h>

#define PAR_OPT_READ_DIS     (0u << 0)
#define PAR_OPT_READ_EN      (1u << 0)
#define PAR_OPT_WRITE_DIS    (0u << 1)
#define PAR_OPT_WRITE_EN     (1u << 1)
#define PAR_OPT_EXECUTE_DIS  (0u << 2)
#define PAR_OPT_EXECUTE_EN   (1u << 2)

#define BK_PARTITIONS_TABLE_SIZE       9u
#define BK_PARTITION_BOOTLOADER        0u
#define BK_PARTITION_APPLICATION       1u
#define BK_PARTITION_APPLICATION1      2u
#define BK_PARTITION_SYS_RF            3u
#define BK_PARTITION_SYS_NET           4u
#define BK_PARTITION_OTA               5u
#define BK_PARTITION_USR_CONFIG        6u
#define BK_PARTITION_EASYFLASH         7u
#define BK_PARTITION_EASYFLASH_AP      8u

typedef uint32_t bk_partition_t;

typedef enum
{
  BK_FLASH_EMBEDDED = 0,
  BK_FLASH_SPI = 1,
} bk_flash_t;

typedef struct
{
  bk_flash_t partition_owner;
  const char *partition_description;
  uint32_t partition_start_addr;
  uint32_t partition_length;
  uint32_t partition_options;
} bk_logic_partition_t;

bk_logic_partition_t *bk_flash_partition_get_info(
  bk_partition_t partition);

bk_err_t bk_spec_flash_write_bytes(bk_partition_t partition,
                                    const uint8_t *buffer,
                                    uint32_t size, uint32_t offset);

#endif /* __MOCK_DRIVER_FLASH_PARTITION_H */
