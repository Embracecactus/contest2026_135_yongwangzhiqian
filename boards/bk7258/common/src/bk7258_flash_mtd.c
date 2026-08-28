/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/bk7258_flash_mtd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 product-partition MTD composition.
 *
 * Exposes the selected on-chip persistent_data range through the chip-owned
 * raw Flash service.  Geometry comes only from the generated partition
 * contract.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/mtd/mtd.h>

#include <arch/chip/bk7258_flash.h>
#include <arch/chip/bk7258_image_layout.h>
#include <arch/chip/bk7258_storage_guard.h>

#include "bk7258_flash_mtd.h"

_Static_assert(BK7258_FLASH_ERASE_SIZE == BK7258_FLASH_SECTOR_SIZE,
               "generated and chip Flash erase sizes differ");

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Selected persistent-data layout. */

#define BK7258_DATA_PART_BASE       BK7258_DATA_RAW_PHYSICAL_OFFSET
#define BK7258_DATA_PART_SIZE       BK7258_DATA_RAW_PHYSICAL_SIZE

#define BK7258_FLASH_BLOCK_SIZE     BK7258_FLASH_ERASE_SIZE
#define BK7258_FLASH_NBLOCKS        (BK7258_DATA_PART_SIZE / BK7258_FLASH_BLOCK_SIZE)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_flash_mtd_s
{
  struct mtd_dev_s mtd;
  uint32_t base;
  uint32_t size;
  bool crc_encoded;
  enum bk7258_storage_guard_e owner;
  FAR const char *name;
};

struct bk7258_flash_mtd_block_range_s
{
  uint32_t offset;
  uint32_t nbytes;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_flash_mtd_s g_bk7258_data_mtd =
{
  .base = BK7258_DATA_PART_BASE,
  .size = BK7258_DATA_PART_SIZE,
  .owner = BK7258_STORAGE_GUARD_DATA,
  .name = "bk7258-data"
};

static bool g_bk7258_flash_mtd_initialized;

static FAR struct bk7258_flash_mtd_s *
bk7258_flash_mtd_state(FAR struct mtd_dev_s *dev)
{
  if (dev == &g_bk7258_data_mtd.mtd)
    {
      return &g_bk7258_data_mtd;
    }

  return NULL;
}

/* Validate a block range before narrowing it to the raw-Flash API's
 * uint32_t address and byte-count domain.  In particular, do not express
 * the partition check as first + nblocks: callers can supply values for
 * which that addition wraps before the comparison.
 */

static int bk7258_flash_mtd_block_range(
    FAR const struct bk7258_flash_mtd_s *state, off_t startblock,
    size_t nblocks, FAR struct bk7258_flash_mtd_block_range_s *range)
{
  size_t first;
  size_t block_count;

  if (state == NULL || range == NULL || startblock < 0 ||
      (uintmax_t)startblock > SIZE_MAX)
    {
      return -EINVAL;
    }

  first = (size_t)startblock;

  /* Both products below are stored in uint32_t.  Check before computing
   * either one, independently of the generated partition's current size.
   */

  if (first > UINT32_MAX / BK7258_FLASH_BLOCK_SIZE ||
      nblocks > UINT32_MAX / BK7258_FLASH_BLOCK_SIZE)
    {
      return -EINVAL;
    }

  block_count = state->size / BK7258_FLASH_BLOCK_SIZE;
  if (first > block_count || nblocks > block_count - first)
    {
      return -EINVAL;
    }

  range->offset = (uint32_t)first * BK7258_FLASH_BLOCK_SIZE;
  range->nbytes = (uint32_t)nblocks * BK7258_FLASH_BLOCK_SIZE;
  return OK;
}

static int bk7258_flash_mtd_lock(const struct bk7258_flash_mtd_s *state,
                                 bool write)
{
  /* Keep data-partition reads explicitly unprivileged.  The OTA parent uses
   * its distinct owner only so the existing guard can confine mutation to
   * the hardware-inactive image pair.
   */

  if (state->owner == BK7258_STORAGE_GUARD_DATA)
    {
      return bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_DATA,
                                       write, 0u);
    }

  return bk7258_storage_guard_lock(state->owner, write, 0u);
}

static void bk7258_flash_mtd_unlock(void)
{
  bk7258_storage_guard_unlock();
}

/* The boot ROM/XIP controller exposes executable Flash as 32 data bytes
 * followed by two CRC16 bytes.  The NuttX MCUboot flash-map contract instead
 * requires a contiguous logical byte stream.  This adapter is deliberately
 * read-only in the BL2 profile: no standard MCUboot update path can mutate
 * executable Flash until its matching encoded write transaction is added.
 */

static uint16_t bk7258_flash_crc16(const uint8_t *data)
{
  uint16_t crc = 0xffffu;
  unsigned int index;
  unsigned int bit;

  for (index = 0; index < BK7258_FLASH_CRC_DATA_SIZE; index++)
    {
      crc ^= (uint16_t)data[index] << 8;
      for (bit = 0; bit < 8; bit++)
        {
          crc = (uint16_t)((crc << 1) ^
                           ((crc & 0x8000u) != 0 ? 0x8005u : 0u));
        }
    }

  return crc;
}

