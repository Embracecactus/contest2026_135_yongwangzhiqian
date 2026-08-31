/****************************************************************************
 * chips/bk7258/cp/bk7258_ota_pair.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Transport-neutral, pair-atomic CP/AP installer for authenticated sources.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <crypto/sha2.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_flash.h>
#include <arch/chip/bk7258_ota.h>

#include "bk7258_ota_engine_internal.h"
#include "bk7258_ota_flash_internal.h"
#include "bk7258_ota_image.h"
#include "bk7258_storage_internal.h"
#ifdef CONFIG_BK7258_WDT
#  include "bk7258_wdt.h"
#endif

#define BK7258_OTA_PROGRESS_GRANULARITY (64u * 1024u)
#define BK7258_OTA_STAGE_LOCK_TIMEOUT_MS 5000u

static uint8_t g_bk7258_ota_write_sector[BK7258_OTA_ERASE_SIZE];
static uint8_t g_bk7258_ota_cp_commit[BK7258_OTA_ERASE_SIZE];

static int bk7258_ota_service_runtime(void)
{
#ifdef CONFIG_BK7258_WDT
  int ret = bk7258_wdt_service();

  if (ret < 0)
    {
      return ret;
    }
#endif

  (void)nxsig_usleep(1000);
  return 0;
}

static int bk7258_ota_checkpoint(
  const struct bk7258_ota_source_ops_s *ops, void *context,
  enum bk7258_ota_phase_e phase, enum bk7258_ota_image_e image,
  uint32_t completed, uint32_t total)
{
  struct bk7258_ota_progress_s progress;

  if (ops->checkpoint == NULL)
    {
      return 0;
    }

  progress.phase = phase;
  progress.image = image;
  progress.completed = completed;
  progress.total = total;
  return ops->checkpoint(context, &progress);
}

static int bk7258_ota_source_read(
  const struct bk7258_ota_source_ops_s *ops, void *context,
  enum bk7258_ota_image_e image, uint32_t offset, uint8_t *buffer,
  size_t nbytes)
{
  int ret = ops->read_at(context, image, offset, buffer, nbytes);

  return ret == 0 ? 0 : (ret < 0 ? ret : -EIO);
}

static int bk7258_ota_erase(
  const struct bk7258_ota_source_ops_s *ops, void *context,
  enum bk7258_ota_phase_e phase, enum bk7258_ota_image_e image,
  uint32_t base, uint32_t size, uint32_t erase_size)
{
  uint32_t offset;
  int ret;

  ret = bk7258_ota_checkpoint(ops, context, phase, image, 0u, size);
  if (ret < 0)
    {
      return ret;
    }

  for (offset = 0; offset < size; offset += erase_size)
    {
      if (bk7258_flash_erase_sector(base + offset) < 0)
        {
          return -EIO;
        }

      ret = bk7258_ota_service_runtime();
      if (ret < 0)
        {
          return ret;
        }

      if (offset + erase_size == size ||
          (offset + erase_size) %
            BK7258_OTA_PROGRESS_GRANULARITY == 0u)
        {
          ret = bk7258_ota_checkpoint(
                  ops, context, phase, image,
                  offset + erase_size, size);
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  return 0;
}

static int bk7258_ota_program_image(
  const struct bk7258_ota_source_ops_s *ops, void *context,
  enum bk7258_ota_phase_e phase, enum bk7258_ota_image_e image,
  uint32_t base, uint32_t size, uint32_t first_offset,
  uint32_t erase_size, SHA2_CTX *sha256)
{
  uint32_t offset;
  int ret;

  ret = bk7258_ota_checkpoint(ops, context, phase, image,
                              first_offset, size);
  if (ret < 0)
    {
      return ret;
    }

  for (offset = first_offset; offset < size;
       offset += erase_size)
    {
      ret = bk7258_ota_source_read(ops, context, image, offset,
                                   g_bk7258_ota_write_sector,
                                   sizeof(g_bk7258_ota_write_sector));
      if (ret < 0)
        {
          return ret;
        }

      sha256update(sha256, g_bk7258_ota_write_sector,
                   sizeof(g_bk7258_ota_write_sector));
      if (bk7258_flash_write(
            base + offset, g_bk7258_ota_write_sector,
            sizeof(g_bk7258_ota_write_sector)) < 0 ||
          bk7258_ota_flash_verify(base + offset,
                                  g_bk7258_ota_write_sector,
                                  sizeof(g_bk7258_ota_write_sector)) < 0)
        {
          return -EIO;
        }

      ret = bk7258_ota_service_runtime();
      if (ret < 0)
        {
          return ret;
        }

      if (offset + erase_size == size ||
          (offset + erase_size) %
            BK7258_OTA_PROGRESS_GRANULARITY == 0u)
        {
          ret = bk7258_ota_checkpoint(
                  ops, context, phase, image,
                  offset + erase_size, size);
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  return 0;
}

static int bk7258_ota_validate_manifest(
  const struct bk7258_ota_manifest_s *manifest,
  const struct bk7258_ota_layout_s *layout)
{
  bool package_id_present = false;
  size_t index;

  if (manifest->version != BK7258_OTA_MANIFEST_VERSION ||
      memcmp(manifest->layout_sha256, layout->layout_sha256,
             sizeof(layout->layout_sha256)) != 0 ||
      manifest->security_counter == 0u ||
      manifest->image[BK7258_OTA_IMAGE_CP].physical_size !=
        layout->slot[BK7258_BOOT_SLOT_PRIMARY]
                    [BK7258_OTA_IMAGE_CP].raw_size ||
      manifest->image[BK7258_OTA_IMAGE_AP].physical_size !=
        layout->slot[BK7258_BOOT_SLOT_PRIMARY]
                    [BK7258_OTA_IMAGE_AP].raw_size)
    {
      return -EINVAL;
    }

  for (index = 0u; index < sizeof(manifest->package_id); index++)
    {
      package_id_present |= manifest->package_id[index] != 0u;
    }

  return package_id_present ? 0 : -EINVAL;
}

static int bk7258_ota_admit_candidate(
  const struct bk7258_ota_source_ops_s *ops, void *context,
  const struct bk7258_ota_manifest_s *manifest,
  const struct bk7258_ota_pair_snapshot_s *active,
  const struct bk7258_ota_layout_s *layout)
{
  struct bk7258_ota_image_metadata_s cp;
  struct bk7258_ota_image_metadata_s ap;
  int ret;

  ret = bk7258_ota_source_image_metadata(
          ops, context, BK7258_OTA_IMAGE_CP,
          manifest->image[BK7258_OTA_IMAGE_CP].physical_size,
          layout->active_logical_size[BK7258_OTA_IMAGE_CP], &cp);
  if (ret == 0)
    {
      ret = bk7258_ota_source_image_metadata(
              ops, context, BK7258_OTA_IMAGE_AP,
              manifest->image[BK7258_OTA_IMAGE_AP].physical_size,
              layout->active_logical_size[BK7258_OTA_IMAGE_AP], &ap);
    }
  if (ret < 0)
    {
      return ret;
    }

  if (!bk7258_mcuboot_version_equal(&cp.version, &ap.version) ||
      !bk7258_mcuboot_version_equal(&cp.version,
                                    &manifest->image_version) ||
      !cp.security_counter_present || !ap.security_counter_present ||
      cp.security_counter != ap.security_counter ||
      cp.security_counter != manifest->security_counter)
    {
      return -EILSEQ;
    }

  if (bk7258_mcuboot_version_compare(&cp.version, &active->version) <= 0 ||
      cp.security_counter <= active->security_counter)
    {
      return -EPERM;
    }

  return 0;
}

int bk7258_ota_stage_pair(const struct bk7258_ota_source_ops_s *ops,
                          void *context)
{
  struct bk7258_ota_geometry_s geometry;
  const struct bk7258_ota_layout_s *layout;
  const struct bk7258_ota_layout_s *locked_layout;
  struct bk7258_ota_manifest_s manifest;
  struct bk7258_ota_pair_snapshot_s active;
  enum bk7258_boot_slot_e active_slot;
  enum bk7258_boot_slot_e locked_active_slot;
  SHA2_CTX sha256;
  uint8_t digest[BK7258_OTA_SHA256_SIZE];
  bool opened = false;
  int ret;

  if (ops == NULL || ops->open == NULL || ops->read_at == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_ota_resolve_layout(&layout, &active_slot);
  if (ret < 0)
    {
      return ret;
    }

  geometry.active_slot = active_slot;
  geometry.inactive_slot = active_slot == BK7258_BOOT_SLOT_PRIMARY ?
                           BK7258_BOOT_SLOT_SECONDARY :
                           BK7258_BOOT_SLOT_PRIMARY;
  geometry.cp_raw_offset =
    layout->slot[geometry.inactive_slot][BK7258_OTA_IMAGE_CP].raw_offset;
  geometry.cp_raw_size =
    layout->slot[geometry.inactive_slot][BK7258_OTA_IMAGE_CP].raw_size;
  geometry.ap_raw_offset =
    layout->slot[geometry.inactive_slot][BK7258_OTA_IMAGE_AP].raw_offset;
  geometry.ap_raw_size =
    layout->slot[geometry.inactive_slot][BK7258_OTA_IMAGE_AP].raw_size;

  ret = bk7258_storage_lock(
          geometry.inactive_slot == BK7258_BOOT_SLOT_PRIMARY ?
            BK7258_STORAGE_GUARD_OTA_STAGE_PRIMARY :
            BK7258_STORAGE_GUARD_OTA_STAGE_SECONDARY,
          BK7258_OTA_STAGE_LOCK_TIMEOUT_MS);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ota_resolve_layout(&locked_layout, &locked_active_slot);
  if (ret == 0 &&
      locked_active_slot != active_slot)
    {
      ret = -ESTALE;
    }
  if (ret == 0)
    {
      ret = bk7258_ota_get_active_pair(&active);
    }
  if (ret == 0 && active.active_slot != geometry.active_slot)
    {
      ret = -ESTALE;
    }
  if (ret == 0 && active.state != BK7258_OTA_PAIR_CONFIRMED)
    {
      ret = active.state == BK7258_OTA_PAIR_PENDING ? -EBUSY : -EPERM;
    }

  memset(&manifest, 0, sizeof(manifest));
  if (ret == 0)
    {
      opened = true;
      ret = ops->open(context, &manifest);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_validate_manifest(&manifest, locked_layout);
    }
  else if (ret > 0)
    {
      ret = -EIO;
    }

  if (ret == 0)
    {
      ret = bk7258_ota_admit_candidate(ops, context, &manifest, &active,
                                       locked_layout);
    }

  if (ret == 0)
    {
      ret = bk7258_ota_checkpoint(ops, context, BK7258_OTA_PHASE_PREPARE,
                                  BK7258_OTA_IMAGE_CP, 0u,
                                  locked_layout->erase_size);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_source_read(ops, context, BK7258_OTA_IMAGE_CP, 0u,
                                   g_bk7258_ota_cp_commit,
                                   sizeof(g_bk7258_ota_cp_commit));
    }
  if (ret == 0)
    {
      ret = bk7258_ota_checkpoint(ops, context, BK7258_OTA_PHASE_PREPARE,
                                  BK7258_OTA_IMAGE_CP,
                                  locked_layout->erase_size,
                                  locked_layout->erase_size);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_flash_initialize();
    }
  if (ret == 0)
    {
      ret = bk7258_ota_erase(ops, context, BK7258_OTA_PHASE_ERASE_CP,
                             BK7258_OTA_IMAGE_CP, geometry.cp_raw_offset,
                             geometry.cp_raw_size,
                             locked_layout->erase_size);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_erase(ops, context, BK7258_OTA_PHASE_ERASE_AP,
                             BK7258_OTA_IMAGE_AP, geometry.ap_raw_offset,
                             geometry.ap_raw_size,
                             locked_layout->erase_size);
    }
  if (ret == 0)
    {
      sha256init(&sha256);
      ret = bk7258_ota_program_image(ops, context,
                                     BK7258_OTA_PHASE_WRITE_AP,
                                     BK7258_OTA_IMAGE_AP,
                                     geometry.ap_raw_offset,
                                     geometry.ap_raw_size, 0u,
                                     locked_layout->erase_size, &sha256);
    }
  if (ret == 0)
    {
      sha256final(digest, &sha256);
      if (memcmp(digest,
                 manifest.image[BK7258_OTA_IMAGE_AP].sha256,
                 sizeof(digest)) != 0)
        {
          ret = -EILSEQ;
        }
    }
  if (ret == 0)
    {
      sha256init(&sha256);
      sha256update(&sha256, g_bk7258_ota_cp_commit,
                   sizeof(g_bk7258_ota_cp_commit));
      ret = bk7258_ota_program_image(ops, context,
                                     BK7258_OTA_PHASE_WRITE_CP,
                                     BK7258_OTA_IMAGE_CP,
                                     geometry.cp_raw_offset,
                                     geometry.cp_raw_size,
                                     locked_layout->erase_size,
                                     locked_layout->erase_size, &sha256);
    }
  if (ret == 0)
    {
      sha256final(digest, &sha256);
      if (memcmp(digest,
                 manifest.image[BK7258_OTA_IMAGE_CP].sha256,
                 sizeof(digest)) != 0)
        {
          ret = -EILSEQ;
        }
    }
  if (ret == 0)
    {
      ret = bk7258_ota_checkpoint(ops, context,
                                  BK7258_OTA_PHASE_COMMIT_CP,
                                  BK7258_OTA_IMAGE_CP, 0u,
                                  locked_layout->erase_size);
    }
  if (ret == 0 &&
      (bk7258_flash_write(geometry.cp_raw_offset,
                          g_bk7258_ota_cp_commit,
                          sizeof(g_bk7258_ota_cp_commit)) < 0 ||
       bk7258_ota_flash_verify(geometry.cp_raw_offset,
                               g_bk7258_ota_cp_commit,
                               sizeof(g_bk7258_ota_cp_commit)) < 0))
    {
      ret = -EIO;
    }
  if (ret == 0)
    {
      /* The pair is boot-eligible now; notification failures cannot undo it. */

      (void)bk7258_ota_checkpoint(ops, context,
                                  BK7258_OTA_PHASE_COMMIT_CP,
                                  BK7258_OTA_IMAGE_CP,
                                  locked_layout->erase_size,
                                  locked_layout->erase_size);
      (void)bk7258_ota_checkpoint(ops, context,
                                  BK7258_OTA_PHASE_COMPLETE,
                                  BK7258_OTA_IMAGE_CP, 1u, 1u);
    }

  bk7258_storage_unlock();
  if (ret < 0 && opened && ops->cancel != NULL)
    {
      (void)ops->cancel(context);
    }
  if (opened && ops->close != NULL)
    {
      ops->close(context);
    }

  return ret;
}
