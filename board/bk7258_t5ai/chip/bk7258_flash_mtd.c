/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_flash_mtd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) on-chip flash data-partition MTD lower-half.
 *
 * Exposes the 1 MiB data partition (logical 0x00100000..0x001FFFFF) as a
 * NuttX MTD.  Read/erase/write use the board-verified flash-controller
 * sequence:
 *
 *   wait BUSY clear -> OP_CMD = (addr[23:0] | op<<24) -> set OP_CTRL.OP_SW
 *   -> wait BUSY clear -> read DATA_IN / stage DATA_SW_FLASH.
 *
 * Geometry is fixed for the GD25Q64-class part:
 *   blocksize  = 4096  (read/write block unit)
 *   erasesize  = 4096  (sector erase unit, a multiple of blocksize)
 *   neraseblocks = 256 (1 MiB / 4 KiB)
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/mtd/mtd.h>

#include "bk7258_flash_mtd.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Flash controller MMIO (Armino SOC_FLASH_REG_BASE = 0x44030000). */

#define BK7258_FREG(a)              (*(volatile uint32_t *)(a))

#define BK7258_FLASH_BASE           0x44030000u
#define BK7258_FLASH_OP_CTRL        (BK7258_FLASH_BASE + 0x10u)
#define BK7258_FLASH_DATA_SW_FLASH  (BK7258_FLASH_BASE + 0x14u)
#define BK7258_FLASH_DATA_IN        (BK7258_FLASH_BASE + 0x18u)
#define BK7258_FLASH_CMD_CFG        (BK7258_FLASH_BASE + 0x1cu)
#define BK7258_FLASH_RD_ID          (BK7258_FLASH_BASE + 0x20u)
#define BK7258_FLASH_STATE         (BK7258_FLASH_BASE + 0x24u)
#define BK7258_FLASH_CONF          (BK7258_FLASH_BASE + 0x28u)
#define BK7258_FLASH_OP_CMD         (BK7258_FLASH_BASE + 0x54u)

#define BK7258_FLASH_BUSY_BIT       (1u << 31)
#define BK7258_FLASH_OP_SW_BIT      (1u << 29)
#define BK7258_FLASH_WP_VALUE_BIT   (1u << 30)

#define BK7258_FLASH_OP_CMD_WREN   1u
#define BK7258_FLASH_OP_CMD_RDSR   3u
#define BK7258_FLASH_OP_CMD_WRSR   4u
#define BK7258_FLASH_OP_CMD_READ   5u
#define BK7258_FLASH_OP_CMD_PP     12u
#define BK7258_FLASH_OP_CMD_SE     13u
#define BK7258_FLASH_OP_CMD_RDID   20u

/* SR0 block-protect bits (GD25Q64-class): BP0/BP1/BP2/TB/SEC/CMP at
 * bits 2..6.  The default SR0=0x1c observed on this board protects a range
 * that includes the boot/app region; writes only succeed while these are
 * cleared.  WIP(bit0)/WEL(bit1) are runtime, never written here.
 */

#define BK7258_FLASH_SR_PROTECT_MASK 0x7cu
#define BK7258_FLASH_WRSR_DATA_SHIFT 10u
#define BK7258_FLASH_WRSR_DATA_MASK  (0xffffu << BK7258_FLASH_WRSR_DATA_SHIFT)

/* Verified data partition layout (matches docs n5-flash-filesystem.md). */

#define BK7258_DATA_PART_BASE       0x00100000u
#define BK7258_DATA_PART_SIZE       0x00100000u

#define BK7258_FLASH_BLOCK_SIZE     4096u
#define BK7258_FLASH_ERASE_SIZE     4096u
#define BK7258_FLASH_NBLOCKS        (BK7258_DATA_PART_SIZE / BK7258_FLASH_BLOCK_SIZE)

#define BK7258_FLASH_TIMEOUT        1000000u

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

/****************************************************************************
 * Private Helpers
 ****************************************************************************/

static inline int bk7258_flash_wait_ready(void)
{
  uint32_t timeout = BK7258_FLASH_TIMEOUT;

  while ((BK7258_FREG(BK7258_FLASH_OP_CTRL) & BK7258_FLASH_BUSY_BIT) != 0)
    {
      if (--timeout == 0)
        {
          return -ETIMEDOUT;
        }
    }

  return OK;
}

/* Read 16 bytes (4 words) from an absolute flash address using the proven
 * READ op sequence.  Returns OK/-errno.  Only valid at 0x20-aligned addresses
 * (the controller READ is a 32-byte burst).  Retained as a utility; not all
 * configurations reference it from outside the driver.
 */