static int bk7258_flash_crc_read(const struct bk7258_flash_mtd_s *state,
                                 uint32_t offset, uint32_t nbytes,
                                 FAR uint8_t *buffer)
{
  uint8_t packet[BK7258_FLASH_CRC_TOTAL_SIZE];

  while (nbytes != 0)
    {
      uint32_t group = offset / BK7258_FLASH_CRC_DATA_SIZE;
      uint32_t in_group = offset % BK7258_FLASH_CRC_DATA_SIZE;
      uint32_t count = BK7258_FLASH_CRC_DATA_SIZE - in_group;
      uint16_t stored_crc;

      if (count > nbytes)
        {
          count = nbytes;
        }

      if (bk7258_flash_read(state->base +
                            group * BK7258_FLASH_CRC_TOTAL_SIZE,
                            packet, sizeof(packet)) < 0)
        {
          ferr("bk7258: MCUboot CRC read failed at 0x%08" PRIx32 "\n",
               state->base + group * BK7258_FLASH_CRC_TOTAL_SIZE);
          return -EIO;
        }

      stored_crc = ((uint16_t)packet[BK7258_FLASH_CRC_DATA_SIZE] << 8) |
                   packet[BK7258_FLASH_CRC_DATA_SIZE + 1u];
      if (stored_crc != bk7258_flash_crc16(packet))
        {
          ferr("bk7258: MCUboot CRC mismatch at 0x%08" PRIx32 "\n",
               state->base + group * BK7258_FLASH_CRC_TOTAL_SIZE);
          return -EILSEQ;
        }

      memcpy(buffer, packet + in_group, count);
      buffer += count;
      offset += count;
      nbytes -= count;
    }

  return OK;
}

/****************************************************************************
 * MTD Methods
 ****************************************************************************/

/* Read whole 4 KiB blocks through the chip raw-Flash service. */

static ssize_t bk7258_flash_mtd_bread(FAR struct mtd_dev_s *dev,
                                      off_t startblock, size_t nblocks,
                                      FAR uint8_t *buffer)
{
  FAR struct bk7258_flash_mtd_s *state = bk7258_flash_mtd_state(dev);
  struct bk7258_flash_mtd_block_range_s range;
  int ret;

  ret = bk7258_flash_mtd_block_range(state, startblock, nblocks, &range);
  if (ret < 0)
    {
      return ret;
    }

  if (nblocks == 0u)
    {
      return 0;
    }

  if (bk7258_flash_mtd_lock(state, false) < 0)
    {
      return -EINTR;
    }

  if ((state->crc_encoded &&
       bk7258_flash_crc_read(state, range.offset, range.nbytes, buffer) < 0) ||
      (!state->crc_encoded &&
       bk7258_flash_read(state->base + range.offset, buffer,
                         range.nbytes) < 0))
    {
      bk7258_flash_mtd_unlock();
      return -EIO;
    }

  bk7258_flash_mtd_unlock();
  return (ssize_t)nblocks;
}

/* Erase whole 4 KiB sectors.  The chip service and SDK driver own controller
 * locking, permission checks and status-register protection.
 */

static int bk7258_flash_mtd_erase(FAR struct mtd_dev_s *dev,
                                  off_t startblock, size_t nblocks)
{
  FAR struct bk7258_flash_mtd_s *state = bk7258_flash_mtd_state(dev);
  struct bk7258_flash_mtd_block_range_s range;
  size_t block;
  int ret;

  ret = bk7258_flash_mtd_block_range(state, startblock, nblocks, &range);
  if (ret < 0)
    {
      return ret;
    }

  if (nblocks == 0u)
    {
      return 0;
    }

  if (state->crc_encoded)
    {
      return -EROFS;
    }

  if (bk7258_flash_mtd_lock(state, true) < 0)
    {
      return -EINTR;
    }

  for (block = 0; block < nblocks; block++)
    {
      uint32_t addr = state->base +
                      range.offset +
                      (uint32_t)block * BK7258_FLASH_BLOCK_SIZE;

      if (bk7258_flash_erase_sector(addr) < 0)
        {
          bk7258_flash_mtd_unlock();
          return -EIO;
        }
    }

  bk7258_flash_mtd_unlock();
  return OK;
}

/* Write whole 4 KiB blocks.  The chip service handles the SDK's page
 * programming mechanics.
 */

static ssize_t bk7258_flash_mtd_bwrite(FAR struct mtd_dev_s *dev,
                                       off_t startblock, size_t nblocks,
                                       FAR const uint8_t *buffer)
{
  FAR struct bk7258_flash_mtd_s *state = bk7258_flash_mtd_state(dev);
  struct bk7258_flash_mtd_block_range_s range;
  uint32_t address;
  int ret;

  ret = bk7258_flash_mtd_block_range(state, startblock, nblocks, &range);
  if (ret < 0)
    {
      return ret;
    }

  if (nblocks == 0u)
    {
      return 0;
    }

  if (state->crc_encoded)
    {
      return -EROFS;
    }

  address = state->base + range.offset;
  if (bk7258_flash_mtd_lock(state, true) < 0)
    {
      return -EINTR;
    }

  if (bk7258_flash_write(address, buffer, range.nbytes) < 0)
    {
      bk7258_flash_mtd_unlock();
      return -EIO;
    }

  bk7258_flash_mtd_unlock();
  return (ssize_t)nblocks;
}

