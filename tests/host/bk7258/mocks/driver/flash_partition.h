/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __MOCK_DRIVER_FLASH_PARTITION_H
#define __MOCK_DRIVER_FLASH_PARTITION_H

#include <stdint.h>

#include <driver/flash.h>

typedef uint32_t bk_partition_t;

typedef struct
{
  uint32_t partition_start_addr;
  uint32_t partition_length;
} bk_logic_partition_t;

bk_logic_partition_t *bk_flash_partition_get_info(
  bk_partition_t partition);

bk_err_t bk_spec_flash_write_bytes(bk_partition_t partition,
                                    const uint8_t *buffer,
                                    uint32_t size, uint32_t offset);

#endif /* __MOCK_DRIVER_FLASH_PARTITION_H */
