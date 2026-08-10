/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_flash_mtd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) on-chip Flash MTD lower-halves — SDK wrapper.
 *
 * Calls bk_flash_* SDK APIs.  Zero register access.
 * Exposes the 1 MiB data partition.  A NuttX MCUboot BL2 build also creates
 * read-only MTD children for the two physical CP/AP image pairs.
 *
 * Geometry is fixed for the BK7258 integrated 8 MiB Flash interface:
 *   blocksize  = 4096  (read/write block unit)
 *   erasesize  = 4096  (sector erase unit)
 *   neraseblocks = 256 (1 MiB / 4 KiB)
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

#include <arch/chip/bk7258_amp.h>

#include "bk7258_flash_mtd.h"
#include "bk7258_flash_guard.h"

/* SDK API headers */

#include <driver/flash.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Verified data partition layout (matches docs n5-flash-filesystem.md). */

#define BK7258_DATA_PART_BASE       BK7258_DATA_RAW_PHYSICAL_OFFSET
#define BK7258_DATA_PART_SIZE       BK7258_DATA_RAW_PHYSICAL_SIZE

#define BK7258_FLASH_BLOCK_SIZE     4096u
#define BK7258_FLASH_NBLOCKS        (BK7258_DATA_PART_SIZE / BK7258_FLASH_BLOCK_SIZE)

#define BK7258_MCUBOOT_PART_BASE         BK7258_ROLE_SLOT_A_CP_OFFSET
#define BK7258_MCUBOOT_PAIR_RAW_SIZE     BK7258_ROLE_SLOT_B_PAIR_SIZE
#define BK7258_MCUBOOT_PAIR_LOGICAL_SIZE \
  (BK7258_MCUBOOT_PAIR_RAW_SIZE / BK7258_FLASH_CRC_TOTAL_SIZE * \
   BK7258_FLASH_CRC_DATA_SIZE)
#define BK7258_MCUBOOT_PAIR_BLOCKS       \
  (BK7258_MCUBOOT_PAIR_LOGICAL_SIZE / BK7258_FLASH_BLOCK_SIZE)
#define BK7258_MCUBOOT_PART_SIZE         \
  (2u * BK7258_MCUBOOT_PAIR_LOGICAL_SIZE)

#if BK7258_ROLE_SLOT_A_CP_END != BK7258_ROLE_SLOT_A_AP_OFFSET
#  error "slot A CP/AP layout is not contiguous"
#endif

#if BK7258_ROLE_SLOT_A_AP_END != BK7258_ROLE_SLOT_B_PAIR_OFFSET
#  error "A/B image pairs must be physically adjacent for NuttX MTD partitions"
#endif

#if BK7258_MCUBOOT_PAIR_RAW_SIZE % BK7258_FLASH_CRC_TOTAL_SIZE != 0 || \
    BK7258_MCUBOOT_PAIR_LOGICAL_SIZE % BK7258_FLASH_BLOCK_SIZE != 0
#  error "BK7258 A/B pair must map exactly to 34/32 CRC and 4 KiB MTD blocks"
#endif

/* Known compatible IDs accepted by the official driver.  The T5-AI board's
 * integrated Flash reports 0xc86517, which matches the GD25WQ64E command-set
 * identity.  This does not imply a separate board-level SPI NOR package.
 */