static int __attribute__((unused)) bk7258_flash_read16(uint32_t addr,
                                                       uint32_t out[4])
{
  uint32_t ctrl;
  unsigned int i;

  if (bk7258_flash_wait_ready() < 0)
    {
      return -ETIMEDOUT;
    }

  /* op_cmd: addr[23:0] | (op_type_sw << 24), READ = 5. */

  BK7258_FREG(BK7258_FLASH_OP_CMD) =
      (addr & 0x00ffffffu) | (BK7258_FLASH_OP_CMD_READ << 24);

  ctrl = BK7258_FREG(BK7258_FLASH_OP_CTRL);
  BK7258_FREG(BK7258_FLASH_OP_CTRL) = ctrl | BK7258_FLASH_OP_SW_BIT;

  if (bk7258_flash_wait_ready() < 0)
    {
      return -ETIMEDOUT;
    }

  for (i = 0; i < 4; i++)
    {
      out[i] = BK7258_FREG(BK7258_FLASH_DATA_IN);
    }

  return OK;
}

/* Read 32 bytes (8 words) from a 0x20-aligned flash address.  This is the
 * SDK-proven READ granularity (flash_read_data: 8 words per op, addr += 32);
 * the controller READ is a 32-byte burst, so reads at non-0x20-aligned
 * addresses return wrong data.  bread uses this exclusively.
 */

static int bk7258_flash_read32(uint32_t addr, uint32_t out[8])
{
  uint32_t ctrl;
  unsigned int i;

  if (bk7258_flash_wait_ready() < 0)
    {
      return -ETIMEDOUT;
    }

  BK7258_FREG(BK7258_FLASH_OP_CMD) =
      (addr & 0x00ffffffu) | (BK7258_FLASH_OP_CMD_READ << 24);

  ctrl = BK7258_FREG(BK7258_FLASH_OP_CTRL);
  BK7258_FREG(BK7258_FLASH_OP_CTRL) = ctrl | BK7258_FLASH_OP_SW_BIT;

  if (bk7258_flash_wait_ready() < 0)
    {
      return -ETIMEDOUT;
    }

  for (i = 0; i < 8; i++)
    {
      out[i] = BK7258_FREG(BK7258_FLASH_DATA_IN);
    }

  return OK;
}

static uint32_t bk7258_flash_read_id(void)
{
  uint32_t ctrl;

  if (bk7258_flash_wait_ready() < 0)
    {
      return 0;
    }

  BK7258_FREG(BK7258_FLASH_OP_CMD) =
      (uint32_t)BK7258_FLASH_OP_CMD_RDID << 24;

  ctrl = BK7258_FREG(BK7258_FLASH_OP_CTRL);
  BK7258_FREG(BK7258_FLASH_OP_CTRL) = ctrl | BK7258_FLASH_OP_SW_BIT;

  if (bk7258_flash_wait_ready() < 0)
    {
      return 0;
    }

  return BK7258_FREG(BK7258_FLASH_RD_ID);
}

/* Trigger a flash controller op at `addr` with op-type `op`, without touching
 * the WP_VALUE bit.  This is the sequence N5-D5 board-verified for READ/SE/PP
 * after SR0 protection has been cleared.
 */

static int bk7258_flash_swop(uint32_t addr, uint32_t op)
{
  uint32_t ctrl;

  if (bk7258_flash_wait_ready() < 0)
    {
      return -ETIMEDOUT;
    }

  BK7258_FREG(BK7258_FLASH_OP_CMD) =
      (addr & 0x00ffffffu) | (op << 24);

  ctrl = BK7258_FREG(BK7258_FLASH_OP_CTRL);
  BK7258_FREG(BK7258_FLASH_OP_CTRL) = ctrl | BK7258_FLASH_OP_SW_BIT;

  return bk7258_flash_wait_ready();
}

static int bk7258_flash_wren(void)
{
  return bk7258_flash_swop(0u, BK7258_FLASH_OP_CMD_WREN);
}

static uint32_t bk7258_flash_read_sr(void)
{
  if (bk7258_flash_swop(0u, BK7258_FLASH_OP_CMD_RDSR) < 0)
    {
      return 0xffffffffu;
    }

  return BK7258_FREG(BK7258_FLASH_STATE) & 0xffu;
}

/* WRSR uses FLASH_CONF.wrsr_data[10:25] as the payload and needs WP_VALUE set
 * for the op (N5-D5 board-verified sequence).
 */

