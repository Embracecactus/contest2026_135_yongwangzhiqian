/*
 * bk7258_ota_n17_publish.c - CP-side normal format-3 OTA publication.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/mtd/mtd.h>

#include <crypto/sha2.h>

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_ota_n17_publish.h>

#include "../../bootloader/boot_n17_journal_core.h"
#include "bk7258_flash_guard.h"
#include "bk7258_flash_mtd.h"

#define BK7258_FLASH_REMAP_ENABLE 0x44030064u
#define BK7258_OTA_N17_HASH_CHUNK 256u
#define BK7258_OTA_N17_PROGRAM_CHUNK 32u
#define BK7258_OTA_N17_SLOT_COUNT 2u
#define BK7258_REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

enum bk7258_ota_n17_lifecycle_action_e
{
  BK7258_OTA_N17_LIFECYCLE_TRIAL,
  BK7258_OTA_N17_LIFECYCLE_CONFIRM,
  BK7258_OTA_N17_LIFECYCLE_ROLLBACK
};

static volatile bool g_bk7258_ota_n17_publish_initialized;
static volatile bool g_bk7258_ota_n17_publish_runtime_write;
static volatile bool g_bk7258_ota_n17_publish_active;

static int bk7258_ota_n17_sha256(void *arg, const uint8_t *data, size_t len,
                                  uint8_t digest[32])
{
  SHA2_CTX context;

  (void)arg;
  sha256init(&context);
  sha256update(&context, data, len);
  sha256final(digest, &context);
  return 0;
}

static bool bk7258_ota_n17_erased(const uint8_t *data, size_t length)
{
  while (length-- != 0)
    {
      if (*data++ != 0xffu)
        {
          return false;
        }
    }

  return true;
}

static uint8_t bk7258_ota_n17_active_slot(void)
{
  return (BK7258_REG32(BK7258_FLASH_REMAP_ENABLE) & 1u) != 0 ? 1u : 0u;
}

static enum bk7258_ota_n17_mtd_region_e
bk7258_ota_n17_manifest_region(uint8_t slot)
{
  return slot == 0 ? BK7258_OTA_N17_MTD_MANIFEST_A :
                     BK7258_OTA_N17_MTD_MANIFEST_B;
}

static enum bk7258_ota_mtd_slot_e bk7258_ota_n17_image_region(uint8_t slot)
{
  return slot == 0 ? BK7258_OTA_MTD_SLOT_A : BK7258_OTA_MTD_SLOT_B;
}

static int bk7258_ota_n17_read_exact(FAR struct mtd_dev_s *mtd,
                                      off_t offset, uint8_t *data,
                                      size_t length)
{
  return mtd == NULL || MTD_READ(mtd, offset, length, data) !=
         (ssize_t)length ? -EIO : 0;
}

static int bk7258_ota_n17_write_exact(FAR struct mtd_dev_s *mtd,
                                       off_t offset, const uint8_t *data,
                                       size_t length)
{
  return mtd == NULL || MTD_WRITE(mtd, offset, length, data) !=
         (ssize_t)length ? -EIO : 0;
}

static int bk7258_ota_n17_verify_erased(FAR struct mtd_dev_s *mtd)
{
  uint8_t buffer[BK7258_OTA_N17_HASH_CHUNK];
  size_t offset;

  for (offset = 0; offset < BK7258_BOOT_N17_BANK_SIZE;
       offset += sizeof(buffer))
    {
      if (bk7258_ota_n17_read_exact(mtd, offset, buffer, sizeof(buffer)) < 0 ||
          !bk7258_ota_n17_erased(buffer, sizeof(buffer)))
        {
          return -EIO;
        }
    }

  return 0;
}

static int bk7258_ota_n17_hash_slot(uint8_t slot,
                                    const uint8_t expected_digest[32])
{
  FAR struct mtd_dev_s *mtd = bk7258_ota_mtd_get(
    bk7258_ota_n17_image_region(slot));
  SHA2_CTX context;
  uint8_t digest[32];
  uint8_t buffer[BK7258_OTA_N17_HASH_CHUNK];
  uint32_t offset;

  if (mtd == NULL)
    {
      return -ENODEV;
    }

  sha256init(&context);
  for (offset = 0; offset < BK7258_ROLE_SLOT_B_PAIR_SIZE;
       offset += sizeof(buffer))
    {
      if (bk7258_ota_n17_read_exact(mtd, offset, buffer, sizeof(buffer)) < 0)
        {
          return -EIO;
        }

      sha256update(&context, buffer, sizeof(buffer));
    }

  sha256final(digest, &context);
  return memcmp(digest, expected_digest, sizeof(digest)) == 0 ? 0 : -EBADMSG;
}

static int bk7258_ota_n17_read_manifest(
  uint8_t slot, uint8_t manifest[BK7258_OTA_N17_MANIFEST_SIZE],
  struct bk7258_ota_n17_manifest_info_s *info)
{
  FAR struct mtd_dev_s *mtd = bk7258_ota_n17_mtd_get(
    bk7258_ota_n17_manifest_region(slot));

  if (bk7258_ota_n17_read_exact(mtd, 0, manifest,
                                 BK7258_OTA_N17_MANIFEST_SIZE) < 0)
    {
      return -EIO;
    }

  return bk7258_ota_n17_verify_manifest_info(manifest, info);
}

static int bk7258_ota_n17_program_manifest(
  uint8_t slot, const uint8_t manifest[BK7258_OTA_N17_MANIFEST_SIZE],
  struct bk7258_ota_n17_manifest_info_s *info)
{
  FAR struct mtd_dev_s *mtd = bk7258_ota_n17_mtd_get(
    bk7258_ota_n17_manifest_region(slot));
  uint8_t readback[BK7258_OTA_N17_MANIFEST_SIZE];
  size_t offset;

  if (mtd == NULL || MTD_ERASE(mtd, 0, 1) < 0 ||
      bk7258_ota_n17_verify_erased(mtd) < 0)
    {
      return -EIO;
    }

  for (offset = 0; offset < BK7258_OTA_N17_MANIFEST_SIZE;
       offset += BK7258_OTA_N17_PROGRAM_CHUNK)
    {
      if (bk7258_ota_n17_write_exact(
            mtd, offset, manifest + offset,
            BK7258_OTA_N17_PROGRAM_CHUNK) < 0)
        {
          return -EIO;
        }
    }

  if (bk7258_ota_n17_read_exact(mtd, 0, readback, sizeof(readback)) < 0 ||
      memcmp(readback, manifest, sizeof(readback)) != 0)
    {
      return -EIO;
    }

  return bk7258_ota_n17_verify_manifest_info(readback, info);
}

static int bk7258_ota_n17_publish_journal(
  uint32_t bank, const struct bk7258_boot_n17_record_s *record,
  const struct bk7258_boot_n17_journal_ops_s *ops)
{
  FAR struct mtd_dev_s *mtd = bk7258_ota_n17_mtd_get(
    bank == 0 ? BK7258_OTA_N17_MTD_JOURNAL_PRIMARY :
                BK7258_OTA_N17_MTD_JOURNAL_MIRROR);
  uint8_t encoded[BK7258_BOOT_N17_RECORD_SIZE];
  uint8_t readback[BK7258_BOOT_N17_RECORD_SIZE];
  uint8_t complete[BK7258_BOOT_N17_BANK_SIZE];
  struct bk7258_boot_n17_bank_s inspected;
  size_t offset;

  if (mtd == NULL || bk7258_boot_n17_record_encode(record, encoded) < 0 ||
      MTD_ERASE(mtd, 0, 1) < 0 || bk7258_ota_n17_verify_erased(mtd) < 0)
    {
      return -EIO;
    }

  for (offset = 0; offset < sizeof(encoded) - BK7258_OTA_N17_PROGRAM_CHUNK;
       offset += BK7258_OTA_N17_PROGRAM_CHUNK)
    {
      if (bk7258_ota_n17_write_exact(
            mtd, offset, encoded + offset,
            BK7258_OTA_N17_PROGRAM_CHUNK) < 0)
        {
          return -EIO;
        }
    }

  if (bk7258_ota_n17_write_exact(
        mtd, sizeof(encoded) - BK7258_OTA_N17_PROGRAM_CHUNK,
        encoded + sizeof(encoded) - BK7258_OTA_N17_PROGRAM_CHUNK,
        BK7258_OTA_N17_PROGRAM_CHUNK) < 0 ||
      bk7258_ota_n17_read_exact(mtd, 0, readback, sizeof(readback)) < 0 ||
      memcmp(readback, encoded, sizeof(encoded)) != 0 ||
      bk7258_boot_n17_record_parse(readback, &inspected.last) < 0 ||
      bk7258_ota_n17_read_exact(mtd, 0, complete, sizeof(complete)) < 0 ||
      bk7258_boot_n17_bank_inspect(complete, ops, &inspected) < 0 ||
      !inspected.eligible || inspected.valid_records != 1)
    {
      return -EIO;
    }

  return 0;
}

static int bk7258_ota_n17_append_journal(
  uint32_t bank, const struct bk7258_boot_n17_record_s *record,
  const struct bk7258_boot_n17_journal_ops_s *ops)
{
  FAR struct mtd_dev_s *mtd = bk7258_ota_n17_mtd_get(
    bank == 0 ? BK7258_OTA_N17_MTD_JOURNAL_PRIMARY :
                BK7258_OTA_N17_MTD_JOURNAL_MIRROR);
  struct bk7258_boot_n17_bank_s inspected;
  uint8_t encoded[BK7258_BOOT_N17_RECORD_SIZE];
  uint8_t complete[BK7258_BOOT_N17_BANK_SIZE];
  size_t offset;

  if (mtd == NULL || bk7258_ota_n17_read_exact(mtd, 0, complete,
                                                sizeof(complete)) < 0 ||
      bk7258_boot_n17_bank_inspect(complete, ops, &inspected) < 0 ||
      !inspected.eligible || inspected.tail_dirty ||
      inspected.valid_records >= BK7258_BOOT_N17_RECORD_COUNT ||
      record->sequence != inspected.last.sequence + 1u ||
      bk7258_boot_n17_record_encode(record, encoded) < 0)
    {
      return -ESTALE;
    }

  offset = inspected.valid_records * BK7258_BOOT_N17_RECORD_SIZE;
  for (size_t written = 0;
       written < sizeof(encoded) - BK7258_OTA_N17_PROGRAM_CHUNK;
       written += BK7258_OTA_N17_PROGRAM_CHUNK)
    {
      if (bk7258_ota_n17_write_exact(
            mtd, offset + written, encoded + written,
            BK7258_OTA_N17_PROGRAM_CHUNK) < 0)
        {
          return -EIO;
        }
    }

  if (bk7258_ota_n17_write_exact(
        mtd, offset + sizeof(encoded) - BK7258_OTA_N17_PROGRAM_CHUNK,
        encoded + sizeof(encoded) - BK7258_OTA_N17_PROGRAM_CHUNK,
        BK7258_OTA_N17_PROGRAM_CHUNK) < 0 ||
      bk7258_ota_n17_read_exact(mtd, 0, complete, sizeof(complete)) < 0 ||
      bk7258_boot_n17_bank_inspect(complete, ops, &inspected) < 0 ||
      !inspected.eligible || inspected.tail_dirty ||
      inspected.last.sequence != record->sequence)
    {
      return -EIO;
    }

  return 0;
}

static int bk7258_ota_n17_select_journal(
  const struct bk7258_boot_n17_journal_ops_s *ops,
  struct bk7258_boot_n17_selection_s *selected,
  uint8_t bank0[BK7258_BOOT_N17_BANK_SIZE],
  uint8_t bank1[BK7258_BOOT_N17_BANK_SIZE])
{
  FAR struct mtd_dev_s *mtd0 = bk7258_ota_n17_mtd_get(
    BK7258_OTA_N17_MTD_JOURNAL_PRIMARY);
  FAR struct mtd_dev_s *mtd1 = bk7258_ota_n17_mtd_get(
    BK7258_OTA_N17_MTD_JOURNAL_MIRROR);

  if (bk7258_ota_n17_read_exact(mtd0, 0, bank0,
                                 BK7258_BOOT_N17_BANK_SIZE) < 0 ||
      bk7258_ota_n17_read_exact(mtd1, 0, bank1,
                                 BK7258_BOOT_N17_BANK_SIZE) < 0 ||
      bk7258_boot_n17_bank_select(bank0, bank1, ops, selected) < 0 ||
      !selected->present || selected->rejected_newer ||
      selected->bank.tail_dirty)
    {
      return -ESTALE;
    }

  return 0;
}

static int bk7258_ota_n17_verify_slot(uint8_t slot,
                                       const uint8_t expected_sha256[32],
                                       struct bk7258_ota_n17_manifest_info_s
                                         *info)
{
  uint8_t manifest[BK7258_OTA_N17_MANIFEST_SIZE];

  if (bk7258_ota_n17_read_manifest(slot, manifest, info) < 0 ||
      memcmp(info->signed_sha256, expected_sha256,
             BK7258_OTA_N17_SHA256_SIZE) != 0)
    {
      return -EBADMSG;
    }

  return bk7258_ota_n17_hash_slot(slot, info->pair_sha256);
}

static int bk7258_ota_n17_lifecycle(
  enum bk7258_ota_n17_lifecycle_action_e action,
  uint64_t expected_generation)
{
  struct bk7258_boot_n17_journal_ops_s ops =
  {
    .arg = NULL,
    .sha256 = bk7258_ota_n17_sha256
  };
  struct bk7258_boot_n17_selection_s selected;
  struct bk7258_boot_n17_record_s next;
  struct bk7258_ota_n17_manifest_info_s stable_info;
  struct bk7258_ota_n17_manifest_info_s target_info;
  uint8_t bank0[BK7258_BOOT_N17_BANK_SIZE];
  uint8_t bank1[BK7258_BOOT_N17_BANK_SIZE];
  uint8_t *selected_raw;
  uint8_t stable;
  uint8_t target;
  bool active = false;
  int ret;

  if (expected_generation == 0 ||
      !__atomic_load_n(&g_bk7258_ota_n17_publish_initialized,
                       __ATOMIC_ACQUIRE) ||
      !bk7258_ota_n17_publish_write_enabled())
    {
      return -EACCES;
    }

  if (!__atomic_compare_exchange_n(&g_bk7258_ota_n17_publish_active,
                                   &active, true, false,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  ret = bk7258_flash_guard_lock(BK7258_FLASH_GUARD_OTA_N17_METADATA,
                                true, 0);
  if (ret < 0)
    {
      goto out;
    }

  ret = bk7258_ota_n17_select_journal(&ops, &selected, bank0, bank1);
  if (ret < 0 || selected.bank.last.generation != expected_generation)
    {
      ret = -ESTALE;
      goto unlock;
    }

  stable = selected.bank.last.stable_slot;
  target = selected.bank.last.target_slot;
  if (bk7258_ota_n17_verify_slot(
        stable, stable == 0 ? selected.bank.last.slot_a_manifest_sha256 :
                              selected.bank.last.slot_b_manifest_sha256,
        &stable_info) < 0 ||
      stable_info.security_counter !=
        selected.bank.last.accepted_security_counter)
    {
      ret = -EBADMSG;
      goto unlock;
    }

  next = selected.bank.last;
  next.sequence++;
  selected_raw = selected.bank_index == 0 ? bank0 : bank1;
  bk7258_ota_n17_sha256(NULL,
    selected_raw + (selected.bank.valid_records - 1u) *
      BK7258_BOOT_N17_RECORD_SIZE,
    BK7258_BOOT_N17_RECORD_SIZE, next.previous_record_sha256);

  if (action == BK7258_OTA_N17_LIFECYCLE_TRIAL)
    {
      if (next.phase != BK7258_BOOT_N17_PENDING ||
          bk7258_ota_n17_active_slot() != stable ||
          bk7258_ota_n17_verify_slot(
            target, target == 0 ? selected.bank.last.slot_a_manifest_sha256 :
                                  selected.bank.last.slot_b_manifest_sha256,
            &target_info) < 0 ||
          target_info.security_counter <= stable_info.security_counter)
        {
          ret = -EBADMSG;
          goto unlock;
        }

      next.phase = BK7258_BOOT_N17_TRIAL;
    }
  else if (action == BK7258_OTA_N17_LIFECYCLE_CONFIRM)
    {
      if (next.phase != BK7258_BOOT_N17_TRIAL ||
          bk7258_ota_n17_active_slot() != target ||
          bk7258_ota_n17_verify_slot(
            target, target == 0 ? selected.bank.last.slot_a_manifest_sha256 :
                                  selected.bank.last.slot_b_manifest_sha256,
            &target_info) < 0 ||
          target_info.security_counter <= stable_info.security_counter)
        {
          ret = -EBADMSG;
          goto unlock;
        }

      next.phase = BK7258_BOOT_N17_STABLE;
      next.outcome = BK7258_BOOT_N17_CONFIRMED;
      next.stable_slot = target;
      next.target_slot = 0xffu;
      next.accepted_security_counter = target_info.security_counter;
    }
  else
    {
      if ((next.phase != BK7258_BOOT_N17_PENDING &&
           next.phase != BK7258_BOOT_N17_TRIAL) ||
          bk7258_ota_n17_active_slot() != stable)
        {
          ret = -EBADMSG;
          goto unlock;
        }

      next.phase = BK7258_BOOT_N17_STABLE;
      next.outcome = BK7258_BOOT_N17_ROLLED_BACK;
      next.target_slot = 0xffu;
    }

  ret = bk7258_ota_n17_append_journal(selected.bank_index, &next, &ops);

unlock:
  bk7258_flash_guard_unlock();
out:
  __atomic_store_n(&g_bk7258_ota_n17_publish_active, false,
                   __ATOMIC_RELEASE);
  return ret;
}

int bk7258_ota_n17_publish_initialize(void)
{
  __atomic_store_n(&g_bk7258_ota_n17_publish_runtime_write, false,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&g_bk7258_ota_n17_publish_active, false,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&g_bk7258_ota_n17_publish_initialized, true,
                   __ATOMIC_RELEASE);
  return 0;
}

bool bk7258_ota_n17_publish_write_enabled(void)
{
#ifdef CONFIG_BK7258_OTA_N17_WRITE
  return __atomic_load_n(&g_bk7258_ota_n17_publish_runtime_write,
                         __ATOMIC_ACQUIRE);
#else
  return false;
#endif
}

#ifdef CONFIG_BK7258_OTA_N17_WRITE
int bk7258_ota_n17_publish_set_write_enabled(bool enabled)
{
  if (!__atomic_load_n(&g_bk7258_ota_n17_publish_initialized,
                       __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  __atomic_store_n(&g_bk7258_ota_n17_publish_runtime_write, enabled,
                   __ATOMIC_RELEASE);
  return 0;
}
#endif

int bk7258_ota_n17_publish_pending(
  const struct bk7258_ota_n17_pending_request_s *request,
  struct bk7258_ota_n17_pending_result_s *result)
{
  struct bk7258_boot_n17_journal_ops_s journal_ops =
  {
    .arg = NULL,
    .sha256 = bk7258_ota_n17_sha256
  };
  struct bk7258_boot_n17_selection_s selected;
  struct bk7258_boot_n17_record_s record;
  struct bk7258_ota_n17_manifest_info_s stable_info;
  struct bk7258_ota_n17_manifest_info_s target_info;
  FAR struct mtd_dev_s *journal[2];
  uint8_t stable_manifest[BK7258_OTA_N17_MANIFEST_SIZE];
  uint8_t bank0[BK7258_BOOT_N17_BANK_SIZE];
  uint8_t bank1[BK7258_BOOT_N17_BANK_SIZE];
  uint8_t stable;
  uint32_t destination_bank;
  bool active = false;
  int ret;

  if (request == NULL || result == NULL || request->target_slot >=
      BK7258_OTA_N17_SLOT_COUNT || request->generation == 0 ||
      !__atomic_load_n(&g_bk7258_ota_n17_publish_initialized,
                       __ATOMIC_ACQUIRE) ||
      !bk7258_ota_n17_publish_write_enabled())
    {
      return -EACCES;
    }

  if (!__atomic_compare_exchange_n(&g_bk7258_ota_n17_publish_active,
                                   &active, true, false,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  ret = bk7258_flash_guard_lock(BK7258_FLASH_GUARD_OTA_N17_METADATA,
                                true, 0);
  if (ret < 0)
    {
      goto out;
    }

  journal[0] = bk7258_ota_n17_mtd_get(BK7258_OTA_N17_MTD_JOURNAL_PRIMARY);
  journal[1] = bk7258_ota_n17_mtd_get(BK7258_OTA_N17_MTD_JOURNAL_MIRROR);
  if (bk7258_ota_n17_read_exact(journal[0], 0, bank0, sizeof(bank0)) < 0 ||
      bk7258_ota_n17_read_exact(journal[1], 0, bank1, sizeof(bank1)) < 0 ||
      bk7258_boot_n17_bank_select(bank0, bank1, &journal_ops, &selected) < 0 ||
      !selected.present || selected.rejected_newer ||
      selected.bank.last.phase != BK7258_BOOT_N17_STABLE ||
      selected.bank.last.generation == UINT64_MAX ||
      request->generation != selected.bank.last.generation + 1u)
    {
      ret = -ESTALE;
      goto unlock;
    }

  stable = selected.bank.last.stable_slot;
  if (stable != bk7258_ota_n17_active_slot() || request->target_slot == stable ||
      bk7258_ota_n17_read_manifest(stable, stable_manifest, &stable_info) < 0 ||
      stable_info.security_counter !=
        selected.bank.last.accepted_security_counter ||
      memcmp(stable_info.signed_sha256,
             stable == 0 ? selected.bank.last.slot_a_manifest_sha256 :
                           selected.bank.last.slot_b_manifest_sha256,
             BK7258_OTA_N17_SHA256_SIZE) != 0 ||
      bk7258_ota_n17_verify_manifest_info(request->manifest, &target_info) < 0 ||
      target_info.security_counter <= stable_info.security_counter ||
      bk7258_ota_n17_hash_slot(request->target_slot, target_info.pair_sha256) < 0)
    {
      ret = -EBADMSG;
      goto unlock;
    }

  if (bk7258_ota_n17_program_manifest(request->target_slot, request->manifest,
                                       &target_info) < 0)
    {
      ret = -EIO;
      goto unlock;
    }

  memset(&record, 0, sizeof(record));
  record.phase = BK7258_BOOT_N17_PENDING;
  record.outcome = BK7258_BOOT_N17_NONE;
  record.stable_slot = stable;
  record.target_slot = request->target_slot;
  record.sequence = 1u;
  record.generation = request->generation;
  record.accepted_security_counter = stable_info.security_counter;
  record.created_timestamp = request->timestamp;
  if (stable == 0)
    {
      memcpy(record.slot_a_manifest_sha256, stable_info.signed_sha256,
             BK7258_OTA_N17_SHA256_SIZE);
      memcpy(record.slot_b_manifest_sha256, target_info.signed_sha256,
             BK7258_OTA_N17_SHA256_SIZE);
    }
  else
    {
      memcpy(record.slot_a_manifest_sha256, target_info.signed_sha256,
             BK7258_OTA_N17_SHA256_SIZE);
      memcpy(record.slot_b_manifest_sha256, stable_info.signed_sha256,
             BK7258_OTA_N17_SHA256_SIZE);
    }

  destination_bank = 1u - selected.bank_index;
  ret = bk7258_ota_n17_publish_journal(destination_bank, &record,
                                        &journal_ops);
  if (ret == 0)
    {
      uint8_t *published_bank = destination_bank == 0 ? bank0 : bank1;

      if (bk7258_ota_n17_read_exact(journal[destination_bank], 0,
                                     published_bank,
                                     BK7258_BOOT_N17_BANK_SIZE) < 0 ||
          bk7258_boot_n17_bank_select(bank0, bank1, &journal_ops,
                                      &selected) < 0 ||
          !selected.present || selected.bank_index != destination_bank ||
          selected.bank.last.generation != request->generation ||
          selected.bank.last.phase != BK7258_BOOT_N17_PENDING)
        {
          ret = -EIO;
        }
      else
        {
          result->stable_slot = stable;
          result->target_slot = request->target_slot;
          result->journal_bank = destination_bank;
          result->generation = request->generation;
          result->accepted_security_counter = stable_info.security_counter;
        }
    }

unlock:
  bk7258_flash_guard_unlock();
out:
  __atomic_store_n(&g_bk7258_ota_n17_publish_active, false,
                   __ATOMIC_RELEASE);
  return ret;
}

int bk7258_ota_n17_publish_trial(uint64_t expected_generation)
{
  return bk7258_ota_n17_lifecycle(BK7258_OTA_N17_LIFECYCLE_TRIAL,
                                  expected_generation);
}

int bk7258_ota_n17_publish_confirm(uint64_t expected_generation)
{
  return bk7258_ota_n17_lifecycle(BK7258_OTA_N17_LIFECYCLE_CONFIRM,
                                  expected_generation);
}

int bk7258_ota_n17_publish_rollback(uint64_t expected_generation)
{
  return bk7258_ota_n17_lifecycle(BK7258_OTA_N17_LIFECYCLE_ROLLBACK,
                                  expected_generation);
}
