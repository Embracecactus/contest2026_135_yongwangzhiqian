/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_flashdiag.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 Stage N5 flash layout, ID, read-only scan, and disabled-by-default
 * raw flash write verification helpers.
 *
 * Contract: the including translation unit must include arm_internal.h first
 * so getreg32()/putreg32() are available.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASHDIAG_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASHDIAG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

#include "bk7258_diag_uart.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Flash layout candidate verified during Stage N5. */

#define BK7258_CDIAG_FLASH_SIZE      0x00800000u
#define BK7258_CDIAG_IMAGE_LEN       0x0002837au
#define BK7258_CDIAG_FS_RESERVED_END 0x00100000u
#define BK7258_CDIAG_FS_DATA_START   0x00100000u
#define BK7258_CDIAG_FS_DATA_SIZE    0x00100000u
#define BK7258_CDIAG_FS_DATA_END     \
  (BK7258_CDIAG_FS_DATA_START + BK7258_CDIAG_FS_DATA_SIZE - 1u)

#define BK7258_CDIAG_FLASH_BASE          0x44030000u
#define BK7258_CDIAG_FLASH_OP_CTRL      (BK7258_CDIAG_FLASH_BASE + 0x10u)
#define BK7258_CDIAG_FLASH_DATA_SW_FLASH (BK7258_CDIAG_FLASH_BASE + 0x14u)
#define BK7258_CDIAG_FLASH_DATA_IN       (BK7258_CDIAG_FLASH_BASE + 0x18u)
#define BK7258_CDIAG_FLASH_CMD_CFG       (BK7258_CDIAG_FLASH_BASE + 0x1cu)
#define BK7258_CDIAG_FLASH_RD_ID        (BK7258_CDIAG_FLASH_BASE + 0x20u)
#define BK7258_CDIAG_FLASH_STATE        (BK7258_CDIAG_FLASH_BASE + 0x24u)
#define BK7258_CDIAG_FLASH_CONF         (BK7258_CDIAG_FLASH_BASE + 0x28u)
#define BK7258_CDIAG_FLASH_OP_CMD       (BK7258_CDIAG_FLASH_BASE + 0x54u)

#define BK7258_CDIAG_FLASH_BUSY_BIT     (1u << 31)
#define BK7258_CDIAG_FLASH_OP_SW_BIT    (1u << 29)
#define BK7258_CDIAG_FLASH_WP_VALUE_BIT (1u << 30)

#define BK7258_CDIAG_FLASH_TIMEOUT      100000u

#define BK7258_CDIAG_FLASH_OP_CMD_WREN  1u
#define BK7258_CDIAG_FLASH_OP_CMD_RDSR  3u
#define BK7258_CDIAG_FLASH_OP_CMD_WRSR  4u
#define BK7258_CDIAG_FLASH_OP_CMD_READ  5u
#define BK7258_CDIAG_FLASH_OP_CMD_RDSR2 6u
#define BK7258_CDIAG_FLASH_OP_CMD_PP    12u
#define BK7258_CDIAG_FLASH_OP_CMD_SE    13u
#define BK7258_CDIAG_FLASH_OP_CMD_RDID  20u

/* D5 is board-verified raw flash erase/write/read-back/re-erase only.  Keep
 * this destructive probe disabled in normal firmware; it is not an MTD or
 * filesystem verification.
 */

#define BK7258_CDIAG_ENABLE_N5_D5        0
#define BK7258_CDIAG_N5_D5_TOKEN         0u
#define BK7258_CDIAG_N5_D5_TOKEN_VALUE   0x4e354435u  /* "N5D5" */

#define BK7258_CDIAG_FS_D5_START         0x00100000u
#define BK7258_CDIAG_FS_D5_SECTOR_SIZE   0x00001000u
#define BK7258_CDIAG_FS_D5_WRITE_LEN     0x00000100u
#define BK7258_CDIAG_FS_D5_PATTERN       0xdeadbeefu

#define BK7258_CDIAG_FLASH_WRSR_DATA_SHIFT 10u
#define BK7258_CDIAG_FLASH_WRSR_DATA_MASK  \
  (0xffffu << BK7258_CDIAG_FLASH_WRSR_DATA_SHIFT)
#define BK7258_CDIAG_FLASH_SR1_PROTECT_MASK 0x7cu

/****************************************************************************
 * Public Functions (static inline)
 ****************************************************************************/

