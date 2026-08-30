/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * Host contract tests for the BK7258 OTA pair health-confirmation adapter.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_BK7258_AP_SUPERVISOR
#  include <setjmp.h>
#endif

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_storage_config.h>
#include <arch/chip/bk7258_boot_slot.h>
#include <arch/chip/bk7258_flash.h>
#include <arch/chip/bk7258_ota.h>
#include <arch/chip/bk7258_reset_cause.h>
#include <arch/chip/bk7258_storage_guard.h>

#include "bk7258_ota_image.h"

#ifndef CONFIG_BK7258_OTA
#  error "CONFIG_BK7258_OTA is required"
#endif

#define TEST_HEALTH_MAX_AGE_MS 1234u
#define TEST_GENERATION         17u
#define TEST_SAMPLE_SEQUENCE    41u
#define TEST_LOGICAL_SIZE       4096u
#define TEST_RAW_SIZE \
  (TEST_LOGICAL_SIZE / BK7258_OTA_CRC_DATA_SIZE * \
   BK7258_OTA_CRC_TOTAL_SIZE)
#define TEST_CP_RAW_BASE        0x00100000u
#define TEST_AP_RAW_BASE        0x00200000u
#define TEST_SECTOR_OFFSET      (TEST_RAW_SIZE - BK7258_OTA_ERASE_SIZE)
#define TEST_CONFIRM_LOGICAL_OFFSET \
  BK7258_MCUBOOT_IMAGE_OK_OFFSET(TEST_LOGICAL_SIZE)
#define TEST_CONFIRM_PACKET_OFFSET \
  (TEST_CONFIRM_LOGICAL_OFFSET / BK7258_OTA_CRC_DATA_SIZE * \
   BK7258_OTA_CRC_TOTAL_SIZE - TEST_SECTOR_OFFSET)
#define TEST_CONFIRM_DATA_OFFSET \
  (TEST_CONFIRM_LOGICAL_OFFSET % BK7258_OTA_CRC_DATA_SIZE)

static struct bk7258_ota_layout_s g_layout;
static struct bk7258_ap_boot_state_s g_boot_state;
static uint8_t g_cp_sector[BK7258_FLASH_SECTOR_SIZE];
static uint8_t g_ap_sector[BK7258_FLASH_SECTOR_SIZE];

static unsigned int g_storage_layout_calls;
static unsigned int g_storage_lock_calls;
static unsigned int g_storage_unlock_calls;
static unsigned int g_boot_active_calls;
static unsigned int g_flash_initialize_calls;
static unsigned int g_flash_read_calls;
static unsigned int g_flash_erase_calls;
static unsigned int g_flash_write_calls;
static unsigned int g_cp_erase_calls;
static unsigned int g_ap_erase_calls;
static unsigned int g_cp_write_calls;
static unsigned int g_ap_write_calls;

static uint8_t *test_sector(uint32_t address)
{
  if (address == TEST_CP_RAW_BASE + TEST_SECTOR_OFFSET)
    {
      return g_cp_sector;
    }

  if (address == TEST_AP_RAW_BASE + TEST_SECTOR_OFFSET)
    {
      return g_ap_sector;
    }

  return NULL;
}

static uint16_t test_crc16(const uint8_t *data)
{
  uint16_t crc = 0xffffu;
  uint32_t index;
  uint32_t bit;

  for (index = 0; index < BK7258_OTA_CRC_DATA_SIZE; index++)
    {
      crc ^= (uint16_t)data[index] << 8;
      for (bit = 0; bit < 8u; bit++)
        {
          crc = (uint16_t)((crc << 1) ^
            ((crc & 0x8000u) != 0u ? 0x8005u : 0u));
        }
    }

  return crc;
}

#ifdef CONFIG_BK7258_AP_SUPERVISOR
static void prepare_sector(uint8_t *sector)
{
  uint16_t crc;

  memset(sector, 0xff, BK7258_FLASH_SECTOR_SIZE);
  crc = test_crc16(&sector[TEST_CONFIRM_PACKET_OFFSET]);
  sector[TEST_CONFIRM_PACKET_OFFSET + BK7258_OTA_CRC_DATA_SIZE] =
    (uint8_t)(crc >> 8);
  sector[TEST_CONFIRM_PACKET_OFFSET + BK7258_OTA_CRC_DATA_SIZE + 1u] =
    (uint8_t)crc;
}

