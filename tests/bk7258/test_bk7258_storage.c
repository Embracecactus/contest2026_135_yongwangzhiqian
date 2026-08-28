/****************************************************************************
 * Host contract tests for the real BK7258 CP storage configuration.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nuttx/mutex.h>

#include <arch/chip/bk7258_storage_config.h>
#include <arch/chip/bk7258_storage_guard.h>

#include "bk7258_storage_configure.h"
#include "bk7258_storage_internal.h"

#ifndef TEST_STORAGE_SCENARIO
#  error "TEST_STORAGE_SCENARIO is required"
#endif

#define TEST_UNUSED static __attribute__((unused))

static struct bk7258_ota_layout_s g_layout;
static struct bk7258_radio_storage_config_s g_radio;
static struct bk7258_storage_region_s g_data;

TEST_UNUSED void fill_layout(void)
{
  uint32_t image;
  uint32_t slot;

  memset(&g_layout, 0, sizeof(g_layout));
  g_layout.version = BK7258_OTA_LAYOUT_VERSION;
  g_layout.size = sizeof(g_layout);
  g_layout.erase_size = BK7258_OTA_ERASE_SIZE;
  g_layout.crc_data_size = BK7258_OTA_CRC_DATA_SIZE;
  g_layout.crc_total_size = BK7258_OTA_CRC_TOTAL_SIZE;
  g_layout.remap.version = BK7258_BOOT_SLOT_MAP_VERSION;
  g_layout.remap.size = sizeof(g_layout.remap);
  g_layout.remap.secondary_begin = 0x100000u;
  g_layout.remap.secondary_end = 0x200000u;
  g_layout.remap.secondary_offset = 0x300000u;
  g_layout.active_xip_start[BK7258_OTA_IMAGE_CP] = 0x100000u;
  g_layout.active_logical_size[BK7258_OTA_IMAGE_CP] =
    BK7258_OTA_CRC_DATA_SIZE * 2048u;
  g_layout.active_xip_start[BK7258_OTA_IMAGE_AP] =
    g_layout.active_xip_start[BK7258_OTA_IMAGE_CP] +
    g_layout.active_logical_size[BK7258_OTA_IMAGE_CP];
  g_layout.active_logical_size[BK7258_OTA_IMAGE_AP] =
    BK7258_OTA_CRC_DATA_SIZE * 2048u;
  g_layout.layout_sha256[0] = 1u;

  for (slot = 0; slot < BK7258_OTA_SLOT_COUNT; slot++)
    {
      uint32_t base = 0x200000u + slot * 0x800000u;

      for (image = 0; image < BK7258_OTA_IMAGE_COUNT; image++)
        {
          uint32_t raw_size = g_layout.active_logical_size[image] /
                              g_layout.crc_data_size *
                              g_layout.crc_total_size;

          g_layout.slot[slot][image].raw_offset = base;
          g_layout.slot[slot][image].raw_size = raw_size;
          base += raw_size;
        }
    }

  g_radio = (struct bk7258_radio_storage_config_s)
  {
    .version = BK7258_RADIO_STORAGE_CONFIG_VERSION,
    .size = sizeof(g_radio),
    .backup =
    {
      .partition = 1u,
      .start = 0x6000u,
      .size = 0x1000u,
    },
    .network =
    {
      .partition = 2u,
      .start = 0x8000u,
      .size = 0x1000u,
    },
  };
  g_data = (struct bk7258_storage_region_s)
  {
    .start = 0xa000u,
    .size = 0x2000u,
  };
}

TEST_UNUSED struct bk7258_storage_config_s make_config(void)
{
  return (struct bk7258_storage_config_s)
  {
    .version = BK7258_STORAGE_CONFIG_VERSION,
    .size = sizeof(struct bk7258_storage_config_s),
    .ota_layout = &g_layout,
    .radio_storage = &g_radio,
    .data_storage = &g_data,
    .reset_marker_address = 0x10000u,
    .reset_marker_erase_size = BK7258_RESET_MARKER_ERASE_SIZE,
  };
}

TEST_UNUSED void test_unbound(void)
{
  const struct bk7258_ota_layout_s *layout = NULL;
  const struct bk7258_radio_storage_config_s *radio = NULL;
  uint32_t address = 0u;

  assert(bk7258_storage_ota_layout(NULL) == -EINVAL);
  assert(bk7258_storage_ota_layout(&layout) == -EAGAIN);
  assert(bk7258_storage_marker_address(NULL) == -EINVAL);
  assert(bk7258_storage_marker_address(&address) == -EAGAIN);
  assert(bk7258_storage_radio_config(NULL) == -EINVAL);
  assert(bk7258_storage_radio_config(&radio) == -EAGAIN);
  assert(bk7258_storage_lock(BK7258_STORAGE_GUARD_RESET_MARKER, 1u) ==
         -EAGAIN);
}

TEST_UNUSED void test_config(void)
{
  struct bk7258_storage_config_s value = make_config();
  struct bk7258_storage_config_s replacement = make_config();
  const struct bk7258_radio_storage_config_s *radio = NULL;
  uint32_t address = 0u;

  assert(bk7258_storage_configure(NULL) == -EINVAL);
  value.version++;
  assert(bk7258_storage_configure(&value) == -EINVAL);
  value = make_config();
  value.size = 0u;
  assert(bk7258_storage_configure(&value) == -EINVAL);

  value = make_config();
  assert(bk7258_storage_configure(&value) == 0);
  assert(bk7258_storage_configure(&value) == -EALREADY);
  assert(bk7258_storage_configure(&replacement) == -EALREADY);

  /* The chip publishes copies, not board-owned pointers. */

  value.reset_marker_address = 0u;
  g_radio.backup.start = 0x7000u;
  g_data.start = 0xc000u;
  assert(bk7258_storage_marker_address(&address) == 0);
  assert(address == 0x10000u);
  assert(bk7258_storage_radio_config(&radio) == 0);
  assert(radio != &g_radio && radio->backup.start == 0x6000u);
  assert(bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_DATA,
                                   true, 0u) == 0);
  assert(bk7258_storage_guard_write_authorized(0xa000u, 1u));
  assert(!bk7258_storage_guard_write_authorized(0xc000u, 1u));
  bk7258_storage_guard_unlock();
}