static inline void bk7258_clockdiag_fs_layout_dump(void)
{
  bk7258_clockdiag_puts("N5FS:L\r\n");

  bk7258_clockdiag_putreg("FSIZ", BK7258_CDIAG_FLASH_SIZE);
  bk7258_clockdiag_puts(" ");
  bk7258_clockdiag_putreg("IMGL", BK7258_CDIAG_IMAGE_LEN);
  bk7258_clockdiag_puts("\r\n");

  bk7258_clockdiag_putreg("REND", BK7258_CDIAG_FS_RESERVED_END - 1u);
  bk7258_clockdiag_puts("\r\n");

  bk7258_clockdiag_putreg("DSTA", BK7258_CDIAG_FS_DATA_START);
  bk7258_clockdiag_puts(" ");
  bk7258_clockdiag_putreg("DSIZ", BK7258_CDIAG_FS_DATA_SIZE);
  bk7258_clockdiag_puts(" ");
  bk7258_clockdiag_putreg("DEND", BK7258_CDIAG_FS_DATA_END);
  bk7258_clockdiag_puts("\r\n");
}




static inline int bk7258_clockdiag_flash_wait_ready(void)
{
  uint32_t timeout = BK7258_CDIAG_FLASH_TIMEOUT;

  while ((getreg32(BK7258_CDIAG_FLASH_OP_CTRL) &
          BK7258_CDIAG_FLASH_BUSY_BIT) != 0)
    {
      if (--timeout == 0)
        {
          return -1;
        }
    }

  return 0;
}



static inline uint32_t bk7258_clockdiag_flash_read_id(void)
{
  uint32_t cmd;
  uint32_t ctrl;

  if (bk7258_clockdiag_flash_wait_ready() < 0)
    {
      return 0;
    }

  /* op_cmd.op_type_sw is bits [24:28]. RDID command is controller op 20. */

  cmd = BK7258_CDIAG_FLASH_OP_CMD_RDID << 24;
  putreg32(cmd, BK7258_CDIAG_FLASH_OP_CMD);

  ctrl = getreg32(BK7258_CDIAG_FLASH_OP_CTRL);
  putreg32(ctrl | BK7258_CDIAG_FLASH_OP_SW_BIT,
            BK7258_CDIAG_FLASH_OP_CTRL);

  if (bk7258_clockdiag_flash_wait_ready() < 0)
    {
      return 0;
    }

  return getreg32(BK7258_CDIAG_FLASH_RD_ID);
}


static inline uint32_t bk7258_clockdiag_flash_size_from_id(uint32_t id)
{
  switch (id & 0x00ffffffu)
    {
      case 0x000b4017u:
      case 0x00c84017u:
      case 0x00c86517u:
      case 0x00cd6017u:
        return 0x00800000u;

      default:
        return 0;
    }
}
static inline int bk7258_clockdiag_flash_read16(uint32_t addr,
                                                uint32_t out[4])
{
  uint32_t cmd;
  uint32_t ctrl;
  unsigned int i;

  if (bk7258_clockdiag_flash_wait_ready() < 0)
    {
      return -1;
    }

  cmd = (addr & 0x00ffffffu) |
        (BK7258_CDIAG_FLASH_OP_CMD_READ << 24);
  putreg32(cmd, BK7258_CDIAG_FLASH_OP_CMD);

  ctrl = getreg32(BK7258_CDIAG_FLASH_OP_CTRL);
  putreg32(ctrl | BK7258_CDIAG_FLASH_OP_SW_BIT,
            BK7258_CDIAG_FLASH_OP_CTRL);

  if (bk7258_clockdiag_flash_wait_ready() < 0)
    {
      return -1;
    }

  for (i = 0; i < 4; i++)
    {
      out[i] = getreg32(BK7258_CDIAG_FLASH_DATA_IN);
    }

  return 0;
}
static inline void bk7258_clockdiag_flash_dump16(uint32_t addr)
{
  uint32_t buf[4];

  bk7258_clockdiag_putreg("OFF", addr);
  bk7258_clockdiag_puts(" ");

  if (bk7258_clockdiag_flash_read16(addr, buf) < 0)
    {
      bk7258_clockdiag_puts("TIMEOUT\r\n");
      return;
    }

  bk7258_clockdiag_putreg("W0", buf[0]);
  bk7258_clockdiag_puts(" ");
  bk7258_clockdiag_putreg("W1", buf[1]);
  bk7258_clockdiag_puts(" ");
  bk7258_clockdiag_putreg("W2", buf[2]);
  bk7258_clockdiag_puts(" ");
  bk7258_clockdiag_putreg("W3", buf[3]);
  bk7258_clockdiag_puts("\r\n");
}
#if BK7258_CDIAG_ENABLE_N5_D5 && \
    (BK7258_CDIAG_N5_D5_TOKEN == BK7258_CDIAG_N5_D5_TOKEN_VALUE)