static void setup_layout(void)
{
  memset(&g_layout, 0, sizeof(g_layout));
  g_layout.version = BK7258_OTA_LAYOUT_VERSION;
  g_layout.size = sizeof(g_layout);
  g_layout.erase_size = BK7258_OTA_ERASE_SIZE;
  g_layout.crc_data_size = BK7258_OTA_CRC_DATA_SIZE;
  g_layout.crc_total_size = BK7258_OTA_CRC_TOTAL_SIZE;
  g_layout.remap.version = BK7258_BOOT_SLOT_MAP_VERSION;
  g_layout.remap.size = sizeof(g_layout.remap);
  g_layout.remap.secondary_begin = 0x01000000u;
  g_layout.remap.secondary_end = 0x01200000u;
  g_layout.remap.secondary_offset = 0x00200000u;
  g_layout.slot[BK7258_BOOT_SLOT_PRIMARY][BK7258_OTA_IMAGE_CP].raw_offset =
    TEST_CP_RAW_BASE;
  g_layout.slot[BK7258_BOOT_SLOT_PRIMARY][BK7258_OTA_IMAGE_CP].raw_size =
    TEST_RAW_SIZE;
  g_layout.slot[BK7258_BOOT_SLOT_PRIMARY][BK7258_OTA_IMAGE_AP].raw_offset =
    TEST_AP_RAW_BASE;
  g_layout.slot[BK7258_BOOT_SLOT_PRIMARY][BK7258_OTA_IMAGE_AP].raw_size =
    TEST_RAW_SIZE;
  g_layout.active_logical_size[BK7258_OTA_IMAGE_CP] = TEST_LOGICAL_SIZE;
  g_layout.active_logical_size[BK7258_OTA_IMAGE_AP] = TEST_LOGICAL_SIZE;

  prepare_sector(g_cp_sector);
  prepare_sector(g_ap_sector);
}
#endif

int bk7258_storage_ota_layout(
  const struct bk7258_ota_layout_s **layout)
{
  if (layout == NULL)
    {
      return -EINVAL;
    }

  g_storage_layout_calls++;
  *layout = &g_layout;
  return 0;
}

int bk7258_storage_lock(enum bk7258_storage_guard_e guard,
                        uint32_t timeout_ms)
{
  assert(guard == BK7258_STORAGE_GUARD_OTA_CONFIRM_PRIMARY);
  assert(timeout_ms == 5000u);
  g_storage_lock_calls++;
  return 0;
}

void bk7258_storage_unlock(void)
{
  g_storage_unlock_calls++;
}

int bk7258_boot_active_slot(const struct bk7258_boot_slot_map_s *map,
                            enum bk7258_boot_slot_e *slot)
{
  if (map == NULL || slot == NULL)
    {
      return -EINVAL;
    }

  g_boot_active_calls++;
  *slot = BK7258_BOOT_SLOT_PRIMARY;
  return 0;
}

int bk7258_ota_xip_image_metadata(
  uint32_t xip, uint32_t logical_size,
  struct bk7258_ota_image_metadata_s *metadata)
{
  (void)xip;
  if (metadata == NULL || logical_size != TEST_LOGICAL_SIZE)
    {
      return -EINVAL;
    }

  memset(metadata, 0, sizeof(*metadata));
  metadata->version.major = 1u;
  metadata->version.minor = 2u;
  metadata->version.revision = 3u;
  metadata->version.build = 4u;
  metadata->security_counter = 9u;
  metadata->security_counter_present = true;
  return 0;
}

int bk7258_flash_initialize(void)
{
  g_flash_initialize_calls++;
  return 0;
}

int bk7258_flash_read(uint32_t address, void *buffer, size_t nbytes)
{
  uint8_t *sector = test_sector(address);

  if (sector == NULL || buffer == NULL ||
      nbytes != BK7258_FLASH_SECTOR_SIZE)
    {
      return -EIO;
    }

  g_flash_read_calls++;
  memcpy(buffer, sector, nbytes);
  return 0;
}