TEST_UNUSED void layout_fault_apply(unsigned int fault,
                                    struct bk7258_storage_config_s *value)
{
  switch (fault)
    {
      case 0:
        value->ota_layout = NULL;
        break;
      case 1:
        g_layout.version++;
        break;
      case 2:
        g_layout.size = 0u;
        break;
      case 3:
        memset(g_layout.layout_sha256, 0, sizeof(g_layout.layout_sha256));
        break;
      case 4:
        g_layout.crc_total_size++;
        break;
      case 5:
        g_layout.erase_size++;
        break;
      case 6:
        g_layout.remap.secondary_end = g_layout.remap.secondary_begin;
        break;
      case 7:
        g_layout.active_logical_size[BK7258_OTA_IMAGE_CP]++;
        break;
      case 8:
        g_layout.active_xip_start[BK7258_OTA_IMAGE_AP]++;
        break;
      case 9:
        g_layout.slot[BK7258_BOOT_SLOT_PRIMARY][BK7258_OTA_IMAGE_CP]
          .raw_offset++;
        break;
      case 10:
        g_layout.slot[BK7258_BOOT_SLOT_PRIMARY][BK7258_OTA_IMAGE_AP]
          .raw_offset++;
        break;
      case 11:
        g_layout.slot[BK7258_BOOT_SLOT_SECONDARY][BK7258_OTA_IMAGE_CP]
          .raw_offset =
            g_layout.slot[BK7258_BOOT_SLOT_PRIMARY][BK7258_OTA_IMAGE_AP]
              .raw_offset;
        break;
      default:
        assert(false);
    }
}

