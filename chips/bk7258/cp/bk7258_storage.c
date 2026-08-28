/****************************************************************************
 * chips/bk7258/cp/bk7258_storage.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-side validation and publication of the immutable storage config.
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>

#include <arch/chip/bk7258_storage_guard.h>

#include "bk7258_storage_configure.h"
#include "bk7258_storage_internal.h"

static struct bk7258_storage_config_s g_config;
static struct bk7258_ota_layout_s g_config_ota_layout;
static struct bk7258_radio_storage_config_s g_config_radio_storage;
static struct bk7258_storage_region_s g_config_data_storage;
static bool g_config_ready;
static FAR const struct bk7258_ota_layout_s *g_ota_layout;
static uint32_t g_marker_address;
static mutex_t g_bk7258_storage_config_lock = NXMUTEX_INITIALIZER;
static mutex_t g_bk7258_storage_guard_lock = NXMUTEX_INITIALIZER;
static pid_t g_bk7258_storage_guard_pid = (pid_t)-1;
static enum bk7258_storage_guard_e g_bk7258_storage_guard_owner;
static bool g_bk7258_storage_guard_write;
static unsigned int g_bk7258_storage_guard_depth;

static FAR const struct bk7258_storage_config_s *
bk7258_storage_config(void)
{
  return __atomic_load_n(&g_config_ready, __ATOMIC_ACQUIRE) ?
         &g_config : NULL;
}

static bool bk7258_storage_range(uint32_t address, uint32_t size,
                                 uint32_t start, uint32_t length)
{
  uint32_t offset;

  if (size == 0u || address < start)
    {
      return false;
    }

  offset = address - start;
  return offset < length && size <= length - offset;
}

static bool bk7258_storage_guard_range(
  enum bk7258_storage_guard_e guard, uint32_t address, uint32_t size,
  FAR const struct bk7258_storage_config_s *config)
{
  FAR const struct bk7258_ota_layout_s *layout;
  FAR const struct bk7258_radio_storage_config_s *radio;
  FAR const struct bk7258_storage_region_s *data;
  FAR const struct bk7258_ota_storage_image_s *cp;
  FAR const struct bk7258_ota_storage_image_s *ap;
  enum bk7258_boot_slot_e slot;

  switch (guard)
    {
      case BK7258_STORAGE_GUARD_DATA:
        data = config->data_storage;
        return data != NULL &&
               bk7258_storage_range(address, size, data->start, data->size);

      case BK7258_STORAGE_GUARD_RADIO:
        radio = config->radio_storage;
        return radio != NULL &&
               (bk7258_storage_range(address, size, radio->backup.start,
                                     radio->backup.size) ||
                bk7258_storage_range(address, size, radio->network.start,
                                     radio->network.size));

      case BK7258_STORAGE_GUARD_OTA_STAGE_PRIMARY:
      case BK7258_STORAGE_GUARD_OTA_CONFIRM_PRIMARY:
        slot = BK7258_BOOT_SLOT_PRIMARY;
        break;

      case BK7258_STORAGE_GUARD_OTA_STAGE_SECONDARY:
      case BK7258_STORAGE_GUARD_OTA_CONFIRM_SECONDARY:
        slot = BK7258_BOOT_SLOT_SECONDARY;
        break;

      case BK7258_STORAGE_GUARD_RESET_MARKER:
        return bk7258_storage_range(address, size,
                                    config->reset_marker_address,
                                    config->reset_marker_erase_size);

      default:
        return false;
    }

  layout = config->ota_layout;
  if (layout == NULL)
    {
      return false;
    }

  cp = &layout->slot[slot][BK7258_OTA_IMAGE_CP];
  ap = &layout->slot[slot][BK7258_OTA_IMAGE_AP];
  if (guard == BK7258_STORAGE_GUARD_OTA_STAGE_PRIMARY ||
      guard == BK7258_STORAGE_GUARD_OTA_STAGE_SECONDARY)
    {
      return bk7258_storage_range(address, size, cp->raw_offset,
                                  cp->raw_size + ap->raw_size);
    }

  return bk7258_storage_range(address, size,
                              cp->raw_offset + cp->raw_size -
                                layout->erase_size,
                              layout->erase_size) ||
         bk7258_storage_range(address, size,
                              ap->raw_offset + ap->raw_size -
                                layout->erase_size,
                              layout->erase_size);
}

static int bk7258_storage_layout_validate(
  FAR const struct bk7258_ota_layout_s *layout)
{
  bool hash_present = false;
  uint32_t slot;
  uint32_t image;

  if (layout == NULL || layout->version != BK7258_OTA_LAYOUT_VERSION ||
      layout->size < sizeof(*layout) ||
      layout->erase_size != BK7258_OTA_ERASE_SIZE ||
      layout->crc_data_size != BK7258_OTA_CRC_DATA_SIZE ||
      layout->crc_total_size != BK7258_OTA_CRC_TOTAL_SIZE ||
      layout->remap.version != BK7258_BOOT_SLOT_MAP_VERSION ||
      layout->remap.size < sizeof(layout->remap) ||
      layout->remap.secondary_begin == 0u ||
      layout->remap.secondary_end <= layout->remap.secondary_begin ||
      layout->remap.secondary_offset == 0u)
    {
      return -EINVAL;
    }

  for (image = 0; image < BK7258_OTA_IMAGE_COUNT; image++)
    {
      if (layout->active_xip_start[image] == 0u ||
          layout->active_logical_size[image] == 0u ||
          layout->active_logical_size[image] % layout->crc_data_size != 0u)
        {
          return -EINVAL;
        }
    }

  if (layout->active_xip_start[BK7258_OTA_IMAGE_CP] >
        UINT32_MAX - layout->active_logical_size[BK7258_OTA_IMAGE_CP] ||
      layout->active_xip_start[BK7258_OTA_IMAGE_CP] +
        layout->active_logical_size[BK7258_OTA_IMAGE_CP] !=
      layout->active_xip_start[BK7258_OTA_IMAGE_AP])
    {
      return -EINVAL;
    }

  for (slot = 0; slot < BK7258_OTA_SLOT_COUNT; slot++)
    {
      for (image = 0; image < BK7258_OTA_IMAGE_COUNT; image++)
        {
          FAR const struct bk7258_ota_storage_image_s *storage =
            &layout->slot[slot][image];
          uint64_t expected =
            (uint64_t)layout->active_logical_size[image] /
            layout->crc_data_size * layout->crc_total_size;

          if (storage->raw_offset % layout->erase_size != 0u ||
              storage->raw_size == 0u ||
              storage->raw_size % layout->erase_size != 0u ||
              storage->raw_size != expected ||
              storage->raw_offset > UINT32_MAX - storage->raw_size)
            {
              return -EINVAL;
            }
        }

      if (layout->slot[slot][BK7258_OTA_IMAGE_CP].raw_offset +
            layout->slot[slot][BK7258_OTA_IMAGE_CP].raw_size !=
          layout->slot[slot][BK7258_OTA_IMAGE_AP].raw_offset)
        {
          return -EINVAL;
        }
    }

  if (layout->slot[BK7258_BOOT_SLOT_PRIMARY][BK7258_OTA_IMAGE_AP].raw_offset +
        layout->slot[BK7258_BOOT_SLOT_PRIMARY][BK7258_OTA_IMAGE_AP].raw_size >
      layout->slot[BK7258_BOOT_SLOT_SECONDARY][BK7258_OTA_IMAGE_CP].raw_offset)
    {
      return -EINVAL;
    }

  for (image = 0; image < sizeof(layout->layout_sha256); image++)
    {
      hash_present |= layout->layout_sha256[image] != 0u;
    }

  return hash_present ? 0 : -EINVAL;
}

int bk7258_storage_configure(
  FAR const struct bk7258_storage_config_s *config)
{
  int ret;

  if (config == NULL ||
      config->version != BK7258_STORAGE_CONFIG_VERSION ||
      config->size < sizeof(*config))
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_storage_config_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_config_ready)
    {
      /* Configuration is a one-shot boot-time handoff.  Once the chip-owned
       * copy is published, retain neither the board object's address nor any
       * callback into board code.
       */

      ret = -EALREADY;
    }
  else
    {
      g_config = *config;
      if (config->ota_layout != NULL)
        {
          g_config_ota_layout = *config->ota_layout;
          g_config.ota_layout = &g_config_ota_layout;
        }

      if (config->radio_storage != NULL)
        {
          g_config_radio_storage = *config->radio_storage;
          g_config.radio_storage = &g_config_radio_storage;
        }

      if (config->data_storage != NULL)
        {
          g_config_data_storage = *config->data_storage;
          g_config.data_storage = &g_config_data_storage;
        }

      __atomic_store_n(&g_config_ready, true, __ATOMIC_RELEASE);
      ret = 0;
    }

  nxmutex_unlock(&g_bk7258_storage_config_lock);
  return ret;
}