static inline int bk7258_clockdiag_flash_swop_plain(uint32_t addr,
                                                    uint32_t op)
{
  uint32_t cmd;
  uint32_t ctrl;

  if (bk7258_clockdiag_flash_wait_ready() < 0)
    {
      return -1;
    }

  cmd = (addr & 0x00ffffffu) | (op << 24);
  putreg32(cmd, BK7258_CDIAG_FLASH_OP_CMD);

  ctrl = getreg32(BK7258_CDIAG_FLASH_OP_CTRL);
  putreg32(ctrl | BK7258_CDIAG_FLASH_OP_SW_BIT,
            BK7258_CDIAG_FLASH_OP_CTRL);

  return bk7258_clockdiag_flash_wait_ready();
}


static inline int bk7258_clockdiag_flash_swop_wp(uint32_t addr,
                                                  uint32_t op)
{
  uint32_t cmd;
  uint32_t ctrl;
  int ret;

  if (bk7258_clockdiag_flash_wait_ready() < 0)
    {
      return -1;
    }

  cmd = (addr & 0x00ffffffu) | (op << 24);
  putreg32(cmd, BK7258_CDIAG_FLASH_OP_CMD);

  ctrl = getreg32(BK7258_CDIAG_FLASH_OP_CTRL);
  putreg32(ctrl | BK7258_CDIAG_FLASH_OP_SW_BIT |
            BK7258_CDIAG_FLASH_WP_VALUE_BIT,
            BK7258_CDIAG_FLASH_OP_CTRL);

  ret = bk7258_clockdiag_flash_wait_ready();

  putreg32(getreg32(BK7258_CDIAG_FLASH_OP_CTRL) &
            ~BK7258_CDIAG_FLASH_WP_VALUE_BIT,
            BK7258_CDIAG_FLASH_OP_CTRL);

  return ret;
}
static inline int bk7258_clockdiag_flash_wren_plain(void)
{
  return bk7258_clockdiag_flash_swop_plain(0u,
                                                                           BK7258_CDIAG_FLASH_OP_CMD_WREN);
}
static inline int bk7258_clockdiag_flash_write_sr1(uint32_t sr1)
{
  uint32_t conf0;
  uint32_t conf;
  int ret;

  if (bk7258_clockdiag_flash_wren_plain() < 0)
    {
      return -1;
    }

  putreg32(0u, BK7258_CDIAG_FLASH_CMD_CFG);

  conf0 = getreg32(BK7258_CDIAG_FLASH_CONF);
  conf = conf0 & ~BK7258_CDIAG_FLASH_WRSR_DATA_MASK;
  conf |= (sr1 & 0xffu) << BK7258_CDIAG_FLASH_WRSR_DATA_SHIFT;
  putreg32(conf, BK7258_CDIAG_FLASH_CONF);

  ret = bk7258_clockdiag_flash_swop_wp(0u,
                                                                  BK7258_CDIAG_FLASH_OP_CMD_WRSR);

  putreg32(conf0, BK7258_CDIAG_FLASH_CONF);

  return ret;
}
static inline uint32_t bk7258_clockdiag_flash_read_sr(uint32_t op)
{
  if (bk7258_clockdiag_flash_swop_plain(0u, op) < 0)
    {
      return 0xffffffffu;
    }

  return getreg32(BK7258_CDIAG_FLASH_STATE) & 0xffu;
}
static inline int bk7258_clockdiag_flash_d5_unprotect(uint32_t *saved_sr0)
{
  uint32_t sr0;
  uint32_t unprotect_sr0;

  sr0 = bk7258_clockdiag_flash_read_sr(BK7258_CDIAG_FLASH_OP_CMD_RDSR);
  if (sr0 == 0xffffffffu)
    {
      return -1;
    }

  *saved_sr0 = sr0;

  if ((sr0 & BK7258_CDIAG_FLASH_SR1_PROTECT_MASK) == 0)
    {
      return 0;
    }

  /* Clear protection bits. Do not try to write WIP/WEL. */

  unprotect_sr0 = sr0 & ~BK7258_CDIAG_FLASH_SR1_PROTECT_MASK;
  unprotect_sr0 &= ~0x03u;

  if (bk7258_clockdiag_flash_write_sr1(unprotect_sr0) < 0)
    {
      return -1;
    }

  sr0 = bk7258_clockdiag_flash_read_sr(BK7258_CDIAG_FLASH_OP_CMD_RDSR);
  if ((sr0 & BK7258_CDIAG_FLASH_SR1_PROTECT_MASK) != 0)
    {
      return -1;
    }

  return 1;
}