TEST_UNUSED void test_layout(void)
{
  const struct bk7258_ota_layout_s *layout = NULL;
  struct bk7258_storage_config_s value;
  unsigned int fault;

  for (fault = 0; fault < 12u; fault++)
    {
      pid_t child = fork();
      int status;

      assert(child >= 0);
      if (child == 0)
        {
          fill_layout();
          value = make_config();
          layout_fault_apply(fault, &value);
          assert(bk7258_storage_configure(&value) == 0);
          assert(bk7258_storage_ota_layout(&layout) == -EINVAL);
          _exit(0);
        }

      assert(waitpid(child, &status, 0) == child);
      assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

  fill_layout();
  value = make_config();
  assert(bk7258_storage_configure(&value) == 0);
  g_layout.version++;

  assert(bk7258_storage_ota_layout(&layout) == 0);
  assert(layout != &g_layout && layout->version == BK7258_OTA_LAYOUT_VERSION);
  assert(bk7258_storage_ota_layout(&layout) == 0);
}

TEST_UNUSED void test_locking(void)
{
  struct bk7258_storage_config_s value = make_config();
  const struct bk7258_ota_layout_s *layout;

  assert(bk7258_storage_configure(&value) == 0);
  assert(bk7258_storage_ota_layout(&layout) == 0);

  assert(bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_DATA,
                                   true, 55u) == 0);
  assert(bk7258_storage_guard_write_authorized(0xa000u, 0x2000u));
  assert(!bk7258_storage_guard_write_authorized(0x9fffu, 1u));
  assert(bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_DATA,
                                   true, 0u) == 0);
  assert(bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_RADIO,
                                   true, 0u) == -EDEADLK);
  bk7258_storage_guard_unlock();
  bk7258_storage_guard_unlock();

  assert(bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_RADIO,
                                   false, 0u) == 0);
  assert(!bk7258_storage_guard_write_authorized(0x6000u, 1u));
  assert(bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_RADIO,
                                   true, 0u) == -EDEADLK);
  bk7258_storage_guard_unlock();

  assert(bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_RADIO,
                                   true, 0u) == 0);
  assert(bk7258_storage_guard_write_authorized(0x6000u, 0x1000u));
  assert(bk7258_storage_guard_write_authorized(0x8000u, 0x1000u));
  assert(!bk7258_storage_guard_write_authorized(0x7000u, 1u));
  bk7258_storage_guard_unlock();

  assert(bk7258_storage_lock(BK7258_STORAGE_GUARD_OTA_STAGE_PRIMARY,
                             0u) == 0);
  assert(bk7258_storage_guard_write_authorized(
           layout->slot[BK7258_BOOT_SLOT_PRIMARY][BK7258_OTA_IMAGE_CP]
             .raw_offset,
           layout->slot[BK7258_BOOT_SLOT_PRIMARY][BK7258_OTA_IMAGE_CP]
             .raw_size));
  bk7258_storage_unlock();

  assert(bk7258_storage_lock(BK7258_STORAGE_GUARD_OTA_CONFIRM_SECONDARY,
                             0u) == 0);
  assert(bk7258_storage_guard_write_authorized(
           layout->slot[BK7258_BOOT_SLOT_SECONDARY][BK7258_OTA_IMAGE_AP]
             .raw_offset +
           layout->slot[BK7258_BOOT_SLOT_SECONDARY][BK7258_OTA_IMAGE_AP]
             .raw_size - layout->erase_size,
           layout->erase_size));
  assert(!bk7258_storage_guard_write_authorized(
           layout->slot[BK7258_BOOT_SLOT_SECONDARY][BK7258_OTA_IMAGE_AP]
             .raw_offset,
           1u));
  bk7258_storage_unlock();

  assert(bk7258_storage_lock(BK7258_STORAGE_GUARD_RESET_MARKER, 0u) == 0);
  assert(bk7258_storage_guard_write_authorized(
           0x10000u, BK7258_RESET_MARKER_ERASE_SIZE));
  bk7258_storage_unlock();

  assert(bk7258_storage_lock((enum bk7258_storage_guard_e)-1, 0u) ==
         -EINVAL);
  assert(bk7258_storage_lock(BK7258_STORAGE_GUARD_COUNT, 0u) ==
         -EINVAL);
  mock_mutex_fail_next(1);
  assert(bk7258_storage_lock(BK7258_STORAGE_GUARD_RESET_MARKER, 0u) ==
         -EAGAIN);
}

int main(void)
{
  fill_layout();
#if TEST_STORAGE_SCENARIO == 1
  test_unbound();
#elif TEST_STORAGE_SCENARIO == 2
  test_config();
#elif TEST_STORAGE_SCENARIO == 3
  test_layout();
#elif TEST_STORAGE_SCENARIO == 4
  test_locking();
#else
#  error "Unknown TEST_STORAGE_SCENARIO"
#endif
  puts("bk7258 storage config contract test: PASS");
  return 0;
}