int bk7258_flash_erase_sector(uint32_t address)
{
  uint8_t *sector = test_sector(address);

  if (sector == NULL)
    {
      return -EIO;
    }

  g_flash_erase_calls++;
  if (sector == g_cp_sector)
    {
      g_cp_erase_calls++;
    }
  else
    {
      g_ap_erase_calls++;
    }

  memset(sector, 0xff, BK7258_FLASH_SECTOR_SIZE);
  return 0;
}

int bk7258_flash_write(uint32_t address, const void *buffer, size_t nbytes)
{
  uint8_t *sector = test_sector(address);

  if (sector == NULL || buffer == NULL ||
      nbytes != BK7258_FLASH_SECTOR_SIZE)
    {
      return -EIO;
    }

  g_flash_write_calls++;
  if (sector == g_cp_sector)
    {
      g_cp_write_calls++;
    }
  else
    {
      g_ap_write_calls++;
    }

  memcpy(sector, buffer, nbytes);
  return 0;
}

int bk7258_ota_flash_initialize(void)
{
  return bk7258_flash_initialize();
}

int bk7258_ota_flash_verify(uint32_t address, const uint8_t *expected,
                            uint32_t nbytes)
{
  uint8_t *sector = test_sector(address);

  if (sector == NULL || expected == NULL ||
      nbytes != BK7258_FLASH_SECTOR_SIZE)
    {
      return -EIO;
    }

  return memcmp(sector, expected, nbytes) == 0 ? 0 : -EIO;
}

uint16_t bk7258_ota_flash_crc16(const uint8_t *data)
{
  return test_crc16(data);
}

volatile struct bk7258_ap_boot_state_s *bk7258_ap_boot_state(void)
{
  return &g_boot_state;
}

#ifdef CONFIG_BK7258_AP_SUPERVISOR
static jmp_buf g_reset_env;
static unsigned int g_reset_calls;
static unsigned int g_health_calls;
static uint32_t g_health_max_age[2];
static uint8_t g_xip[TEST_LOGICAL_SIZE * 2u];

int bk7258_ap_supervisor_health_token(
  uint32_t expected_generation, uint32_t max_age_ms,
  struct bk7258_ap_supervisor_health_token_s *token)
{
  assert(expected_generation == TEST_GENERATION);
  assert(token != NULL);
  assert(g_health_calls < 2u);
  g_health_max_age[g_health_calls] = max_age_ms;
  if (g_health_calls++ == 1u)
    {
      return -EHOSTDOWN;
    }

  token->generation = TEST_GENERATION;
  token->sample_sequence = TEST_SAMPLE_SEQUENCE + 1u;
  token->flags = 0u;
  token->healthy_age_ms = 0u;
  token->sample_age_ms = 0u;
  return 0;
}

static void setup_supervisor_xip(void)
{
  static const uint8_t magic[BK7258_MCUBOOT_TRAILER_MAGIC_SIZE] =
    BK7258_MCUBOOT_TRAILER_MAGIC_INIT;

  assert((uintptr_t)&g_xip[sizeof(g_xip) - 1u] <= UINT32_MAX);
  memset(g_xip, 0xff, sizeof(g_xip));
  memcpy(g_xip + TEST_LOGICAL_SIZE - sizeof(magic), magic, sizeof(magic));
  memcpy(g_xip + sizeof(g_xip) - sizeof(magic), magic, sizeof(magic));
  g_xip[BK7258_MCUBOOT_COPY_DONE_OFFSET(TEST_LOGICAL_SIZE)] = 1u;
  g_xip[TEST_LOGICAL_SIZE +
        BK7258_MCUBOOT_COPY_DONE_OFFSET(TEST_LOGICAL_SIZE)] = 1u;
  g_layout.active_xip_start[BK7258_OTA_IMAGE_CP] =
    (uint32_t)(uintptr_t)&g_xip[0];
  g_layout.active_xip_start[BK7258_OTA_IMAGE_AP] =
    (uint32_t)(uintptr_t)&g_xip[TEST_LOGICAL_SIZE];
}

