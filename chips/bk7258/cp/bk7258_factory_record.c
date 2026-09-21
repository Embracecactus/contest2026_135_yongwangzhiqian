/****************************************************************************
 * chips/bk7258/cp/bk7258_factory_record.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * First-use factory transaction record: two erase-aligned slots, monotonic
 * generation numbers and a CRC that only detects damage. The CRC is never an
 * authorization: only a deployment writes BK7258_FACTORY_PENDING, and only
 * that state lets the device initialize its own user storage.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <string.h>

#include <arch/chip/bk7258_factory_record.h>
#include <arch/chip/bk7258_flash.h>
#include <arch/chip/bk7258_storage_guard.h>

#include "bk7258_storage_internal.h"

#define BK7258_FACTORY_LOCK_TIMEOUT_MS 1000u

static uint32_t factory_crc32(FAR const uint8_t *data, size_t length)
{
  uint32_t crc = 0xffffffffu;
  size_t i;
  unsigned int bit;

  for (i = 0; i < length; i++)
    {
      crc ^= data[i];
      for (bit = 0; bit < 8u; bit++)
        {
          crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }

  return ~crc;
}

static uint32_t factory_record_crc(FAR const struct bk7258_factory_record_s *
                                   record)
{
  struct bk7258_factory_record_s copy = *record;

  copy.crc = 0;
  return factory_crc32((FAR const uint8_t *)&copy, sizeof(copy));
}

static bool factory_record_valid(FAR const struct bk7258_factory_record_s *
                                 record)
{
  if (record->magic != BK7258_FACTORY_MAGIC ||
      record->version != BK7258_FACTORY_VERSION ||
      record->length != sizeof(*record) ||
      record->generation == 0u || record->generation == UINT32_MAX ||
      record->state == BK7258_FACTORY_EMPTY ||
      record->state > BK7258_FACTORY_FAULT)
    {
      return false;
    }

  return record->crc == factory_record_crc(record);
}

static int factory_region(FAR uint32_t *address, FAR uint32_t *size)
{
  int ret;

  ret = bk7258_storage_factory_address(address);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_storage_factory_size(size);
  if (ret < 0)
    {
      return ret;
    }

  if (*size < BK7258_FLASH_SECTOR_SIZE * BK7258_FACTORY_SLOT_COUNT)
    {
      return -EINVAL;
    }

  return 0;
}

static int factory_read_slot(unsigned int slot,
                             FAR struct bk7258_factory_record_s *record)
{
  uint32_t address;
  uint32_t size;
  int ret;

  ret = factory_region(&address, &size);
  if (ret < 0)
    {
      return ret;
    }

  address += slot * BK7258_FLASH_SECTOR_SIZE;
  ret = bk7258_flash_initialize();
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_flash_read(address, record, sizeof(*record));
}

int bk7258_factory_record_read(FAR struct bk7258_factory_record_s *record)
{
  struct bk7258_factory_record_s first;
  struct bk7258_factory_record_s second;
  bool have_first;
  bool have_second;
  int ret;

  if (record == NULL)
    {
      return -EINVAL;
    }

  memset(&first, 0, sizeof(first));
  memset(&second, 0, sizeof(second));
  ret = factory_read_slot(0u, &first);
  if (ret < 0)
    {
      return ret;
    }

  ret = factory_read_slot(1u, &second);
  if (ret < 0)
    {
      return ret;
    }

  have_first = factory_record_valid(&first);
  have_second = factory_record_valid(&second);
  if (!have_first && !have_second)
    {
      memset(record, 0, sizeof(*record));
      record->version = BK7258_FACTORY_VERSION;
      record->length = sizeof(*record);
      record->state = BK7258_FACTORY_EMPTY;
      return 0;
    }

  if (have_first && have_second)
    {
      *record = first.generation >= second.generation ? first : second;
      return 0;
    }

  *record = have_first ? first : second;
  return 0;
}

int bk7258_factory_record_write(FAR const struct bk7258_factory_record_s *
                                record)
{
  struct bk7258_factory_record_s current;
  struct bk7258_factory_record_s verify;
  struct bk7258_factory_record_s pending = *record;
  bool first_valid;
  bool second_valid;
  unsigned int slot;
  uint32_t address;
  uint32_t size;
  int ret;

  if (record == NULL || record->state == BK7258_FACTORY_EMPTY ||
      record->state > BK7258_FACTORY_FAULT)
    {
      return -EINVAL;
    }

  ret = factory_read_slot(0u, &current);
  if (ret < 0)
    {
      return ret;
    }

  first_valid = factory_record_valid(&current);
  ret = factory_read_slot(1u, &verify);
  if (ret < 0)
    {
      return ret;
    }

  second_valid = factory_record_valid(&verify);

  if (!first_valid && !second_valid)
    {
      slot = 0u;
    }
  else if (first_valid && second_valid)
    {
      slot = current.generation >= verify.generation ? 1u : 0u;
    }
  else
    {
      slot = first_valid ? 1u : 0u;
    }

  ret = factory_region(&address, &size);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_flash_initialize();
  if (ret < 0)
    {
      return ret;
    }

  address += slot * BK7258_FLASH_SECTOR_SIZE;
  pending.crc = factory_record_crc(&pending);
  ret = bk7258_storage_lock(BK7258_STORAGE_GUARD_FACTORY_RECORD,
                            BK7258_FACTORY_LOCK_TIMEOUT_MS);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_flash_erase_sector(address);
  if (ret == 0)
    {
      ret = bk7258_flash_write(address, &pending, sizeof(pending));
    }

  bk7258_storage_unlock();
  if (ret < 0)
    {
      return ret;
    }

  /* A readback only catches an immediate failure. Crashing between the erase
   * and the write leaves this slot invalid, and the other slot still holds the
   * previous generation, so recovery never depends on this readback.
   */
  memset(&verify, 0, sizeof(verify));
  ret = bk7258_flash_read(address, &verify, sizeof(verify));
  if (ret < 0)
    {
      return ret;
    }

  if (!factory_record_valid(&verify) ||
      verify.generation != pending.generation ||
      verify.state != pending.state ||
      memcmp(verify.transaction, pending.transaction,
             sizeof(verify.transaction)) != 0)
    {
      return -EIO;
    }

  return 0;
}