static int bk7258_flash_write_sr(uint8_t sr)
{
  uint32_t conf0;
  uint32_t conf;
  uint32_t ctrl;
  int ret;

  if (bk7258_flash_wren() < 0)
    {
      return -ETIMEDOUT;
    }

  BK7258_FREG(BK7258_FLASH_CMD_CFG) = 0u;

  conf0 = BK7258_FREG(BK7258_FLASH_CONF);
  conf = (conf0 & ~BK7258_FLASH_WRSR_DATA_MASK) |
         ((uint32_t)sr << BK7258_FLASH_WRSR_DATA_SHIFT);
  BK7258_FREG(BK7258_FLASH_CONF) = conf;

  if (bk7258_flash_wait_ready() < 0)
    {
      BK7258_FREG(BK7258_FLASH_CONF) = conf0;
      return -ETIMEDOUT;
    }

  BK7258_FREG(BK7258_FLASH_OP_CMD) = (uint32_t)BK7258_FLASH_OP_CMD_WRSR << 24;

  ctrl = BK7258_FREG(BK7258_FLASH_OP_CTRL);
  BK7258_FREG(BK7258_FLASH_OP_CTRL) =
      ctrl | BK7258_FLASH_OP_SW_BIT | BK7258_FLASH_WP_VALUE_BIT;

  ret = bk7258_flash_wait_ready();

  BK7258_FREG(BK7258_FLASH_OP_CTRL) =
      BK7258_FREG(BK7258_FLASH_OP_CTRL) & ~BK7258_FLASH_WP_VALUE_BIT;
  BK7258_FREG(BK7258_FLASH_CONF) = conf0;

  return ret;
}

/* N5-D6-B protection policy (option A): clear SR0 block-protect bits around
 * each erase/write call and restore the original value afterwards, so the
 * boot/app region keeps its hardware protection outside the op window.
 *
 * Returns: 1 if protection was changed (caller must restore), 0 if already
 * clear, negative errno on failure.
 */

static int bk7258_flash_unprotect(uint8_t *saved_sr)
{
  uint32_t sr;
  int ret;

  sr = bk7258_flash_read_sr();
  if (sr == 0xffffffffu)
    {
      _err("bk7258_flash_unprotect: SR0 read failed\n");
      return -EIO;
    }

  *saved_sr = (uint8_t)sr;

  if ((sr & BK7258_FLASH_SR_PROTECT_MASK) == 0u)
    {
      return 0;
    }

  ret = bk7258_flash_write_sr((uint8_t)(sr & ~BK7258_FLASH_SR_PROTECT_MASK));
  if (ret < 0)
    {
      /* Best-effort restore of the original SR0; ignore the return value
       * since we are already on the failure path.
       */

      (void)bk7258_flash_write_sr((uint8_t)sr);
      _err("bk7258_flash_unprotect: clear SR0 protect failed (ret=%d)\n", ret);
      return ret;
    }

  if ((bk7258_flash_read_sr() & BK7258_FLASH_SR_PROTECT_MASK) != 0u)
    {
      (void)bk7258_flash_write_sr((uint8_t)sr);
      _err("bk7258_flash_unprotect: SR0 protect bits still set after clear\n");
      return -EIO;
    }

  return 1;
}

static int bk7258_flash_restore(uint8_t saved_sr, int changed)
{
  if (changed)
    {
      int ret;

      /* Restore only the writable protect bits; leave WIP/WEL untouched. */

      ret = bk7258_flash_write_sr((uint8_t)(saved_sr & 0xfcu));
      if (ret < 0)
        {
          _err("bk7258_flash_restore: SR0 restore failed (ret=%d); "
               "boot/app region may be unprotected\n", ret);
        }

      return ret;
    }

  return OK;
}

/* Program one 32-byte chunk (8 words) at `addr` using the N5-D5 board-verified
 * PP sequence: WREN -> stage 8 words to DATA_SW_FLASH -> PP.  Caller must have
 * already cleared SR0 protection for the call.
 */

static int bk7258_flash_program32(uint32_t addr, FAR const uint8_t *buf)
{
  unsigned int i;

  if (bk7258_flash_wren() < 0)
    {
      return -ETIMEDOUT;
    }

  for (i = 0; i < 8; i++)
    {
      uint32_t w = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                   ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);

      BK7258_FREG(BK7258_FLASH_DATA_SW_FLASH) = w;
      buf += 4;
    }

  return bk7258_flash_swop(addr, BK7258_FLASH_OP_CMD_PP);
}

/****************************************************************************
 * MTD Methods
 ****************************************************************************/

/* Read whole 4 KiB blocks.  startblock/nblocks are in block units; each block
 * is read 32 bytes at a time from 0x20-aligned addresses (the controller READ
 * is a 32-byte burst, so mid-burst offsets return wrong data).
 */