static inline int bk7258_clockdiag_flash_d5_restore(uint32_t saved_sr0,
                                                    int changed)
{
  uint32_t restore_sr0;

  if (!changed)
    {
      return 0;
    }

  /* Restore writable protection bits; WIP/WEL are read-only/runtime bits. */

  restore_sr0 = saved_sr0 & 0xfcu;
  return bk7258_clockdiag_flash_write_sr1(restore_sr0);
}

static inline int bk7258_clockdiag_flash_erase_d5_sector(void)
{
  return bk7258_clockdiag_flash_swop_plain(BK7258_CDIAG_FS_D5_START,
                                                                          BK7258_CDIAG_FLASH_OP_CMD_SE);
}



static inline void bk7258_clockdiag_flash_d5_status(const char *tag)
{
  uint32_t sr0;
  uint32_t sr1;

  sr0 = bk7258_clockdiag_flash_read_sr(BK7258_CDIAG_FLASH_OP_CMD_RDSR);
  sr1 = bk7258_clockdiag_flash_read_sr(BK7258_CDIAG_FLASH_OP_CMD_RDSR2);

  bk7258_clockdiag_puts(tag);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putfield("SR0", sr0, 2);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putfield("SR1", sr1, 2);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("OC", getreg32(BK7258_CDIAG_FLASH_OP_CTRL));
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("CMD", getreg32(BK7258_CDIAG_FLASH_OP_CMD));
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("ST", getreg32(BK7258_CDIAG_FLASH_STATE));
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("CF", getreg32(BK7258_CDIAG_FLASH_CONF));
  bk7258_clockdiag_puts("\r\n");
}

static inline void bk7258_clockdiag_flash_d5_regs(const char *tag)
{
  bk7258_clockdiag_puts(tag);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("OC", getreg32(BK7258_CDIAG_FLASH_OP_CTRL));
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("CMD", getreg32(BK7258_CDIAG_FLASH_OP_CMD));
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("ST", getreg32(BK7258_CDIAG_FLASH_STATE));
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("CF", getreg32(BK7258_CDIAG_FLASH_CONF));
  bk7258_clockdiag_puts("\r\n");
}

static inline int bk7258_clockdiag_flash_write32_pattern(uint32_t addr)
{
  int diag;
  int ret;
  unsigned int i;

  if (addr < BK7258_CDIAG_FS_D5_START ||
      addr >= BK7258_CDIAG_FS_D5_START + BK7258_CDIAG_FS_D5_WRITE_LEN ||
      (addr & 0x1fu) != 0)
    {
      return -1;
    }

  diag = (addr == BK7258_CDIAG_FS_D5_START);

  if (bk7258_clockdiag_flash_wait_ready() < 0)
    {
      return -1;
    }

  if (diag)
    {
      bk7258_clockdiag_flash_d5_status("WB");
    }

  if (bk7258_clockdiag_flash_wren_plain() < 0)
    {
      return -1;
    }

  if (diag)
    {
      bk7258_clockdiag_flash_d5_status("WE");
    }

  for (i = 0; i < 8; i++)
    {
      putreg32(BK7258_CDIAG_FS_D5_PATTERN,
               BK7258_CDIAG_FLASH_DATA_SW_FLASH);
    }

  if (diag)
    {
      bk7258_clockdiag_flash_d5_regs("WS");
    }

  ret = bk7258_clockdiag_flash_swop_plain(addr,
                                                                        BK7258_CDIAG_FLASH_OP_CMD_PP);

  if (diag)
    {
      bk7258_clockdiag_flash_d5_status("WP");
    }

  return ret;
}