int bk7258_factory_record_advance(uint32_t state)
{
  struct bk7258_factory_record_s record;
  int ret;

  ret = bk7258_factory_record_read(&record);
  if (ret < 0)
    {
      return ret;
    }

  if (record.state == state &&
      record.generation != 0u)
    {
      return 0;
    }

  /* Monotonic, authorized transitions only. Anything else - including every
   * move out of DEPLOYED and any backwards move - is refused so a storage
   * fault can never re-open a destructive path.
   */
  switch (record.state)
    {
      case BK7258_FACTORY_EMPTY:
        if (state != BK7258_FACTORY_PENDING) return -EPERM;
        break;

      case BK7258_FACTORY_PENDING:
        if (state != BK7258_FACTORY_STORAGE_READY &&
            state != BK7258_FACTORY_FAULT)
          {
            return -EPERM;
          }

        break;

      case BK7258_FACTORY_STORAGE_READY:
        if (state != BK7258_FACTORY_IDENTITY_READY &&
            state != BK7258_FACTORY_FAULT)
          {
            return -EPERM;
          }

        break;

      case BK7258_FACTORY_IDENTITY_READY:
        if (state != BK7258_FACTORY_UNCLAIMED_READY &&
            state != BK7258_FACTORY_FAULT)
          {
            return -EPERM;
          }

        break;

      case BK7258_FACTORY_UNCLAIMED_READY:
        if (state != BK7258_FACTORY_DEPLOYED &&
            state != BK7258_FACTORY_FAULT)
          {
            return -EPERM;
          }

        break;

      default:
        return -EPERM;
    }

  record.state = state;
  record.generation = record.generation == 0u ? 1u : record.generation + 1u;
  return bk7258_factory_record_write(&record);
}

const char *bk7258_factory_state_name(uint32_t state)
{
  switch (state)
    {
      case BK7258_FACTORY_EMPTY: return "empty";
      case BK7258_FACTORY_PENDING: return "pending";
      case BK7258_FACTORY_STORAGE_READY: return "storage-ready";
      case BK7258_FACTORY_IDENTITY_READY: return "identity-ready";
      case BK7258_FACTORY_UNCLAIMED_READY: return "unclaimed-ready";
      case BK7258_FACTORY_DEPLOYED: return "deployed";
      case BK7258_FACTORY_FAULT: return "fault";
      default: return "unknown";
    }
}