static ssize_t bk7258_flash_bread(FAR struct mtd_dev_s *dev, off_t startblock,
                                  size_t nblocks, FAR uint8_t *buffer)
{
  size_t block;
  size_t off;
  uint32_t addr;
  uint32_t wbuf[8];
  FAR uint8_t *p;

  (void)dev;

  if (startblock < 0 || (size_t)startblock + nblocks > BK7258_FLASH_NBLOCKS)
    {
      return -EINVAL;
    }

  p = buffer;

  for (block = 0; block < nblocks; block++)
    {
      addr = BK7258_DATA_PART_BASE +
             ((size_t)startblock + block) * BK7258_FLASH_BLOCK_SIZE;

      for (off = 0; off < BK7258_FLASH_BLOCK_SIZE; off += 32u)
        {
          unsigned int i;

          if (bk7258_flash_read32(addr + off, wbuf) < 0)
            {
              return -EIO;
            }

          for (i = 0; i < 8; i++)
            {
              FAR uint8_t *wp = (FAR uint8_t *)&wbuf[i];
              unsigned int b;

              for (b = 0; b < 4; b++)
                {
                  *p++ = wp[b];
                }
            }
        }
    }

  return nblocks;
}

/* Erase whole 4 KiB sectors.  SR0 protection is cleared once at entry and
 * restored at exit (N5-D6-B option A); each sector uses the verified
 * swop(addr, SE) sequence.
 */

static int bk7258_flash_erase(FAR struct mtd_dev_s *dev, off_t startblock,
                              size_t nblocks)
{
  uint8_t saved_sr;
  size_t block;
  int changed;

  (void)dev;

  if (startblock < 0 || (size_t)startblock + nblocks > BK7258_FLASH_NBLOCKS)
    {
      return -EINVAL;
    }

  changed = bk7258_flash_unprotect(&saved_sr);
  if (changed < 0)
    {
      return changed;
    }

  for (block = 0; block < nblocks; block++)
    {
      uint32_t addr = BK7258_DATA_PART_BASE +
                      ((size_t)startblock + block) * BK7258_FLASH_BLOCK_SIZE;

      if (bk7258_flash_swop(addr, BK7258_FLASH_OP_CMD_SE) < 0)
        {
          /* Op failed: best-effort restore, then report the failure. */

          (void)bk7258_flash_restore(saved_sr, changed > 0);
          return -EIO;
        }
    }

  /* Op succeeded: data is committed.  A restore failure here must not turn
   * a successful erase into a failure (the sectors are already erased), so
   * the restore return value is intentionally ignored.
   */

  (void)bk7258_flash_restore(saved_sr, changed > 0);
  return OK;
}

/* Write whole 4 KiB blocks.  Programs 32 bytes at a time via the verified PP
 * sequence (WREN -> 8 words -> PP), SR0 protection cleared/restored around
 * the whole call.  The caller (filesystem layer) is responsible for erasing
 * the block first; programming only flips bits 1->0.
 */

static ssize_t bk7258_flash_bwrite(FAR struct mtd_dev_s *dev, off_t startblock,
                                   size_t nblocks, FAR const uint8_t *buffer)
{
  uint8_t saved_sr;
  size_t block;
  size_t off;
  int changed;

  (void)dev;

  if (startblock < 0 || (size_t)startblock + nblocks > BK7258_FLASH_NBLOCKS)
    {
      return -EINVAL;
    }

  changed = bk7258_flash_unprotect(&saved_sr);
  if (changed < 0)
    {
      return changed;
    }

  for (block = 0; block < nblocks; block++)
    {
      uint32_t base = BK7258_DATA_PART_BASE +
                      ((size_t)startblock + block) * BK7258_FLASH_BLOCK_SIZE;
      FAR const uint8_t *p = buffer + block * BK7258_FLASH_BLOCK_SIZE;

      for (off = 0; off < BK7258_FLASH_BLOCK_SIZE; off += 32u)
        {
          if (bk7258_flash_program32(base + off, p + off) < 0)
            {
              /* Op failed: best-effort restore, then report the failure. */

              (void)bk7258_flash_restore(saved_sr, changed > 0);
              return -EIO;
            }
        }
    }

  /* Op succeeded: data is committed.  A restore failure here must not turn
   * a successful write into a failure (data is already on flash), so the
   * restore return value is intentionally ignored.
   */

  (void)bk7258_flash_restore(saved_sr, changed > 0);
  return nblocks;
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

  id = bk7258_flash_read_id() & 0x00ffffffu;

  /* Reject anything that is not the verified 8 MiB GD25Q64-class part. */

  if (id != 0x00c86517u && id != 0x00c84017u &&
      id != 0x000b4017u && id != 0x00cd6017u)
    {
      return NULL;
    }

  return &g_bk7258_flash_mtd.mtd;
}