static inline int bk7258_clockdiag_flash_write_tail16_pattern(uint32_t addr)
{
  unsigned int i;

  if (addr != BK7258_CDIAG_FS_D5_START +
              BK7258_CDIAG_FS_D5_WRITE_LEN - 0x10u)
    {
      return -1;
    }

  if (bk7258_clockdiag_flash_wait_ready() < 0)
    {
      return -1;
    }

  if (bk7258_clockdiag_flash_wren_plain() < 0)
    {
      return -1;
    }

  /* D5-only tail coverage. Stage the same pattern in all slots; if the
    * controller writes more than 16B or wraps within the 256B page, it still
    * writes the same pattern and the sector is re-erased before exit.
    */

  for (i = 0; i < 8; i++)
    {
      putreg32(BK7258_CDIAG_FS_D5_PATTERN,
               BK7258_CDIAG_FLASH_DATA_SW_FLASH);
    }

  return bk7258_clockdiag_flash_swop_plain(addr,
                                                                          BK7258_CDIAG_FLASH_OP_CMD_PP);
}
static inline int bk7258_clockdiag_flash_verify_range(uint32_t start,
                                                      uint32_t end,
                                                      uint32_t expected)
{
  uint32_t addr;
  uint32_t buf[4];
  unsigned int i;

  for (addr = start; addr < end; addr += 0x10u)
    {
      if (bk7258_clockdiag_flash_read16(addr, buf) < 0)
        {
          bk7258_clockdiag_puts("VERIFY TIMEOUT ");
          bk7258_clockdiag_putreg("OFF", addr);
          bk7258_clockdiag_puts("\r\n");
          return -1;
        }

      for (i = 0; i < 4; i++)
        {
          if (buf[i] != expected)
            {
              bk7258_clockdiag_puts("VERIFY BAD ");
              bk7258_clockdiag_putreg("OFF", addr);
              bk7258_clockdiag_puts(" ");
              bk7258_clockdiag_putreg("EXP", expected);
              bk7258_clockdiag_puts(" ");
              bk7258_clockdiag_putreg("GOT", buf[i]);
              bk7258_clockdiag_puts("\r\n");
              return -1;
            }
        }
    }

  return 0;
}