static void test_supervisor_confirmation(void)
{
  struct bk7258_ota_pair_snapshot_s expected =
  {
    .active_slot = BK7258_BOOT_SLOT_PRIMARY,
    .state = BK7258_OTA_PAIR_PENDING,
    .version = {1u, 2u, 3u, 4u},
    .security_counter = 9u,
    .security_counter_present = true,
  };
  struct bk7258_ap_supervisor_health_token_s health =
  {
    .generation = TEST_GENERATION,
    .sample_sequence = TEST_SAMPLE_SEQUENCE,
  };

  setup_layout();
  setup_supervisor_xip();
  g_boot_state.generation = TEST_GENERATION;
  g_boot_state.state = BK7258_AP_STATE_READY;

  if (setjmp(g_reset_env) == 0)
    {
      int ret = bk7258_ota_confirm_pair_health(
        &expected, &health, TEST_HEALTH_MAX_AGE_MS);
      assert(ret == -EHOSTDOWN);
      assert(0);
    }

  assert(g_reset_calls == 1u);
  assert(g_health_calls == 2u);
  assert(g_health_max_age[0] == TEST_HEALTH_MAX_AGE_MS);
  assert(g_health_max_age[1] == TEST_HEALTH_MAX_AGE_MS);
  assert(g_storage_lock_calls == 1u);
  assert(g_storage_unlock_calls == 1u);
  assert(g_boot_active_calls != 0u);
  assert(g_flash_initialize_calls == 1u);
  assert(g_flash_read_calls == 1u);
  assert(g_flash_erase_calls == 1u);
  assert(g_flash_write_calls == 1u);
  assert(g_ap_erase_calls == 1u);
  assert(g_ap_write_calls == 1u);
  assert(g_cp_erase_calls == 0u);
  assert(g_cp_write_calls == 0u);
  assert(g_cp_sector[TEST_CONFIRM_PACKET_OFFSET +
                     TEST_CONFIRM_DATA_OFFSET] == 0xffu);
  assert(g_ap_sector[TEST_CONFIRM_PACKET_OFFSET +
                     TEST_CONFIRM_DATA_OFFSET] == 1u);
}
#endif

void bk7258_system_reset(enum bk7258_reset_source_e source)
{
#ifdef CONFIG_BK7258_AP_SUPERVISOR
  assert(source == BK7258_RESET_SOURCE_REBOOT);
  g_reset_calls++;
  longjmp(g_reset_env, 1);
#else
  (void)source;
  abort();
#endif
}

#ifndef CONFIG_BK7258_AP_SUPERVISOR
static void test_no_supervisor_confirmation(void)
{
  struct bk7258_ota_pair_snapshot_s expected =
  {
    .active_slot = BK7258_BOOT_SLOT_PRIMARY,
    .state = BK7258_OTA_PAIR_PENDING,
  };
  struct bk7258_ap_supervisor_health_token_s health = {0};

  assert(bk7258_ota_confirm_pair_health(
           &expected, NULL, TEST_HEALTH_MAX_AGE_MS) == -EINVAL);
  assert(bk7258_ota_confirm_pair_health(
           &expected, &health, 0u) == -EINVAL);
  assert(bk7258_ota_confirm_pair_health(
           NULL, &health, TEST_HEALTH_MAX_AGE_MS) == -ENOSYS);
  assert(bk7258_ota_confirm_pair_health(
           &expected, &health, TEST_HEALTH_MAX_AGE_MS) == -ENOSYS);
  assert(g_storage_layout_calls == 0u);
  assert(g_storage_lock_calls == 0u);
  assert(g_storage_unlock_calls == 0u);
  assert(g_boot_active_calls == 0u);
  assert(g_flash_initialize_calls == 0u);
  assert(g_flash_read_calls == 0u);
  assert(g_flash_erase_calls == 0u);
  assert(g_flash_write_calls == 0u);
}
#endif

int main(void)
{
#ifdef CONFIG_BK7258_AP_SUPERVISOR
  test_supervisor_confirmation();
  puts("bk7258 OTA health-confirm supervisor test: PASS");
#else
  test_no_supervisor_confirmation();
  puts("bk7258 OTA health-confirm no-supervisor test: PASS");
#endif
  return 0;
}
