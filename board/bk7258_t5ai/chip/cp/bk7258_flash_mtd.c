/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/cp/bk7258_flash_mtd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) on-chip flash data-partition MTD lower-half — SDK wrapper.
 *
 * Calls bk_flash_* SDK APIs.  Zero register access.
 * Exposes the 1 MiB data partition (logical 0x00100000..0x001FFFFF) as a
 * NuttX MTD.
 *
 * Geometry is fixed for the GD25Q64-class part:
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

#include <nuttx/arch.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>

#include <arch/chip/bk7258_amp.h>

#include "bk7258_flash_mtd.h"

/* SDK API headers */

#include <driver/flash.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Verified data partition layout (matches docs n5-flash-filesystem.md). */

#define BK7258_DATA_PART_BASE       BK7258_DATA_FLASH_OFFSET
#define BK7258_DATA_PART_SIZE       BK7258_DATA_FLASH_SIZE

#define BK7258_FLASH_BLOCK_SIZE     4096u
#define BK7258_FLASH_ERASE_SIZE     4096u
#define BK7258_FLASH_NBLOCKS        (BK7258_DATA_PART_SIZE / BK7258_FLASH_BLOCK_SIZE)

/* The v3.1.1.9 SDK enables CONFIG_FLASH_PARTITION_CHECK_VALID and its
 * generated partition table still describes the vendor application layout.
 * That table classifies this project's 0x00100000..0x001fffff data window as
 * part of a read-only application partition.  Keep the SDK permission gate
 * for every other caller and use the linker's --wrap hook to grant only the
 * board MTD owner access to this exact range.
 */

#define BK7258_FLASH_API_MAGIC_CODE 0x12345678u

/* Known JEDEC IDs for 8 MiB GD25Q64-class parts on this board. */