int bk7258_storage_ota_layout(
  FAR const struct bk7258_ota_layout_s **layout)
{
  FAR const struct bk7258_storage_config_s *config;
  FAR const struct bk7258_ota_layout_s *current;
  int ret;

  if (layout == NULL)
    {
      return -EINVAL;
    }

  current = __atomic_load_n(&g_ota_layout, __ATOMIC_ACQUIRE);
  if (current == NULL)
    {
      config = bk7258_storage_config();
      if (config == NULL)
        {
          return -EAGAIN;
        }

      current = config->ota_layout;
      ret = bk7258_storage_layout_validate(current);
      if (ret < 0)
        {
          return ret;
        }

      __atomic_store_n(&g_ota_layout, current, __ATOMIC_RELEASE);
    }

  *layout = current;
  return 0;
}

int bk7258_storage_marker_address(FAR uint32_t *address)
{
  FAR const struct bk7258_storage_config_s *config;
  uint32_t current;

  if (address == NULL)
    {
      return -EINVAL;
    }

  current = __atomic_load_n(&g_marker_address, __ATOMIC_ACQUIRE);
  if (current == 0u)
    {
      config = bk7258_storage_config();
      if (config == NULL)
        {
          return -EAGAIN;
        }

      current = config->reset_marker_address;
      if (current == 0u ||
          config->reset_marker_erase_size !=
            BK7258_RESET_MARKER_ERASE_SIZE ||
          current % config->reset_marker_erase_size != 0u)
        {
          return -EINVAL;
        }

      __atomic_store_n(&g_marker_address, current, __ATOMIC_RELEASE);
    }

  *address = current;
  return 0;
}

