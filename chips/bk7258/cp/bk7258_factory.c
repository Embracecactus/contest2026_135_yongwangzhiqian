/****************************************************************************
 * chips/bk7258/cp/bk7258_factory.c
 * SPDX-License-Identifier: Apache-2.0
 * Append-only first-boot journal, separate from data and reset/OTA records.
 ****************************************************************************/
#include <nuttx/config.h>
#ifdef CONFIG_BK7258_FACTORY_INIT

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <arch/chip/bk7258_factory.h>
#include <arch/chip/bk7258_flash.h>
#include "bk7258_storage_internal.h"

#define RECORD_SIZE 128u
#define MAX_RECORDS 64u

struct scan_s
{
  struct bk7258_factory_status_s status;
  uint8_t record[RECORD_SIZE];
  uint32_t used;
};

static uint32_t get32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static void put32(uint8_t *p, uint32_t n)
{
  p[0] = n >> 24;
  p[1] = n >> 16;
  p[2] = n >> 8;
  p[3] = n;
}

static uint32_t checksum(const uint8_t *p, size_t n)
{
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < n; i++)
    {
      crc ^= p[i];
      for (unsigned int j = 0; j < 8; j++)
        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  return ~crc;
}

static bool erased(const uint8_t *p, size_t n)
{
  for (size_t i = 0; i < n; i++) if (p[i] != 0xff) return false;
  return true;
}

static int scan(struct scan_s *s,
                const struct bk7258_storage_region_s *journal,
                const struct bk7258_storage_region_s *data,
                const struct bk7258_ota_layout_s *layout)
{
  uint8_t record[RECORD_SIZE];
  bool recognizable = false;
  memset(s, 0, sizeof(*s));
  if (journal->size != MAX_RECORDS * RECORD_SIZE) return -EINVAL;
  for (uint32_t i = 0; i < MAX_RECORDS; i++)
    {
      int ret = bk7258_flash_read(journal->start + i * RECORD_SIZE,
                                  record, sizeof(record));
      if (ret) return ret < 0 ? ret : -EIO;
      if (erased(record, sizeof(record))) continue;
      s->used = i + 1;
      if (memcmp(record, "SFJ1", 4)) continue;
      recognizable = true;
      if (get32(record + 4) != 1 ||
          get32(record + 124) != checksum(record, 124)) continue;
      uint32_t sequence = get32(record + 8);
      uint32_t phase = get32(record + 12);
      if (sequence == 0 || sequence > MAX_RECORDS || phase < 1 || phase > 3 ||
          get32(record + 16) != data->start ||
          get32(record + 20) != data->size) return -EXDEV;
      uint8_t nonzero = 0;
      for (size_t j = 56; j < 72; j++) nonzero |= record[j];
      if (!nonzero) return -EBADMSG;
      for (size_t j = 72; j < 124; j++) if (record[j]) return -EBADMSG;
      if (s->status.sequence &&
          (sequence <= s->status.sequence || phase < s->status.phase ||
           memcmp(record + 56, s->status.transaction, 16))) return -EBADMSG;
      s->status.sequence = sequence;
      s->status.phase = phase;
      memcpy(s->status.transaction, record + 56, 16);
      memcpy(s->record, record, sizeof(record));
    }
  if (s->status.sequence && s->status.phase != BK7258_FACTORY_READY &&
      memcmp(s->record + 24, layout->layout_sha256, 32)) return -EXDEV;
  return s->status.sequence ? 0 : recognizable ? -EBADMSG : -ENOENT;
}

static int topology(const struct bk7258_storage_region_s **journal,
                    const struct bk7258_storage_region_s **data,
                    const struct bk7258_ota_layout_s **layout)
{
  int ret = bk7258_storage_factory_regions(journal, data);
  if (!ret) ret = bk7258_storage_ota_layout(layout);
  if (!ret) ret = bk7258_flash_initialize();
  return ret > 0 ? -EIO : ret;
}

int bk7258_factory_status(struct bk7258_factory_status_s *status)
{
  const struct bk7258_storage_region_s *journal;
  const struct bk7258_storage_region_s *data;
  const struct bk7258_ota_layout_s *layout;
  struct scan_s s;
  if (!status) return -EINVAL;
  int ret = topology(&journal, &data, &layout);
  if (ret) return ret;
  ret = bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_FACTORY, false, 1000);
  if (ret) return ret;
  ret = scan(&s, journal, data, layout);
  if (!ret) *status = s.status;
  bk7258_storage_guard_unlock();
  return ret;
}

int bk7258_factory_advance(uint32_t expected, uint32_t next)
{
  const struct bk7258_storage_region_s *journal;
  const struct bk7258_storage_region_s *data;
  const struct bk7258_ota_layout_s *layout;
  uint8_t scratch[256];
  struct scan_s s;
  if (expected < 1 || next != expected + 1 || next > 3) return -EINVAL;
  int ret = topology(&journal, &data, &layout);
  if (ret) return ret;
  /* The first destructive phase is admitted only for a fully erased target.
   * Mount/format is not performed under a raw flash lock, avoiding a nested
   * filesystem/data guard deadlock. Startup has not published /data yet.
   */
  if (expected == BK7258_FACTORY_REQUESTED)
    {
      ret = bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_DATA, false, 1000);
      if (ret) return ret;
      for (uint32_t at = 0; at < data->size; at += sizeof(scratch))
        {
          size_t n = data->size - at;
          if (n > sizeof(scratch)) n = sizeof(scratch);
          ret = bk7258_flash_read(data->start + at, scratch, n);
          if (ret || !erased(scratch, n))
            {
              if (!ret) ret = -EEXIST;
              break;
            }
        }
      bk7258_storage_guard_unlock();
      if (ret) return ret < 0 ? ret : -EIO;
    }
  ret = bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_FACTORY, true, 1000);
  if (ret) return ret;
  ret = scan(&s, journal, data, layout);
  if (!ret && s.status.phase != expected) ret = -ESTALE;
  if (!ret && s.used >= MAX_RECORDS) ret = -ENOSPC;
  if (!ret)
    {
      put32(s.record + 8, s.status.sequence + 1);
      put32(s.record + 12, next);
      put32(s.record + 124, checksum(s.record, 124));
      uint32_t address = journal->start + s.used * RECORD_SIZE;
      ret = bk7258_flash_write(address, s.record, sizeof(s.record));
      if (!ret) ret = bk7258_flash_read(address, scratch, sizeof(s.record));
      if (!ret && memcmp(scratch, s.record, sizeof(s.record))) ret = -EIO;
    }
  bk7258_storage_guard_unlock();
  return ret > 0 ? -EIO : ret;
}
#endif