static inline void bk7258_clockdiag_flash_d5_run(void)
{
  uint32_t addr;
  uint32_t saved_sr0 = 0;
  int sr_changed = 0;
  bk7258_clockdiag_puts("N5FS:D5\r\n");

  bk7258_clockdiag_puts("PRE ");
  bk7258_clockdiag_flash_dump16(BK7258_CDIAG_FS_D5_START);
  bk7258_clockdiag_flash_d5_status("PB");

  sr_changed = bk7258_clockdiag_flash_d5_unprotect(&saved_sr0);
  if (sr_changed < 0)
    {
      bk7258_clockdiag_puts("N5FS:D5 UNPROTFAIL\r\n");
      bk7258_clockdiag_flash_d5_restore(saved_sr0, sr_changed > 0);
      bk7258_clockdiag_flash_d5_status("PR");
      return;
    }

  bk7258_clockdiag_flash_d5_status("PU");
  if (bk7258_clockdiag_flash_verify_range(
        BK7258_CDIAG_FS_D5_START,
        BK7258_CDIAG_FS_D5_START + BK7258_CDIAG_FS_D5_WRITE_LEN,
        0xffffffffu) < 0)
    {
      bk7258_clockdiag_puts("N5FS:D5 PREFAIL\r\n");
      bk7258_clockdiag_flash_d5_restore(saved_sr0, sr_changed > 0);
      bk7258_clockdiag_flash_d5_status("PR");
      return;
    }

  if (bk7258_clockdiag_flash_erase_d5_sector() < 0)
    {
      bk7258_clockdiag_puts("N5FS:D5 ERASEFAIL\r\n");
      bk7258_clockdiag_flash_d5_restore(saved_sr0, sr_changed > 0);
      bk7258_clockdiag_flash_d5_status("PR");
      return;
    }

  bk7258_clockdiag_puts("ERA OK\r\n");
  bk7258_clockdiag_puts("ERD ");
  bk7258_clockdiag_flash_dump16(BK7258_CDIAG_FS_D5_START);

  if (bk7258_clockdiag_flash_verify_range(
        BK7258_CDIAG_FS_D5_START,
        BK7258_CDIAG_FS_D5_START + BK7258_CDIAG_FS_D5_WRITE_LEN,
        0xffffffffu) < 0)
    {
      bk7258_clockdiag_puts("N5FS:D5 ERDFAIL\r\n");
      bk7258_clockdiag_flash_d5_restore(saved_sr0, sr_changed > 0);
      bk7258_clockdiag_flash_d5_status("PR");
      return;
    }

  for (addr = BK7258_CDIAG_FS_D5_START;
       addr <= BK7258_CDIAG_FS_D5_START +
               BK7258_CDIAG_FS_D5_WRITE_LEN - 0x20u;
       addr += 0x20u)
    {
      bk7258_clockdiag_puts("WO ");
      bk7258_clockdiag_putreg("OFF", addr);
      bk7258_clockdiag_puts("\r\n");

      if (bk7258_clockdiag_flash_write32_pattern(addr) < 0)
        {
          bk7258_clockdiag_puts("N5FS:D5 WRITEFAIL ");
          bk7258_clockdiag_putreg("OFF", addr);
          bk7258_clockdiag_puts("\r\n");

          bk7258_clockdiag_flash_erase_d5_sector();
          bk7258_clockdiag_flash_d5_restore(saved_sr0, sr_changed > 0);
          bk7258_clockdiag_flash_d5_status("PR");
          return;
        }
    }
  addr = BK7258_CDIAG_FS_D5_START +
           BK7258_CDIAG_FS_D5_WRITE_LEN - 0x10u;

  bk7258_clockdiag_puts("WT ");
  bk7258_clockdiag_putreg("OFF", addr);
  bk7258_clockdiag_puts("\r\n");

  if (bk7258_clockdiag_flash_write_tail16_pattern(addr) < 0)
    {
      bk7258_clockdiag_puts("N5FS:D5 TAILFAIL ");
      bk7258_clockdiag_putreg("OFF", addr);
      bk7258_clockdiag_puts("\r\n");

      bk7258_clockdiag_flash_erase_d5_sector();
      bk7258_clockdiag_flash_d5_restore(saved_sr0, sr_changed > 0);
      bk7258_clockdiag_flash_d5_status("PR");
      return;
    }
  bk7258_clockdiag_puts("WR OK\r\n");
  bk7258_clockdiag_puts("WRD0 ");
  bk7258_clockdiag_flash_dump16(BK7258_CDIAG_FS_D5_START);

  bk7258_clockdiag_puts("WRDE ");
  bk7258_clockdiag_flash_dump16(BK7258_CDIAG_FS_D5_START +
                                BK7258_CDIAG_FS_D5_WRITE_LEN - 0x20u);

  bk7258_clockdiag_puts("WRDF ");
  bk7258_clockdiag_flash_dump16(BK7258_CDIAG_FS_D5_START +
                                BK7258_CDIAG_FS_D5_WRITE_LEN - 0x10u);

  if (bk7258_clockdiag_flash_verify_range(
          BK7258_CDIAG_FS_D5_START,
          BK7258_CDIAG_FS_D5_START + BK7258_CDIAG_FS_D5_WRITE_LEN,
          BK7258_CDIAG_FS_D5_PATTERN) < 0)
    {
      bk7258_clockdiag_puts("N5FS:D5 WRDFAIL\r\n");

      if (bk7258_clockdiag_flash_erase_d5_sector() == 0)
        {
          bk7258_clockdiag_puts("FAILCLR ");
          bk7258_clockdiag_flash_dump16(BK7258_CDIAG_FS_D5_START);
        }
      else
        {
          bk7258_clockdiag_puts("FAILCLR ERASEFAIL\r\n");
        }

      bk7258_clockdiag_flash_d5_restore(saved_sr0, sr_changed > 0);
      bk7258_clockdiag_flash_d5_status("PR");

      return;
    }

  if (bk7258_clockdiag_flash_erase_d5_sector() < 0)
    {
      bk7258_clockdiag_puts("N5FS:D5 RERASEFAIL\r\n");
      bk7258_clockdiag_flash_d5_restore(saved_sr0, sr_changed > 0);
      bk7258_clockdiag_flash_d5_status("PR");
      return;
    }

  bk7258_clockdiag_puts("RER OK\r\n");
  bk7258_clockdiag_puts("FIN ");
  bk7258_clockdiag_flash_dump16(BK7258_CDIAG_FS_D5_START);

  if (bk7258_clockdiag_flash_verify_range(
        BK7258_CDIAG_FS_D5_START,
        BK7258_CDIAG_FS_D5_START + BK7258_CDIAG_FS_D5_WRITE_LEN,
        0xffffffffu) < 0)
    {
      bk7258_clockdiag_puts("N5FS:D5 FINFAIL\r\n");
      bk7258_clockdiag_flash_d5_restore(saved_sr0, sr_changed > 0);
      bk7258_clockdiag_flash_d5_status("PR");
      return;
    }
  if (bk7258_clockdiag_flash_d5_restore(saved_sr0, sr_changed > 0) < 0)
    {
      bk7258_clockdiag_puts("N5FS:D5 RESTFAIL\r\n");
      return;
    }

  bk7258_clockdiag_flash_d5_status("PR");
  bk7258_clockdiag_puts("N5FS:D5 OK\r\n");
}