int bk7258_storage_radio_config(
  FAR const struct bk7258_radio_storage_config_s **radio)
{
  FAR const struct bk7258_storage_config_s *config;

  if (radio == NULL)
    {
      return -EINVAL;
    }

  config = bk7258_storage_config();
  if (config == NULL)
    {
      return -EAGAIN;
    }

  if (config->radio_storage == NULL)
    {
      return -ENOSYS;
    }

  *radio = config->radio_storage;
  return 0;
}

static int bk7258_storage_guard_validate(
  enum bk7258_storage_guard_e guard,
  FAR const struct bk7258_storage_config_s *config)
{
  FAR const struct bk7258_radio_storage_config_s *radio;
  FAR const struct bk7258_storage_region_s *data;
  FAR const struct bk7258_ota_layout_s *layout;
  uint32_t marker;

  switch (guard)
    {
      case BK7258_STORAGE_GUARD_DATA:
        data = config->data_storage;
        return data != NULL && data->size != 0u &&
               data->start <= UINT32_MAX - data->size ? 0 : -EINVAL;

      case BK7258_STORAGE_GUARD_RADIO:
        radio = config->radio_storage;
        if (radio == NULL ||
            radio->version != BK7258_RADIO_STORAGE_CONFIG_VERSION ||
            radio->size < sizeof(*radio) ||
            radio->backup.size == 0u || radio->network.size == 0u ||
            radio->backup.start > UINT32_MAX - radio->backup.size ||
            radio->network.start > UINT32_MAX - radio->network.size)
          {
            return -EINVAL;
          }

        return 0;

      case BK7258_STORAGE_GUARD_OTA_STAGE_PRIMARY:
      case BK7258_STORAGE_GUARD_OTA_STAGE_SECONDARY:
      case BK7258_STORAGE_GUARD_OTA_CONFIRM_PRIMARY:
      case BK7258_STORAGE_GUARD_OTA_CONFIRM_SECONDARY:
        return bk7258_storage_ota_layout(&layout);

      case BK7258_STORAGE_GUARD_RESET_MARKER:
        return bk7258_storage_marker_address(&marker);

      default:
        return -EINVAL;
    }
}