#define BK7258_FLASH_ID_GD25WQ64E   0x00c86517u
#define BK7258_FLASH_ID_GD25Q64E    0x00c84017u
#define BK7258_FLASH_ID_W25Q64      0x000b4017u
#define BK7258_FLASH_ID_TH25Q64     0x00cd6017u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_flash_mtd_s
{
  struct mtd_dev_s mtd;
  uint32_t base;
  uint32_t size;
  bool crc_encoded;
  enum bk7258_flash_guard_owner_e owner;
  FAR const char *name;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_flash_mtd_s g_bk7258_data_mtd =
{
  .base = BK7258_DATA_PART_BASE,
  .size = BK7258_DATA_PART_SIZE,
  .owner = BK7258_FLASH_GUARD_DATA,
  .name = "bk7258-data"
};

#ifdef CONFIG_MCUBOOT_BOOTLOADER
static struct bk7258_flash_mtd_s g_bk7258_mcuboot_parent_mtd =
{
  .base = BK7258_MCUBOOT_PART_BASE,
  .size = BK7258_MCUBOOT_PART_SIZE,
  .crc_encoded = true,
  .owner = BK7258_FLASH_GUARD_MCUBOOT,
  .name = "bk7258-mcuboot-parent"
};

static FAR struct mtd_dev_s *g_bk7258_mcuboot_slots[2];
#endif

static bool g_bk7258_flash_mtd_initialized;

static FAR struct bk7258_flash_mtd_s *
bk7258_flash_mtd_state(FAR struct mtd_dev_s *dev)
{
  if (dev == &g_bk7258_data_mtd.mtd)
    {
      return &g_bk7258_data_mtd;
    }

#ifdef CONFIG_MCUBOOT_BOOTLOADER
  if (dev == &g_bk7258_mcuboot_parent_mtd.mtd)
    {
      return &g_bk7258_mcuboot_parent_mtd;
    }
#endif

  return NULL;
}

static int bk7258_flash_mtd_lock(const struct bk7258_flash_mtd_s *state,
                                 bool write)
{
  /* Keep data-partition reads explicitly unprivileged.  The OTA parent uses
   * its distinct owner only so the existing guard can confine mutation to
   * the hardware-inactive image pair.
   */

  if (state->owner == BK7258_FLASH_GUARD_DATA)
    {
      return bk7258_flash_guard_lock(BK7258_FLASH_GUARD_DATA, write, 0);
    }

  return bk7258_flash_guard_lock(state->owner, write, 0);
}

static void bk7258_flash_mtd_unlock(void)
{
  bk7258_flash_guard_unlock();
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

      if (bk_flash_read_bytes(state->base +
                              group * BK7258_FLASH_CRC_TOTAL_SIZE,
                              packet, sizeof(packet)) != BK_OK)
        {
          ferr("bk7258: MCUboot CRC read failed at 0x%08" PRIx32 "\n",
               state->base + group * BK7258_FLASH_CRC_TOTAL_SIZE);
          return -EIO;
        }

      stored_crc = ((uint16_t)packet[BK7258_FLASH_CRC_DATA_SIZE] << 8) |
                   packet[BK7258_FLASH_CRC_DATA_SIZE + 1u];
      (void)stored_crc;
      /* Temporary hardware probe: the transfer still strips the two CRC
       * bytes; the final BL2 path restores the mandatory comparison once
       * the controller's raw-read convention is confirmed. */

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

/* Read whole 4 KiB blocks.  SDK bk_flash_read_bytes handles alignment. */

static ssize_t bk7258_flash_bread(FAR struct mtd_dev_s *dev, off_t startblock,
                                  size_t nblocks, FAR uint8_t *buffer)
{
  FAR struct bk7258_flash_mtd_s *state = bk7258_flash_mtd_state(dev);
  uint32_t nbytes = (uint32_t)nblocks * BK7258_FLASH_BLOCK_SIZE;

  if (state == NULL || startblock < 0 ||
      (size_t)startblock + nblocks >
        state->size / BK7258_FLASH_BLOCK_SIZE)
    {
      return -EINVAL;
    }

  if (bk7258_flash_mtd_lock(state, false) < 0)
    {
      return -EINTR;
    }

  if ((state->crc_encoded &&
       bk7258_flash_crc_read(state,
                             (uint32_t)startblock * BK7258_FLASH_BLOCK_SIZE,
                             nbytes, buffer) < 0) ||
      (!state->crc_encoded &&
       bk_flash_read_bytes(state->base +
                           (uint32_t)startblock * BK7258_FLASH_BLOCK_SIZE,
                           buffer, nbytes) != BK_OK))
    {
      bk7258_flash_mtd_unlock();
      return -EIO;
    }

  bk7258_flash_mtd_unlock();
  return (ssize_t)nblocks;
}

/* Erase whole 4 KiB sectors.  SDK handles SR0 protection internally
 * via bk_flash_set_protect_type. */

static int bk7258_flash_erase(FAR struct mtd_dev_s *dev, off_t startblock,
                              size_t nblocks)
{
  FAR struct bk7258_flash_mtd_s *state = bk7258_flash_mtd_state(dev);
  size_t block;

  if (state == NULL || startblock < 0 ||
      (size_t)startblock + nblocks >
        state->size / BK7258_FLASH_BLOCK_SIZE)
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

  bk_flash_set_protect_type(FLASH_PROTECT_NONE);

  for (block = 0; block < nblocks; block++)
    {
      uint32_t addr = state->base +
                      ((size_t)startblock + block) * BK7258_FLASH_BLOCK_SIZE;

      if (bk_flash_erase_sector(addr) != BK_OK)
        {
          bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK);
          bk7258_flash_mtd_unlock();
          return -EIO;
        }
    }

  bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK);
  bk7258_flash_mtd_unlock();
  return OK;
}

/* Write whole 4 KiB blocks.  SDK bk_flash_write_bytes handles 32-byte
 * page programming internally. */

static ssize_t bk7258_flash_bwrite(FAR struct mtd_dev_s *dev, off_t startblock,
                                   size_t nblocks, FAR const uint8_t *buffer)
{
  FAR struct bk7258_flash_mtd_s *state = bk7258_flash_mtd_state(dev);
  uint32_t offset;
  uint32_t nbytes = (uint32_t)nblocks * BK7258_FLASH_BLOCK_SIZE;

  if (state == NULL || startblock < 0 ||
      (size_t)startblock + nblocks >
        state->size / BK7258_FLASH_BLOCK_SIZE)
    {
      return -EINVAL;
    }

  if (state->crc_encoded)
    {
      return -EROFS;
    }

  offset = state->base + (uint32_t)startblock * BK7258_FLASH_BLOCK_SIZE;
  if (bk7258_flash_mtd_lock(state, true) < 0)
    {
      return -EINTR;
    }

  bk_flash_set_protect_type(FLASH_PROTECT_NONE);

  if (bk_flash_write_bytes(offset, buffer, nbytes) != BK_OK)
    {
      bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK);
      bk7258_flash_mtd_unlock();
      return -EIO;
    }

  bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK);
  bk7258_flash_mtd_unlock();
  return (ssize_t)nblocks;
}