#define BK7258_FLASH_ID_GD25Q64     0x00c86517u
#define BK7258_FLASH_ID_GD25Q64B    0x00c84017u
#define BK7258_FLASH_ID_W25Q64      0x000b4017u
#define BK7258_FLASH_ID_TH25Q64     0x00cd6017u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_flash_mtd_s
{
  struct mtd_dev_s mtd;           /* External MTD interface */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_flash_mtd_s g_bk7258_flash_mtd;
static mutex_t g_bk7258_flash_mtd_lock = NXMUTEX_INITIALIZER;
static volatile pid_t g_bk7258_flash_mtd_owner = (pid_t)-1;

/****************************************************************************
 * SDK Partition-Permission Wrapper
 ****************************************************************************/

extern bk_err_t
__real_bk_flash_partition_write_perm_check_by_addr(uint32_t addr,
                                                    uint32_t size,
                                                    uint32_t magic_code);

static bool bk7258_flash_data_range(uint32_t addr, uint32_t size)
{
  uint32_t offset;

  if (size == 0 || addr < BK7258_DATA_PART_BASE)
    {
      return false;
    }

  offset = addr - BK7258_DATA_PART_BASE;
  return offset < BK7258_DATA_PART_SIZE &&
         size <= BK7258_DATA_PART_SIZE - offset;
}

bk_err_t __wrap_bk_flash_partition_write_perm_check_by_addr(
  uint32_t addr, uint32_t size, uint32_t magic_code)
{
  if (!up_interrupt_context() &&
      magic_code == BK7258_FLASH_API_MAGIC_CODE &&
      g_bk7258_flash_mtd_owner == nxsched_getpid() &&
      bk7258_flash_data_range(addr, size))
    {
      return BK_OK;
    }

  return __real_bk_flash_partition_write_perm_check_by_addr(addr, size,
                                                             magic_code);
}

static int bk7258_flash_mtd_lock(bool write)
{
  int ret = nxmutex_lock(&g_bk7258_flash_mtd_lock);

  if (ret >= 0 && write)
    {
      g_bk7258_flash_mtd_owner = nxsched_getpid();
      __asm volatile ("dmb sy" ::: "memory");
    }

  return ret;
}

static void bk7258_flash_mtd_unlock(void)
{
  __asm volatile ("dmb sy" ::: "memory");
  g_bk7258_flash_mtd_owner = (pid_t)-1;
  nxmutex_unlock(&g_bk7258_flash_mtd_lock);
}

/****************************************************************************
 * MTD Methods
 ****************************************************************************/

/* Read whole 4 KiB blocks.  SDK bk_flash_read_bytes handles alignment. */

static ssize_t bk7258_flash_bread(FAR struct mtd_dev_s *dev, off_t startblock,
                                  size_t nblocks, FAR uint8_t *buffer)
{
  uint32_t offset = BK7258_DATA_PART_BASE +
                    (uint32_t)startblock * BK7258_FLASH_BLOCK_SIZE;
  uint32_t nbytes = (uint32_t)nblocks * BK7258_FLASH_BLOCK_SIZE;

  (void)dev;

  if (startblock < 0 || (size_t)startblock + nblocks > BK7258_FLASH_NBLOCKS)
    {
      return -EINVAL;
    }

  if (bk7258_flash_mtd_lock(false) < 0)
    {
      return -EINTR;
    }

  if (bk_flash_read_bytes(offset, buffer, nbytes) != BK_OK)
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
  size_t block;

  (void)dev;

  if (startblock < 0 || (size_t)startblock + nblocks > BK7258_FLASH_NBLOCKS)
    {
      return -EINVAL;
    }

  if (bk7258_flash_mtd_lock(true) < 0)
    {
      return -EINTR;
    }

  bk_flash_set_protect_type(FLASH_PROTECT_NONE);

  for (block = 0; block < nblocks; block++)
    {
      uint32_t addr = BK7258_DATA_PART_BASE +
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
  uint32_t offset = BK7258_DATA_PART_BASE +
                    (uint32_t)startblock * BK7258_FLASH_BLOCK_SIZE;
  uint32_t nbytes = (uint32_t)nblocks * BK7258_FLASH_BLOCK_SIZE;

  (void)dev;

  if (startblock < 0 || (size_t)startblock + nblocks > BK7258_FLASH_NBLOCKS)
    {
      return -EINVAL;
    }

  if (bk7258_flash_mtd_lock(true) < 0)
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

static int bk7258_flash_ioctl(FAR struct mtd_dev_s *dev, int cmd,
                              unsigned long arg)
{
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
          geo->neraseblocks = BK7258_FLASH_NBLOCKS;
          strncpy(geo->model, "bk7258-data", sizeof(geo->model));
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

  (void)dev;
  return -ENOTTY;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct mtd_dev_s *bk7258_flash_mtd_initialize(void)
{
  uint32_t id;

  /* Initialize the SDK flash driver (clock, protection config, etc.) */

  if (bk_flash_driver_init() != BK_OK)
    {
      ferr("ERROR: bk_flash_driver_init failed\n");
      return NULL;
    }

  /* Singleton: the interface struct is statically populated. */

  g_bk7258_flash_mtd.mtd.erase  = bk7258_flash_erase;
  g_bk7258_flash_mtd.mtd.bread  = bk7258_flash_bread;
  g_bk7258_flash_mtd.mtd.bwrite = bk7258_flash_bwrite;
  g_bk7258_flash_mtd.mtd.read   = NULL;
#ifdef CONFIG_MTD_BYTE_WRITE
  g_bk7258_flash_mtd.mtd.write  = NULL;
#endif
  g_bk7258_flash_mtd.mtd.ioctl  = bk7258_flash_ioctl;
  g_bk7258_flash_mtd.mtd.isbad  = NULL;
  g_bk7258_flash_mtd.mtd.markbad = NULL;
  g_bk7258_flash_mtd.mtd.name   = "bk7258-data";

  /* Verify JEDEC ID for known 8 MiB parts. */

  id = bk_flash_get_id() & 0x00ffffffu;

  if (id != BK7258_FLASH_ID_GD25Q64 &&
      id != BK7258_FLASH_ID_GD25Q64B &&
      id != BK7258_FLASH_ID_W25Q64 &&
      id != BK7258_FLASH_ID_TH25Q64)
    {
      ferr("ERROR: Unknown flash ID 0x%06" PRIx32 "\n", id);
      return NULL;
    }

  return &g_bk7258_flash_mtd.mtd;
}