int bk7258_storage_lock(enum bk7258_storage_guard_e guard,
                        uint32_t timeout_ms)
{
  return bk7258_storage_guard_lock(guard, true, timeout_ms);
}

void bk7258_storage_unlock(void)
{
  bk7258_storage_guard_unlock();
}

int bk7258_storage_guard_lock(enum bk7258_storage_guard_e guard,
                              bool write_access, uint32_t timeout_ms)
{
  FAR const struct bk7258_storage_config_s *config =
    bk7258_storage_config();
  pid_t pid;
  int ret;

  if (config == NULL)
    {
      return -EAGAIN;
    }

  if (up_interrupt_context() || guard < BK7258_STORAGE_GUARD_DATA ||
      guard >= BK7258_STORAGE_GUARD_COUNT)
    {
      return -EINVAL;
    }

  ret = bk7258_storage_guard_validate(guard, config);
  if (ret < 0)
    {
      return ret;
    }

  pid = nxsched_gettid();
  if (__atomic_load_n(&g_bk7258_storage_guard_depth, __ATOMIC_ACQUIRE) != 0u &&
      __atomic_load_n(&g_bk7258_storage_guard_pid, __ATOMIC_RELAXED) == pid)
    {
      if (__atomic_load_n(&g_bk7258_storage_guard_owner,
                          __ATOMIC_RELAXED) != guard ||
          (write_access &&
           !__atomic_load_n(&g_bk7258_storage_guard_write,
                            __ATOMIC_RELAXED)))
        {
          return -EDEADLK;
        }

      __atomic_add_fetch(&g_bk7258_storage_guard_depth, 1u,
                         __ATOMIC_RELEASE);
      return 0;
    }

  ret = timeout_ms == 0u ?
        nxmutex_lock(&g_bk7258_storage_guard_lock) :
        nxmutex_timedlock(&g_bk7258_storage_guard_lock, timeout_ms);
  if (ret < 0)
    {
      return ret;
    }

  __atomic_store_n(&g_bk7258_storage_guard_owner, guard,
                   __ATOMIC_RELAXED);
  __atomic_store_n(&g_bk7258_storage_guard_write, write_access,
                   __ATOMIC_RELAXED);
  __atomic_store_n(&g_bk7258_storage_guard_pid, pid, __ATOMIC_RELAXED);
  __atomic_store_n(&g_bk7258_storage_guard_depth, 1u, __ATOMIC_RELEASE);
  return 0;
}

void bk7258_storage_guard_unlock(void)
{
  unsigned int depth =
    __atomic_load_n(&g_bk7258_storage_guard_depth, __ATOMIC_ACQUIRE);

  DEBUGASSERT(depth != 0u);
  DEBUGASSERT(__atomic_load_n(&g_bk7258_storage_guard_pid,
                              __ATOMIC_RELAXED) == nxsched_gettid());
  if (depth == 0u ||
      __atomic_load_n(&g_bk7258_storage_guard_pid,
                      __ATOMIC_RELAXED) != nxsched_gettid())
    {
      return;
    }

  if (depth > 1u)
    {
      __atomic_sub_fetch(&g_bk7258_storage_guard_depth, 1u,
                         __ATOMIC_RELEASE);
      return;
    }

  __atomic_store_n(&g_bk7258_storage_guard_depth, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&g_bk7258_storage_guard_pid, (pid_t)-1,
                   __ATOMIC_RELAXED);
  __atomic_store_n(&g_bk7258_storage_guard_write, false,
                   __ATOMIC_RELAXED);
  nxmutex_unlock(&g_bk7258_storage_guard_lock);
}

bool bk7258_storage_guard_write_authorized(uint32_t address, uint32_t size)
{
  FAR const struct bk7258_storage_config_s *config =
    bk7258_storage_config();

  return config != NULL && !up_interrupt_context() &&
         __atomic_load_n(&g_bk7258_storage_guard_depth,
                         __ATOMIC_ACQUIRE) != 0u &&
         __atomic_load_n(&g_bk7258_storage_guard_write,
                         __ATOMIC_RELAXED) &&
         __atomic_load_n(&g_bk7258_storage_guard_pid,
                         __ATOMIC_RELAXED) == nxsched_gettid() &&
         bk7258_storage_guard_range(
           __atomic_load_n(&g_bk7258_storage_guard_owner,
                           __ATOMIC_RELAXED),
           address, size, config);
}