/* Advertise true byte access to the MTD partition layer.  The caller owns the
 * data guard, and this lower-half nests that owner while issuing Flash
 * commands.
 */

static ssize_t bk7258_flash_mtd_read(FAR struct mtd_dev_s *dev, off_t offset,
                                     size_t nbytes, FAR uint8_t *buffer)
{
  FAR struct bk7258_flash_mtd_s *state = bk7258_flash_mtd_state(dev);

  if (state == NULL || offset < 0 || (uint64_t)offset + nbytes > state->size)
    {
      return -EINVAL;
    }

  if (nbytes == 0u)
    {
      return 0;
    }

  if (bk7258_flash_mtd_lock(state, false) < 0)
    {
      return -EINTR;
    }

  if ((state->crc_encoded &&
       bk7258_flash_crc_read(state, (uint32_t)offset, (uint32_t)nbytes,
                             buffer) < 0) ||
      (!state->crc_encoded &&
       bk7258_flash_read(state->base + (uint32_t)offset, buffer,
                         nbytes) < 0))
    {
      bk7258_flash_mtd_unlock();
      return -EIO;
    }

  bk7258_flash_mtd_unlock();
  return (ssize_t)nbytes;
}

#ifdef CONFIG_MTD_BYTE_WRITE
static ssize_t bk7258_flash_mtd_write(FAR struct mtd_dev_s *dev,
                                      off_t offset, size_t nbytes,
                                      FAR const uint8_t *buffer)
{
  FAR struct bk7258_flash_mtd_s *state = bk7258_flash_mtd_state(dev);

  if (state == NULL || offset < 0 || nbytes == 0 ||
      (uint64_t)offset + nbytes > state->size)
    {
      return -EINVAL;
    }

  if (state->crc_encoded)
    {
      return -EROFS;
    }

  if (bk7258_flash_mtd_lock(state, true) < 0)
    {
      return -EINTR;
    }

  if (bk7258_flash_write(state->base + (uint32_t)offset, buffer,
                         nbytes) < 0)
    {
      bk7258_flash_mtd_unlock();
      return -EIO;
    }

  bk7258_flash_mtd_unlock();
  return (ssize_t)nbytes;
}
#endif

static int bk7258_flash_ioctl(FAR struct mtd_dev_s *dev, int cmd,
                              unsigned long arg)
{
  FAR struct bk7258_flash_mtd_s *state = bk7258_flash_mtd_state(dev);

  if (state == NULL)
    {
      return -EINVAL;
    }

  switch (cmd)
    {
      case MTDIOC_GEOMETRY:
        {
          FAR struct mtd_geometry_s *geo =
              (FAR struct mtd_geometry_s *)arg;

          if (geo == NULL)
            {
              return -EINVAL;
            }

          geo->blocksize    = BK7258_FLASH_BLOCK_SIZE;
          geo->erasesize    = BK7258_FLASH_ERASE_SIZE;
          geo->neraseblocks = state->size / BK7258_FLASH_ERASE_SIZE;
          strncpy(geo->model, state->name, sizeof(geo->model) - 1u);
          geo->model[sizeof(geo->model) - 1u] = '\0';
        }
        return OK;

      case MTDIOC_ERASESTATE:
        {
          FAR uint8_t *erase_state = (FAR uint8_t *)arg;

          if (erase_state == NULL)
            {
              return -EINVAL;
            }

          *erase_state = 0xff;
        }
        return OK;

      default:
        break;
    }

  return -ENOTTY;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static void bk7258_flash_mtd_bind(FAR struct bk7258_flash_mtd_s *state)
{
  state->mtd.erase  = bk7258_flash_mtd_erase;
  state->mtd.bread  = bk7258_flash_mtd_bread;
  state->mtd.bwrite = bk7258_flash_mtd_bwrite;
  state->mtd.read   = bk7258_flash_mtd_read;
#ifdef CONFIG_MTD_BYTE_WRITE
  state->mtd.write  = bk7258_flash_mtd_write;
#endif
  state->mtd.ioctl  = bk7258_flash_ioctl;
  state->mtd.isbad  = NULL;
  state->mtd.markbad = NULL;
  state->mtd.name   = state->name;
}

FAR struct mtd_dev_s *bk7258_flash_mtd_initialize(void)
{
  if (g_bk7258_flash_mtd_initialized)
    {
      return &g_bk7258_data_mtd.mtd;
    }

  /* The chip service owns controller initialization and JEDEC validation. */

  if (bk7258_flash_initialize() < 0)
    {
      ferr("ERROR: BK7258 raw Flash initialization failed\n");
      return NULL;
    }

  bk7258_flash_mtd_bind(&g_bk7258_data_mtd);
  g_bk7258_flash_mtd_initialized = true;
  return &g_bk7258_data_mtd.mtd;
}