#endif
static inline int bk7258_clockdiag_has_bk_magic(uint32_t w0,
                                                uint32_t w1)
{
  return w0 == 0x32374b42u && (w1 & 0x0000ffffu) == 0x00003633u;
}
static inline void bk7258_clockdiag_flash_find_magic(void)
{
  uint32_t addr;
  uint32_t buf[4];

  bk7258_clockdiag_puts("N5FS:M\r\n");

  for (addr = 0; addr < 0x00040000u; addr += 0x20u)
    {
      if (bk7258_clockdiag_flash_read16(addr, buf) < 0)
        {
          bk7258_clockdiag_putreg("TOFF", addr);
          bk7258_clockdiag_puts("\r\n");
          return;
        }

      if (bk7258_clockdiag_has_bk_magic(buf[0], buf[1]))
        {
          bk7258_clockdiag_putreg("MAG", addr);
          bk7258_clockdiag_puts(" ");
          bk7258_clockdiag_putreg("W0", buf[0]);
          bk7258_clockdiag_puts(" ");
          bk7258_clockdiag_putreg("W1", buf[1]);
          bk7258_clockdiag_puts("\r\n");
          return;
        }
    }

  bk7258_clockdiag_puts("MAG=NONE\r\n");
}


static inline void bk7258_clockdiag_flash_dump(void)
{
  uint32_t id;
  uint32_t size;

  id = bk7258_clockdiag_flash_read_id();
  size = bk7258_clockdiag_flash_size_from_id(id);

  bk7258_clockdiag_puts("N5FS:F\r\n");

  bk7258_clockdiag_putreg("FID", id & 0x00ffffffu);
  bk7258_clockdiag_puts(" ");
  bk7258_clockdiag_putreg("FSIZ", size);
  bk7258_clockdiag_puts("\r\n");

  bk7258_clockdiag_putreg("ESZ", 0x00001000u);
  bk7258_clockdiag_puts(" ");
  bk7258_clockdiag_putreg("PSZ", 0x00000100u);
  bk7258_clockdiag_puts(" ");
  bk7258_clockdiag_putreg("B64", 0x00010000u);
  bk7258_clockdiag_puts("N5FS:R\r\n");
  bk7258_clockdiag_flash_dump16(0x00000000u);
  bk7258_clockdiag_flash_dump16(0x00100000u);
  bk7258_clockdiag_flash_dump16(0x00000100u);
  bk7258_clockdiag_flash_dump16(0x00011110u);
  bk7258_clockdiag_flash_dump16(0x00069888u);
  bk7258_clockdiag_flash_find_magic();
  bk7258_clockdiag_puts("N5FS:D\r\n");
  bk7258_clockdiag_flash_dump16(0x00100000u);
  bk7258_clockdiag_flash_dump16(0x00101000u);
  bk7258_clockdiag_flash_dump16(0x00102000u);
  bk7258_clockdiag_flash_dump16(0x00103000u);
  bk7258_clockdiag_puts("\r\n");

#if BK7258_CDIAG_ENABLE_N5_D5 && \
    (BK7258_CDIAG_N5_D5_TOKEN == BK7258_CDIAG_N5_D5_TOKEN_VALUE)
  if (id == 0 || size != BK7258_CDIAG_FLASH_SIZE)
    {
      bk7258_clockdiag_puts("N5FS:D5 SKIP BADFLASH\r\n");
    }
  else
    {
      bk7258_clockdiag_flash_d5_run();
    }
#endif

  if (id == 0 || size == 0)
    {
      bk7258_clockdiag_puts("N5FS:F UNKNOWN\r\n");
    }
}

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASHDIAG_H */