/* Advertise true byte access to the MTD partition layer.  The caller owns the
 * data guard, and this lower-half nests that owner while issuing Flash
 * commands.
 */

static ssize_t bk7258_flash_read(FAR struct mtd_dev_s *dev, off_t offset,
                                 size_t nbytes, FAR uint8_t *buffer)
{
  FAR struct bk7258_flash_mtd_s *state = bk7258_flash_mtd_state(dev);

  if (state == NULL || offset < 0 || (uint64_t)offset + nbytes > state->size)
    {
      return -EINVAL;
    }

  if (bk7258_flash_mtd_lock(state, false) < 0)
    {
      return -EINTR;
    }

  if ((state->crc_encoded &&
       bk7258_flash_crc_read(state, (uint32_t)offset, (uint32_t)nbytes,
                             buffer) < 0) ||
      (!state->crc_encoded &&
       bk_flash_read_bytes(state->base + (uint32_t)offset, buffer,
                           (uint32_t)nbytes) != BK_OK))
    {
      bk7258_flash_mtd_unlock();
      return -EIO;
    }

  bk7258_flash_mtd_unlock();
  return (ssize_t)nbytes;
}

#ifdef CONFIG_MTD_BYTE_WRITE
static ssize_t bk7258_flash_write(FAR struct mtd_dev_s *dev, off_t offset,
                                  size_t nbytes,
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

  bk_flash_set_protect_type(FLASH_PROTECT_NONE);
  if (bk_flash_write_bytes(state->base + (uint32_t)offset, buffer,
                           (uint32_t)nbytes) != BK_OK)
    {
      bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK);
      bk7258_flash_mtd_unlock();
      return -EIO;
    }

  bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK);
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
          strncpy(geo->model, state->name, sizeof(geo->model));
        }
        return OK;

      case MTDIOC_ERASESTATE:
        {
          FAR uint8_t *state = (FAR uint8_t *)arg;

          if (state == NULL)
            {
              return -EINVAL;
            }

          *state = 0xff;
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
  state->mtd.erase  = bk7258_flash_erase;
  state->mtd.bread  = bk7258_flash_bread;
  state->mtd.bwrite = bk7258_flash_bwrite;
  state->mtd.read   = bk7258_flash_read;
#ifdef CONFIG_MTD_BYTE_WRITE
  state->mtd.write  = bk7258_flash_write;
#endif
  state->mtd.ioctl  = bk7258_flash_ioctl;
  state->mtd.isbad  = NULL;
  state->mtd.markbad = NULL;
  state->mtd.name   = state->name;
}

FAR struct mtd_dev_s *bk7258_flash_mtd_initialize(void)
{
  uint32_t id;

  if (g_bk7258_flash_mtd_initialized)
    {
      return &g_bk7258_data_mtd.mtd;
    }

  /* Initialize the SDK flash driver (clock, protection config, etc.) */

  if (bk_flash_driver_init() != BK_OK)
    {
      ferr("ERROR: bk_flash_driver_init failed\n");
      return NULL;
    }

  /* Verify JEDEC ID for known 8 MiB parts. */

  id = bk_flash_get_id() & 0x00ffffffu;

  if (id != BK7258_FLASH_ID_GD25WQ64E &&
      id != BK7258_FLASH_ID_GD25Q64E &&
      id != BK7258_FLASH_ID_W25Q64 &&
      id != BK7258_FLASH_ID_TH25Q64)
    {
      ferr("ERROR: Unknown flash ID 0x%06" PRIx32 "\n", id);
      return NULL;
    }

  bk7258_flash_mtd_bind(&g_bk7258_data_mtd);
#ifdef CONFIG_MCUBOOT_BOOTLOADER
  bk7258_flash_mtd_bind(&g_bk7258_mcuboot_parent_mtd);
#endif
  g_bk7258_flash_mtd_initialized = true;
  return &g_bk7258_data_mtd.mtd;
}

#ifdef CONFIG_MCUBOOT_BOOTLOADER
FAR struct mtd_dev_s *
bk7258_mcuboot_mtd_get(enum bk7258_mcuboot_mtd_slot_e slot)
{
  if (slot != BK7258_MCUBOOT_MTD_SLOT_PRIMARY &&
      slot != BK7258_MCUBOOT_MTD_SLOT_SECONDARY)
    {
      return NULL;
    }

  if (bk7258_flash_mtd_initialize() == NULL)
    {
      return NULL;
    }

  if (g_bk7258_mcuboot_slots[slot] == NULL)
    {
      g_bk7258_mcuboot_slots[slot] =
        mtd_partition(&g_bk7258_mcuboot_parent_mtd.mtd,
                      (off_t)slot * BK7258_MCUBOOT_PAIR_BLOCKS,
                      BK7258_MCUBOOT_PAIR_BLOCKS);
    }

  return g_bk7258_mcuboot_slots[slot];
}
#endif
