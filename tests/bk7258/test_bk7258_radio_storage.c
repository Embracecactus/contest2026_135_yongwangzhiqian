/****************************************************************************
 * Host contract test for the chip-owned BK7258 radio storage mechanism.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <nuttx/mutex.h>

#include <arch/chip/bk7258_flash.h>
#include <arch/chip/bk7258_storage_config.h>
#include <arch/chip/bk7258_storage_guard.h>

#include "bk7258_radio_storage.h"

struct partition_state_s
{
  bool present;
  uint32_t start;
  uint32_t size;
};

static struct partition_state_s g_partitions[4];
static int g_read_result;
static int g_write_result;
static int g_update_result;
static uint32_t g_last_partition;
static uint32_t g_last_address;
static uint32_t g_last_offset;
static size_t g_last_size;
static int g_guard_result;
static int g_guard_locks;
static int g_guard_unlocks;
static bool g_guard_write;

int bk7258_storage_guard_lock(enum bk7258_storage_guard_e guard,
                              bool write_access, uint32_t timeout_ms)
{
  assert(guard == BK7258_STORAGE_GUARD_RADIO);
  assert(timeout_ms == 0u);
  g_guard_locks++;
  g_guard_write = write_access;
  return g_guard_result;
}

void bk7258_storage_guard_unlock(void)
{
  g_guard_unlocks++;
}

int bk7258_flash_partition_get_info(
  uint32_t partition, struct bk7258_flash_partition_info_s *info)
{
  if (info == NULL || partition >= 4u || !g_partitions[partition].present)
    {
      return -ENOENT;
    }

  info->start = g_partitions[partition].start;
  info->size = g_partitions[partition].size;
  return 0;
}

int bk7258_flash_read(uint32_t address, void *buffer, size_t nbytes)
{
  assert(buffer != NULL);
  g_last_address = address;
  g_last_size = nbytes;
  return g_read_result;
}

int bk7258_flash_write(uint32_t address, const void *buffer, size_t nbytes)
{
  assert(buffer != NULL);
  g_last_address = address;
  g_last_size = nbytes;
  return g_write_result;
}

int bk7258_flash_partition_update(uint32_t partition, uint32_t offset,
                                  const void *buffer, size_t nbytes)
{
  assert(buffer != NULL);
  g_last_partition = partition;
  g_last_offset = offset;
  g_last_size = nbytes;
  return g_update_result;
}

static struct bk7258_radio_storage_config_s make_config(void)
{
  return (struct bk7258_radio_storage_config_s)
  {
    .version = BK7258_RADIO_STORAGE_CONFIG_VERSION,
    .size = sizeof(struct bk7258_radio_storage_config_s),
    .backup =
    {
      .partition = 1u,
      .start = 0x1000u,
      .size = 0x1000u,
    },
    .network =
    {
      .partition = 2u,
      .start = 0x3000u,
      .size = 0x1000u,
    },
  };
}

int main(void)
{
  struct bk7258_radio_storage_config_s config = make_config();
  struct bk7258_radio_storage_config_s replacement;
  uint8_t buffer[8] = {0};

  assert(bk7258_radio_storage_read(BK7258_RADIO_STORE_BACKUP, 0u,
                                   buffer, sizeof(buffer)) == -EAGAIN);
  assert(bk7258_radio_storage_read((enum bk7258_radio_store_e)2, 0u,
                                   buffer, sizeof(buffer)) == -EINVAL);
  assert(bk7258_radio_storage_initialize(NULL) == -EINVAL);
  config.version++;
  assert(bk7258_radio_storage_initialize(&config) == -EINVAL);
  config = make_config();
  config.size = 0u;
  assert(bk7258_radio_storage_initialize(&config) == -EINVAL);
  config = make_config();
  config.network.partition = config.backup.partition;
  assert(bk7258_radio_storage_initialize(&config) == -EINVAL);
  config = make_config();
  config.backup.start++;
  assert(bk7258_radio_storage_initialize(&config) == -EINVAL);
  config = make_config();
  config.backup.size = 0u;
  assert(bk7258_radio_storage_initialize(&config) == -EINVAL);

  config = make_config();
  assert(bk7258_radio_storage_initialize(&config) == -ENOENT);
  g_partitions[1] = (struct partition_state_s)
  {
    .present = true,
    .start = 0x1000u,
    .size = 0x1000u,
  };
  g_partitions[2] = (struct partition_state_s)
  {
    .present = true,
    .start = 0x3000u,
    .size = 0x1000u,
  };

  config.backup.start = 0x2000u;
  assert(bk7258_radio_storage_initialize(&config) == -EINVAL);
  config = make_config();
  g_partitions[2].start = 0x1000u;
  config.network.start = 0x1000u;
  assert(bk7258_radio_storage_initialize(&config) == -EINVAL);
  g_partitions[2].start = 0x3000u;
  config = make_config();
  mock_mutex_fail_next(1);
  assert(bk7258_radio_storage_initialize(&config) == -EAGAIN);
  assert(bk7258_radio_storage_initialize(&config) == 0);
  assert(bk7258_radio_storage_initialize(&config) == 0);
  config.backup.start = 0x7000u;

  replacement = make_config();
  replacement.network.partition = 3u;
  replacement.network.start = 0x5000u;
  g_partitions[3] = (struct partition_state_s)
  {
    .present = true,
    .start = 0x5000u,
    .size = 0x1000u,
  };
  assert(bk7258_radio_storage_initialize(&replacement) == -EALREADY);

  assert(bk7258_radio_storage_read(BK7258_RADIO_STORE_BACKUP, 0u,
                                   NULL, sizeof(buffer)) == -EINVAL);
  assert(bk7258_radio_storage_read(BK7258_RADIO_STORE_BACKUP, 0u,
                                   buffer, 0u) == -EINVAL);
  assert(bk7258_radio_storage_read(BK7258_RADIO_STORE_BACKUP, 0x1000u,
                                   buffer, 1u) == -EINVAL);
  g_guard_result = -EBUSY;
  assert(bk7258_radio_storage_read(BK7258_RADIO_STORE_BACKUP, 4u,
                                   buffer, sizeof(buffer)) == -EBUSY);
  assert(g_guard_unlocks == 0);
  g_guard_result = 0;
  g_read_result = -EIO;
  assert(bk7258_radio_storage_read(BK7258_RADIO_STORE_BACKUP, 4u,
                                   buffer, sizeof(buffer)) == -EIO);
  assert(g_last_address == 0x1004u && g_last_size == sizeof(buffer));
  assert(g_guard_locks == 2 && g_guard_unlocks == 1 && !g_guard_write);
  g_read_result = 0;
  assert(bk7258_radio_storage_read(BK7258_RADIO_STORE_NETWORK, 8u,
                                   buffer, sizeof(buffer)) == 0);
  assert(g_last_address == 0x3008u && g_last_size == sizeof(buffer));

  assert(bk7258_radio_storage_write(BK7258_RADIO_STORE_BACKUP, 0u,
                                    NULL, sizeof(buffer)) == -EINVAL);
  g_write_result = -EROFS;
  assert(bk7258_radio_storage_write(BK7258_RADIO_STORE_BACKUP, 12u,
                                    buffer, sizeof(buffer)) == -EROFS);
  assert(g_last_address == 0x100cu && g_last_size == sizeof(buffer));
  assert(g_guard_write);
  g_update_result = -EIO;
  assert(bk7258_radio_storage_write(BK7258_RADIO_STORE_NETWORK, 16u,
                                    buffer, sizeof(buffer)) == -EIO);
  assert(g_last_partition == 2u && g_last_offset == 16u &&
         g_last_size == sizeof(buffer));
  assert(g_guard_locks == 5 && g_guard_unlocks == 4);

  puts("bk7258 radio storage contract test: PASS");
  return 0;
}
