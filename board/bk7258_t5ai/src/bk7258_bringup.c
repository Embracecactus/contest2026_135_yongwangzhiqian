/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/src/bk7258_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board bringup for the Beken BK7258 (T5-AI) NuttX port.
 *
 * board_app_initialize() is the NSH application-init hook: when
 * CONFIG_NSH_ARCHINIT=y, nsh_initialize() issues boardctl(BOARDIOC_INIT)
 * and the NSH init task reaches this function during nx_start().  Stage N3
 * mounts procfs at /proc here so ps, ls /proc, and cat of /proc entries
 * work; later stages (MTD, filesystems, SMP) extend this bring-up.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <nuttx/board.h>

#ifdef CONFIG_BK7258_FLASH_MTD
#include <nuttx/mtd/mtd.h>
#include "bk7258_flash_mtd.h"
#endif

#ifdef CONFIG_BK7258_FLASH_LITTLEFS
#include <nuttx/fs/fs.h>
#endif

/* UART1 MMIO for the boot-trace marker pushed at the top of
 * board_app_initialize().  Freestanding polled putc (polls fifo_status.bit20,
 * writes fifo_port); identical to start.c::bk7258_early_putc and
 * vectors.c::bk7258_fault_putc.  Local to this translation unit so it
 * introduces no new linkage dependency.
 */

#define BK7258_BRG_UART1_FSTAT   (*(volatile uint32_t *)0x45830018u)
#define BK7258_BRG_UART1_FPORT   (*(volatile uint32_t *)0x4583001Cu)
#define BK7258_BRG_UART1_READY   (1u << 20)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Bare MMIO single-byte marker.  Emits 'A' at function entry of
 * board_app_initialize() so board-side observation can confirm the NSH init
 * task reached the board-application bring-up hook during nx_start().
 */

static void bk7258_bringup_diag_putc(unsigned char c)
{
  while ((BK7258_BRG_UART1_FSTAT & BK7258_BRG_UART1_READY) == 0)
    {
    }

  BK7258_BRG_UART1_FPORT = (uint32_t)(c & 0xffu);
}

#ifdef CONFIG_BK7258_FLASH_LITTLEFS
/* LittleFS bring-up: register /dev/mtdblock0 (ftl), mount at /data with the
 * "autoformat" option (formats only on first boot), then run a probe-file
 * persistence check.  Markers: L=mounted l=mount-fail C=created(first boot)
 * R=read-back-ok r=read-back-mismatch.
 *
 * The probe file is created on the first boot after format and read back on
 * every later boot, so a reboot observing R proves write persistence.
 */

#define BK7258_FS_MOUNTPOINT  "/data"
#define BK7258_FS_BLOCKDEV    "/dev/mtdblock0"
#define BK7258_FS_PROBE       "/data/probe.txt"
#define BK7258_FS_PROBE_LEN   12
static const char g_fs_probe[BK7258_FS_PROBE_LEN] = "BK7258LFS-OK";

static void bk7258_fs_probe(struct mtd_dev_s *mtd)
{
  char buf[BK7258_FS_PROBE_LEN];
  int fd;
  ssize_t n;

  if (ftl_initialize(0, mtd) < 0)
    {
      bk7258_bringup_diag_putc('f');   /* ftl registration failed */
      return;
    }

  mkdir(BK7258_FS_MOUNTPOINT, 0777);

  if (mount(BK7258_FS_BLOCKDEV, BK7258_FS_MOUNTPOINT, "littlefs", 0,
            "autoformat") < 0)
    {
      bk7258_bringup_diag_putc('l');   /* littlefs mount failed */
      return;
    }

  bk7258_bringup_diag_putc('L');       /* littlefs mounted at /data */

  /* If the probe file exists, read it back and compare (persistence). */

  fd = open(BK7258_FS_PROBE, O_RDONLY);
  if (fd < 0)
    {
      /* First boot: create the probe file. */

      fd = open(BK7258_FS_PROBE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (fd < 0)
        {
          bk7258_bringup_diag_putc('o');   /* creat failed */
          return;
        }

      if (write(fd, g_fs_probe, BK7258_FS_PROBE_LEN) != BK7258_FS_PROBE_LEN)
        {
          bk7258_bringup_diag_putc('e');   /* write failed */
          close(fd);
          return;
        }

      close(fd);
      sync();
      bk7258_bringup_diag_putc('C');   /* probe created */
      return;
    }

  n = read(fd, buf, BK7258_FS_PROBE_LEN);
  close(fd);

  if (n == BK7258_FS_PROBE_LEN &&
      memcmp(buf, g_fs_probe, BK7258_FS_PROBE_LEN) == 0)
    {
      bk7258_bringup_diag_putc('R');   /* persistence verified */
    }
  else
    {
      bk7258_bringup_diag_putc('r');   /* readback mismatch */
    }
}
#endif /* CONFIG_BK7258_FLASH_LITTLEFS */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   Standard NuttX board Application-level initialization hook, reached via
 *   CONFIG_NSH_ARCHINIT from the NSH init task.  Mounts procfs at
 *   CONFIG_NSH_PROC_MOUNTPOINT (default /proc) and returns OK.
 *
 * Input Parameters:
 *   arg - Board-specific argument (unused).
 *
 * Returned Value:
 *   Zero (OK) on success.
 *
 ****************************************************************************/

int board_app_initialize(uintptr_t arg)
{
  /* Boot-trace marker: reached board_app_initialize() from the NSH init
   * task spawned by nx_start().
   */

  bk7258_bringup_diag_putc('A');

  /* Mount procfs at the NSH proc mountpoint so ps, ls /proc, and cat of
   * /proc entries work.  CONFIG_NSH_ARCHINIT activates this hook;
   * CONFIG_FS_PROCFS provides the filesystem.
   */

  if (mount(NULL, CONFIG_NSH_PROC_MOUNTPOINT, "procfs", 0, NULL) < 0)
    {
      bk7258_bringup_diag_putc('p');   /* procfs mount failed */
    }
  else
    {
      bk7258_bringup_diag_putc('P');   /* procfs mounted at /proc */
    }

#ifdef CONFIG_BK7258_FLASH_MTD
  /* Stage N5-D6: create the MTD instance for the verified 1 MiB data
   * partition.  The instance is held but not mounted: no /dev node, no
   * format, no filesystem.  The driver prints one evidence line (N5FS:MTD)
   * with geometry and the first data words, and performs no writes at init.
   */

  if (bk7258_flash_mtd_initialize() == NULL)
    {
      bk7258_bringup_diag_putc('m');   /* MTD init failed (bad flash id) */
    }
  else
    {
      bk7258_bringup_diag_putc('M');   /* data-partition MTD ready */

#ifdef CONFIG_BK7258_FLASH_MTD_SELFTEST
      /* Authorised one-shot destructive write-path self-test on block 0.
       * Prints N5FS:MTDW ... and restores the block to 0xff on success.
       */

      if (bk7258_flash_mtd_selftest(bk7258_flash_mtd_initialize()) < 0)
        {
          bk7258_bringup_diag_putc('w');   /* self-test failed */
        }
      else
        {
          bk7258_bringup_diag_putc('W');   /* self-test passed */
        }
#endif

#ifdef CONFIG_BK7258_FLASH_LITTLEFS
      /* Authorised LittleFS bring-up on the data partition: register
       * /dev/mtdblock0, auto-format + mount at /data, probe-file persistence
       * check.  Destructive on the data partition on first boot.
       */

      bk7258_fs_probe(bk7258_flash_mtd_initialize());
#endif
    }
#endif

  return 0;
}
