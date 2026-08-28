/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/cp/
 * bk7258_ota_boot_control.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MCUboot pair geometry and health-confirmation adapter.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <arch/chip/bk7258_boot_slot.h>
#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_flash.h>
#include <arch/chip/bk7258_ota.h>
#include <arch/chip/bk7258_reset_cause.h>
#include <arch/chip/bk7258_system_reset.h>

#include "bk7258_ota_engine_internal.h"
#include "bk7258_ota_flash_internal.h"
#include "bk7258_ota_image.h"
#include "bk7258_storage_internal.h"
#include "bk7258_wdt.h"

#define BK7258_OTA_CONFIRM_LOCK_TIMEOUT_MS 5000u

static const uint8_t
  g_bk7258_ota_magic[BK7258_MCUBOOT_TRAILER_MAGIC_SIZE] =
    BK7258_MCUBOOT_TRAILER_MAGIC_INIT;

static uint8_t g_bk7258_ota_confirm_sector[BK7258_OTA_ERASE_SIZE];

struct bk7258_ota_trailer_s
{
  uint8_t copy_done;
  uint8_t image_ok;
};

int bk7258_ota_resolve_layout(
  FAR const struct bk7258_ota_layout_s **layout,
  FAR enum bk7258_boot_slot_e *active_slot)
{
  int ret;

  if (layout == NULL || active_slot == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_storage_ota_layout(layout);
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_boot_active_slot(&(*layout)->remap, active_slot);
}

int bk7258_ota_inactive_geometry(struct bk7258_ota_geometry_s *geometry)
{
  const struct bk7258_ota_layout_s *layout;
  enum bk7258_boot_slot_e active;
  enum bk7258_boot_slot_e inactive;
  int ret;

  if (geometry == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_ota_resolve_layout(&layout, &active);
  if (ret < 0)
    {
      return ret;
    }

  inactive = active == BK7258_BOOT_SLOT_PRIMARY ?
             BK7258_BOOT_SLOT_SECONDARY : BK7258_BOOT_SLOT_PRIMARY;
  geometry->active_slot = active;
  geometry->inactive_slot = inactive;
  geometry->cp_raw_offset =
    layout->slot[inactive][BK7258_OTA_IMAGE_CP].raw_offset;
  geometry->cp_raw_size =
    layout->slot[inactive][BK7258_OTA_IMAGE_CP].raw_size;
  geometry->ap_raw_offset =
    layout->slot[inactive][BK7258_OTA_IMAGE_AP].raw_offset;
  geometry->ap_raw_size =
    layout->slot[inactive][BK7258_OTA_IMAGE_AP].raw_size;
  return 0;
}

static int bk7258_ota_trailer(uint32_t xip, uint32_t size,
                             struct bk7258_ota_trailer_s *trailer)
{
  const volatile uint8_t *image =
    (const volatile uint8_t *)(uintptr_t)xip;
  uint32_t index;

  if (trailer == NULL ||
      size <= BK7258_MCUBOOT_TRAILER_MAGIC_SIZE +
              2u * BK7258_MCUBOOT_TRAILER_ALIGN)
    {
      return -EINVAL;
    }

  for (index = 0; index < sizeof(g_bk7258_ota_magic); index++)
    {
      if (image[size - sizeof(g_bk7258_ota_magic) + index] !=
          g_bk7258_ota_magic[index])
        {
          return -EILSEQ;
        }
    }

  trailer->copy_done =
    image[BK7258_MCUBOOT_COPY_DONE_OFFSET(size)];
  trailer->image_ok = image[BK7258_MCUBOOT_IMAGE_OK_OFFSET(size)];
  return 0;
}

static int bk7258_ota_confirm_image(uint32_t raw_base, uint32_t raw_size,
                                    uint32_t logical_size,
                                    bool *mutation_started)
{
  uint32_t logical_offset =
    BK7258_MCUBOOT_IMAGE_OK_OFFSET(logical_size);
  uint32_t group = logical_offset / BK7258_OTA_CRC_DATA_SIZE;
  uint32_t in_group = logical_offset % BK7258_OTA_CRC_DATA_SIZE;
  uint32_t raw_offset = group * BK7258_OTA_CRC_TOTAL_SIZE;
  uint32_t sector = raw_base + raw_size - BK7258_OTA_ERASE_SIZE;
  uint32_t packet = raw_base + raw_offset - sector;
  uint8_t *data;
  uint16_t crc;

  if (raw_offset + BK7258_OTA_CRC_TOTAL_SIZE > raw_size ||
      packet + BK7258_OTA_CRC_TOTAL_SIZE >
        sizeof(g_bk7258_ota_confirm_sector) ||
      bk7258_flash_read(
        sector, g_bk7258_ota_confirm_sector,
        sizeof(g_bk7258_ota_confirm_sector)) < 0)
    {
      return -EIO;
    }

  data = &g_bk7258_ota_confirm_sector[packet];
  crc = ((uint16_t)data[BK7258_OTA_CRC_DATA_SIZE] << 8) |
        data[BK7258_OTA_CRC_DATA_SIZE + 1u];
  if (crc != bk7258_ota_flash_crc16(data))
    {
      return -EILSEQ;
    }

  if (data[in_group] == 1u)
    {
      return 0;
    }
  if (data[in_group] != 0xffu)
    {
      return -EILSEQ;
    }

  data[in_group] = 1u;
  crc = bk7258_ota_flash_crc16(data);
  data[BK7258_OTA_CRC_DATA_SIZE] = (uint8_t)(crc >> 8);
  data[BK7258_OTA_CRC_DATA_SIZE + 1u] = (uint8_t)crc;

  *mutation_started = true;
  if (bk7258_flash_erase_sector(sector) < 0 ||
      bk7258_flash_write(
        sector, g_bk7258_ota_confirm_sector,
        sizeof(g_bk7258_ota_confirm_sector)) < 0)
    {
      return -EIO;
    }

  return bk7258_ota_flash_verify(sector, g_bk7258_ota_confirm_sector,
                                 sizeof(g_bk7258_ota_confirm_sector));
}

int bk7258_ota_get_active_pair(struct bk7258_ota_pair_snapshot_s *snapshot)
{
  struct bk7258_ota_image_metadata_s cp = {0};
  struct bk7258_ota_image_metadata_s ap = {0};
  const struct bk7258_ota_layout_s *layout;
  const struct bk7258_ota_layout_s *verified_layout;
  struct bk7258_ota_trailer_s cp_trailer = {0};
  struct bk7258_ota_trailer_s ap_trailer = {0};
  enum bk7258_boot_slot_e active;
  enum bk7258_boot_slot_e verified_active = BK7258_BOOT_SLOT_INVALID;
  int ret;

  if (snapshot == NULL)
    {
      return -EINVAL;
    }

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->state = BK7258_OTA_PAIR_INVALID;
  ret = bk7258_ota_resolve_layout(&layout, &active);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ota_xip_image_metadata(
          layout->active_xip_start[BK7258_OTA_IMAGE_CP],
          layout->active_logical_size[BK7258_OTA_IMAGE_CP], &cp);
  if (ret == 0)
    {
      ret = bk7258_ota_xip_image_metadata(
              layout->active_xip_start[BK7258_OTA_IMAGE_AP],
              layout->active_logical_size[BK7258_OTA_IMAGE_AP], &ap);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_trailer(
                               layout->active_xip_start[BK7258_OTA_IMAGE_CP],
                               layout->active_logical_size[BK7258_OTA_IMAGE_CP],
                               &cp_trailer);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_trailer(
                               layout->active_xip_start[BK7258_OTA_IMAGE_AP],
                               layout->active_logical_size[BK7258_OTA_IMAGE_AP],
                               &ap_trailer);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_resolve_layout(&verified_layout, &verified_active);
    }
  if (ret < 0 || verified_active != active || verified_layout != layout ||
      !bk7258_mcuboot_version_equal(&cp.version, &ap.version) ||
      cp.security_counter_present != ap.security_counter_present ||
      cp.security_counter != ap.security_counter ||
      cp_trailer.copy_done != 1u || ap_trailer.copy_done != 1u ||
      cp_trailer.image_ok != ap_trailer.image_ok)
    {
      return ret < 0 ? ret : -EILSEQ;
    }

  if (cp_trailer.image_ok == 0xffu)
    {
      snapshot->state = BK7258_OTA_PAIR_PENDING;
    }
  else if (cp_trailer.image_ok == 1u)
    {
      snapshot->state = BK7258_OTA_PAIR_CONFIRMED;
    }
  else
    {
      return -EILSEQ;
    }

  snapshot->active_slot = active;
  snapshot->version = cp.version;
  snapshot->security_counter = cp.security_counter;
  snapshot->security_counter_present = cp.security_counter_present;
  return 0;
}

static bool bk7258_ota_pair_snapshot_equal(
  const struct bk7258_ota_pair_snapshot_s *left,
  const struct bk7258_ota_pair_snapshot_s *right)
{
  return left->active_slot == right->active_slot &&
         left->state == right->state &&
         left->security_counter_present ==
           right->security_counter_present &&
         left->security_counter == right->security_counter &&
         bk7258_mcuboot_version_equal(&left->version, &right->version);
}

static int bk7258_ota_confirm_health(
  const struct bk7258_ap_supervisor_health_token_s *expected,
  uint32_t max_age_ms)
{
#ifdef CONFIG_BK7258_AP_SUPERVISOR
  volatile struct bk7258_ap_boot_state_s *boot = bk7258_ap_boot_state();
  struct bk7258_ap_supervisor_health_token_s current;
  uint32_t generation;
  uint32_t state;
  int ret;

  if (expected == NULL)
    {
      return OK;
    }

  if (max_age_ms == 0u)
    {
      return -EINVAL;
    }

  memset(&current, 0, sizeof(current));
  ret = bk7258_ap_supervisor_health_token(
          expected->generation, max_age_ms, &current);
  if (ret < 0 || current.sample_sequence < expected->sample_sequence)
    {
      return ret < 0 ? ret : -ESTALE;
    }

  generation = __atomic_load_n(
                 (uint32_t *)(uintptr_t)&boot->generation,
                 __ATOMIC_ACQUIRE);
  state = __atomic_load_n((uint32_t *)(uintptr_t)&boot->state,
                          __ATOMIC_ACQUIRE);
  return generation == expected->generation &&
         state == BK7258_AP_STATE_READY ? OK : -ESTALE;
#else
  (void)max_age_ms;
  return expected == NULL ? OK : -ENOSYS;
#endif
}

static int bk7258_ota_confirm_pair_internal(
  const struct bk7258_ota_pair_snapshot_s *expected,
  const struct bk7258_ap_supervisor_health_token_s *health,
  uint32_t health_max_age_ms)
{
  struct bk7258_ota_pair_snapshot_s current;
  const struct bk7258_ota_layout_s *layout;
  const struct bk7258_ota_layout_s *locked_layout;
  enum bk7258_boot_slot_e active;
  enum bk7258_boot_slot_e locked_active = BK7258_BOOT_SLOT_INVALID;
  uint32_t cp_raw = 0u;
  uint32_t ap_raw = 0u;
  bool mutation_started = false;
  int ret;

  if (expected == NULL || expected->state != BK7258_OTA_PAIR_PENDING)
    {
      return -EINVAL;
    }

  if (expected->active_slot != BK7258_BOOT_SLOT_PRIMARY &&
      expected->active_slot != BK7258_BOOT_SLOT_SECONDARY)
    {
      return -EIO;
    }

  ret = bk7258_ota_resolve_layout(&layout, &active);
  if (ret < 0)
    {
      return ret;
    }
  if (active != expected->active_slot)
    {
      return -ESTALE;
    }

  ret = bk7258_storage_lock(
          expected->active_slot == BK7258_BOOT_SLOT_PRIMARY ?
            BK7258_STORAGE_GUARD_OTA_CONFIRM_PRIMARY :
            BK7258_STORAGE_GUARD_OTA_CONFIRM_SECONDARY,
          BK7258_OTA_CONFIRM_LOCK_TIMEOUT_MS);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ota_resolve_layout(&locked_layout, &locked_active);
  if (ret == 0 &&
      locked_active != active)
    {
      ret = -ESTALE;
    }

  if (ret == 0)
    {
      cp_raw = locked_layout->slot[expected->active_slot]
                                    [BK7258_OTA_IMAGE_CP].raw_offset;
      ap_raw = locked_layout->slot[expected->active_slot]
                                    [BK7258_OTA_IMAGE_AP].raw_offset;
      ret = bk7258_ota_get_active_pair(&current);
    }
  if (ret == 0 && current.state == BK7258_OTA_PAIR_CONFIRMED &&
      current.active_slot == expected->active_slot &&
      current.security_counter_present ==
        expected->security_counter_present &&
      current.security_counter == expected->security_counter &&
      bk7258_mcuboot_version_equal(&current.version, &expected->version))
    {
      ret = -EALREADY;
    }
  else if (ret == 0 && !bk7258_ota_pair_snapshot_equal(&current, expected))
    {
      ret = -ESTALE;
    }

  if (ret == 0)
    {
      ret = bk7258_ota_flash_initialize();
    }
  if (ret == 0)
    {
      ret = bk7258_ota_confirm_health(health, health_max_age_ms);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_confirm_image(
        ap_raw,
        locked_layout->slot[expected->active_slot]
                           [BK7258_OTA_IMAGE_AP].raw_size,
        locked_layout->active_logical_size[BK7258_OTA_IMAGE_AP],
                                     &mutation_started);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_confirm_health(health, health_max_age_ms);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_confirm_image(
        cp_raw,
        locked_layout->slot[expected->active_slot]
                           [BK7258_OTA_IMAGE_CP].raw_size,
        locked_layout->active_logical_size[BK7258_OTA_IMAGE_CP],
                                     &mutation_started);
    }

  bk7258_storage_unlock();
  if (ret < 0 && mutation_started)
    {
      /* A failed sector RMW may leave AP/CP image_ok mismatched.  Reset the
       * whole SoC immediately so BL2 can select the still-confirmed pair. */

      bk7258_ota_system_reset();
    }

  return ret;
}

int bk7258_ota_confirm_pair(
  const struct bk7258_ota_pair_snapshot_s *expected)
{
  return bk7258_ota_confirm_pair_internal(expected, NULL, 0u);
}

int bk7258_ota_confirm_pair_health(
  const struct bk7258_ota_pair_snapshot_s *expected,
  const struct bk7258_ap_supervisor_health_token_s *health,
  uint32_t health_max_age_ms)
{
  if (health == NULL || health_max_age_ms == 0u)
    {
      return -EINVAL;
    }

#ifndef CONFIG_BK7258_AP_SUPERVISOR
  (void)expected;
  return -ENOSYS;
#else
  return bk7258_ota_confirm_pair_internal(
           expected, health, health_max_age_ms);
#endif
}

void bk7258_ota_system_reset(void)
{
  bk7258_system_reset(BK7258_RESET_SOURCE_REBOOT);
}
