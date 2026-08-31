/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * Host contract tests for SDK semantic-to-generated partition translation.
 ****************************************************************************/

#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <bk7258_partitions.h>
#include <driver/flash_partition.h>

#include <arch/chip/bk7258_flash.h>

#include "bk7258_sdk_partition.h"

static uint32_t g_read_address;
static uint32_t g_write_address;
static uint32_t g_erase_address;
static unsigned int g_read_calls;
static unsigned int g_write_calls;
static unsigned int g_erase_calls;

bk_logic_partition_t *__wrap_bk_flash_partition_get_info(
  bk_partition_t partition);
bk_err_t __wrap_bk_flash_partition_read(bk_partition_t partition,
                                        uint8_t *buffer,
                                        uint32_t offset, uint32_t size);
bk_err_t __wrap_bk_flash_partition_write(bk_partition_t partition,
                                         const uint8_t *buffer,
                                         uint32_t offset, uint32_t size);
bk_err_t __wrap_bk_flash_partition_erase(bk_partition_t partition,
                                         uint32_t offset, uint32_t size);

int bk7258_flash_read(uint32_t address, void *buffer, size_t nbytes)
{
  assert(buffer != NULL && nbytes != 0u);
  g_read_address = address;
  g_read_calls++;
  memset(buffer, 0xa5, nbytes);
  return 0;
}

int bk7258_flash_write(uint32_t address, const void *buffer, size_t nbytes)
{
  assert(buffer != NULL && nbytes != 0u);
  g_write_address = address;
  g_write_calls++;
  return 0;
}

int bk7258_flash_erase_sector(uint32_t address)
{
  g_erase_address = address;
  g_erase_calls++;
  return 0;
}

static void assert_partition(bk_partition_t sdk_partition,
                             const char *name,
                             uint32_t offset, uint32_t size,
                             bool writable)
{
  bk_logic_partition_t *partition =
    __wrap_bk_flash_partition_get_info(sdk_partition);

  assert(partition != NULL);
  assert(strcmp(partition->partition_description, name) == 0);
  assert(partition->partition_start_addr == offset);
  assert(partition->partition_length == size);
  assert(((partition->partition_options & PAR_OPT_WRITE_EN) != 0u) ==
         writable);
}

int main(void)
{
  uint32_t sdk_partition;
  uint8_t byte = 0;

  assert_partition(BK_PARTITION_BOOTLOADER, "primary_bootloader",
                   BK7258_PARTITION_PRIMARY_BOOTLOADER_OFFSET,
                   BK7258_PARTITION_PRIMARY_BOOTLOADER_SIZE, false);
  assert_partition(BK_PARTITION_APPLICATION, "primary_cp_app",
                   BK7258_PARTITION_PRIMARY_CP_APP_OFFSET,
                   BK7258_PARTITION_PRIMARY_CP_APP_SIZE, false);
  assert_partition(BK_PARTITION_APPLICATION1, "primary_ap_app",
                   BK7258_PARTITION_PRIMARY_AP_APP_OFFSET,
                   BK7258_PARTITION_PRIMARY_AP_APP_SIZE, false);
  assert_partition(BK_PARTITION_SYS_RF, "sys_rf",
                   BK7258_PARTITION_SYS_RF_OFFSET,
                   BK7258_PARTITION_SYS_RF_SIZE, true);
  assert_partition(BK_PARTITION_SYS_NET, "sys_net",
                   BK7258_PARTITION_SYS_NET_OFFSET,
                   BK7258_PARTITION_SYS_NET_SIZE, true);
  assert_partition(BK_PARTITION_OTA, "s_app",
                   BK7258_PARTITION_S_APP_OFFSET,
                   BK7258_PARTITION_S_APP_SIZE, false);
  assert_partition(BK_PARTITION_USR_CONFIG, "usr_config",
                   BK7258_PARTITION_USR_CONFIG_OFFSET,
                   BK7258_PARTITION_USR_CONFIG_SIZE, true);
  assert_partition(BK_PARTITION_EASYFLASH, "easyflash",
                   BK7258_PARTITION_EASYFLASH_OFFSET,
                   BK7258_PARTITION_EASYFLASH_SIZE, true);
  assert_partition(BK_PARTITION_EASYFLASH_AP, "easyflash_ap",
                   BK7258_PARTITION_EASYFLASH_AP_OFFSET,
                   BK7258_PARTITION_EASYFLASH_AP_SIZE, true);
  assert(__wrap_bk_flash_partition_get_info(BK_PARTITIONS_TABLE_SIZE) ==
         NULL);

  assert(bk7258_sdk_partition_from_layout(
           BK7258_PARTITION_SYS_RF_INDEX, &sdk_partition) == 0);
  assert(sdk_partition == BK_PARTITION_SYS_RF);
  assert(bk7258_sdk_partition_from_layout(
           BK7258_PARTITION_SYS_NET_INDEX, &sdk_partition) == 0);
  assert(sdk_partition == BK_PARTITION_SYS_NET);
  assert(bk7258_sdk_partition_from_layout(
           BK7258_PARTITION_SECONDARY_MANIFEST_INDEX,
           &sdk_partition) == -ENOENT);

  assert(__wrap_bk_flash_partition_read(BK_PARTITION_SYS_RF, &byte,
                                        7u, 1u) == BK_OK);
  assert(g_read_calls == 1u);
  assert(g_read_address == BK7258_PARTITION_SYS_RF_OFFSET + 7u);

  assert(__wrap_bk_flash_partition_write(BK_PARTITION_SYS_NET, &byte,
                                         9u, 1u) == BK_OK);
  assert(g_write_calls == 1u);
  assert(g_write_address == BK7258_PARTITION_SYS_NET_OFFSET + 9u);

  assert(__wrap_bk_flash_partition_write(BK_PARTITION_BOOTLOADER, &byte,
                                         0u, 1u) ==
         BK_ERR_FLASH_ADDR_OUT_OF_RANGE);
  assert(g_write_calls == 1u);

  assert(__wrap_bk_flash_partition_erase(BK_PARTITION_SYS_RF, 0u,
                                         BK7258_FLASH_ERASE_SIZE) == BK_OK);
  assert(g_erase_calls == 1u);
  assert(g_erase_address == BK7258_PARTITION_SYS_RF_OFFSET);

  puts("BK7258 SDK semantic partition mapping tests: PASS");
  return 0;
}
