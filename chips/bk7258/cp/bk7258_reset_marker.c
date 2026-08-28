/****************************************************************************
 * chips/bk7258/cp/bk7258_reset_marker.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 reset-marker format and lifecycle.  The board supplies the exact
 * persistent sector and serialization policy; the chip owns raw Flash I/O.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <arch/chip/bk7258_flash.h>

#include "bk7258_reset_marker_internal.h"
#include "bk7258_storage_internal.h"

#define BK7258_RESET_MARKER_MAGIC        0x524d4b42u /* "BKMR" */
#define BK7258_RESET_MARKER_VERSION      2u
#define BK7258_RESET_MARKER_LOCK_TIMEOUT 1000u

enum bk7258_reset_marker_capture_state_e
{
  BK7258_RESET_MARKER_CAPTURE_EMPTY = 0,
  BK7258_RESET_MARKER_CAPTURE_RUNNING,
  BK7258_RESET_MARKER_CAPTURE_READY
};

struct bk7258_reset_marker_record_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t reason;
  uint32_t reason_inverse;
};

static uint8_t g_capture_state;
static bool g_previous_present;
static uint32_t g_previous_reason;

static int bk7258_reset_marker_result(int ret)
{
  return ret == 0 ? 0 : (ret < 0 ? ret : -EIO);
}

static bool bk7258_reset_marker_valid(
  FAR const struct bk7258_reset_marker_record_s *record)
{
  return record->magic == BK7258_RESET_MARKER_MAGIC &&
         record->version == BK7258_RESET_MARKER_VERSION &&
         (record->reason == BK7258_RESET_SOURCE_WATCHDOG ||
          record->reason == BK7258_RESET_SOURCE_NMI_WDT) &&
         record->reason_inverse == ~record->reason;
}

static int bk7258_reset_marker_begin(FAR uint32_t *address)
{
  int ret;

  ret = bk7258_storage_marker_address(address);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_reset_marker_result(bk7258_flash_initialize());
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_storage_lock(BK7258_STORAGE_GUARD_RESET_MARKER,
                             BK7258_RESET_MARKER_LOCK_TIMEOUT);
}

int bk7258_reset_marker_capture_previous(void)
{
  struct bk7258_reset_marker_record_s record;
  uint32_t address;
  uint8_t expected = BK7258_RESET_MARKER_CAPTURE_EMPTY;
  int ret;

  if (!__atomic_compare_exchange_n(&g_capture_state, &expected,
                                    BK7258_RESET_MARKER_CAPTURE_RUNNING,
                                    false, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE))
    {
      return expected == BK7258_RESET_MARKER_CAPTURE_READY ? 0 : -EBUSY;
    }

  ret = bk7258_reset_marker_begin(&address);
  if (ret < 0)
    {
      __atomic_store_n(&g_capture_state,
                       BK7258_RESET_MARKER_CAPTURE_EMPTY,
                       __ATOMIC_RELEASE);
      return ret;
    }

  memset(&record, 0, sizeof(record));
  ret = bk7258_reset_marker_result(
          bk7258_flash_read(address, &record, sizeof(record)));
  if (ret == 0 && bk7258_reset_marker_valid(&record))
    {
      ret = bk7258_reset_marker_result(
              bk7258_flash_erase_sector(address));
      if (ret == 0)
        {
          g_previous_reason = record.reason;
          g_previous_present = true;
        }
    }

  bk7258_storage_unlock();
  if (ret == 0)
    {
      __atomic_store_n(&g_capture_state,
                       BK7258_RESET_MARKER_CAPTURE_READY,
                       __ATOMIC_RELEASE);
    }
  else
    {
      __atomic_store_n(&g_capture_state,
                       BK7258_RESET_MARKER_CAPTURE_EMPTY,
                       __ATOMIC_RELEASE);
    }

  return ret;
}

int bk7258_reset_marker_previous(FAR uint32_t *reason)
{
  if (reason == NULL)
    {
      return -EINVAL;
    }

  if (__atomic_load_n(&g_capture_state, __ATOMIC_ACQUIRE) !=
      BK7258_RESET_MARKER_CAPTURE_READY)
    {
      return -EAGAIN;
    }

  if (!g_previous_present)
    {
      return 0;
    }

  *reason = g_previous_reason;
  return 1;
}

int bk7258_reset_marker_stamp(enum bk7258_reset_source_e reason)
{
  struct bk7258_reset_marker_record_s verify;
  const struct bk7258_reset_marker_record_s record =
  {
    .magic = BK7258_RESET_MARKER_MAGIC,
    .version = BK7258_RESET_MARKER_VERSION,
    .reason = reason,
    .reason_inverse = ~reason,
  };
  uint32_t address;
  int ret;

  if (reason != BK7258_RESET_SOURCE_WATCHDOG &&
      reason != BK7258_RESET_SOURCE_NMI_WDT)
    {
      return -EINVAL;
    }

  ret = bk7258_reset_marker_begin(&address);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_reset_marker_result(
          bk7258_flash_erase_sector(address));
  if (ret == 0)
    {
      ret = bk7258_reset_marker_result(
              bk7258_flash_write(address, &record,
                                 sizeof(record)));
    }

  if (ret == 0)
    {
      memset(&verify, 0, sizeof(verify));
      ret = bk7258_reset_marker_result(
              bk7258_flash_read(address, &verify,
                                sizeof(verify)));
      if (ret == 0 && memcmp(&verify, &record, sizeof(record)) != 0)
        {
          ret = -EIO;
        }
    }

  bk7258_storage_unlock();
  return ret;
}
