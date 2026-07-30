/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/src/bk7258_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board bringup for the Beken BK7258 (T5-AI) NuttX port.
 *
 * board_app_initialize() is the NSH application-init hook: when
 * CONFIG_NSH_ARCHINIT=y, nsh_initialize() issues boardctl(BOARDIOC_INIT)
 * and the NSH init task reaches this function during nx_start().  procfs is
 * mounted at /proc here so ps, ls /proc, and cat of /proc entries work;
 * when CONFIG_BK7258_FLASH_MTD + CONFIG_BK7258_FLASH_LITTLEFS are enabled,
 * /dev/mtdblock0 + a LittleFS /data mount are added on the data partition.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>

#include <arch/board/board.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <debug.h>
#include <nuttx/arch.h>
#include <nuttx/board.h>

#ifdef CONFIG_BK7258_AP_CONTROL
#include <arch/chip/bk7258_amp.h>
#endif

#ifdef CONFIG_BK7258_FLASH_MTD
#include <nuttx/mtd/mtd.h>
#include "bk7258_flash_mtd.h"
#endif

#ifdef CONFIG_BK7258_FLASH_LITTLEFS
#include <nuttx/fs/fs.h>
#endif

#ifdef CONFIG_BK7258_WDT
#include "bk7258_wdt.h"
#endif

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_BK7258_DVFS_PROCFS)
#include "bk7258_dvfs.h"
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_BK7258_FLASH_LITTLEFS
/* LittleFS bring-up: register /dev/mtdblock0 (ftl), mount at /data with the
 * "autoformat" option (formats only on first boot), then run a probe-file
 * persistence check.
 *
 * The probe file is created on the first boot after format and read back on
 * every later boot, so a reboot observing the expected bytes proves write
 * persistence.
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
      return;
    }

  mkdir(BK7258_FS_MOUNTPOINT, 0777);

  if (mount(BK7258_FS_BLOCKDEV, BK7258_FS_MOUNTPOINT, "littlefs", 0,
            "autoformat") < 0)
    {
      return;
    }

  /* If the probe file exists, read it back and compare (persistence). */

  fd = open(BK7258_FS_PROBE, O_RDONLY);
  if (fd < 0)
    {
      /* First boot: create the probe file. */

      fd = open(BK7258_FS_PROBE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (fd < 0)
        {
          return;
        }

      if (write(fd, g_fs_probe, BK7258_FS_PROBE_LEN) != BK7258_FS_PROBE_LEN)
        {
          close(fd);
          return;
        }

      close(fd);
      sync();
      return;
    }

  n = read(fd, buf, BK7258_FS_PROBE_LEN);
  close(fd);

  /* Persistence verification: the file was created on a previous boot, so a
   * successful read-back of the expected marker proves writes survive reset.
   * Stay silent when persistence is confirmed; log a runtime error otherwise.
   */

  if (n != (ssize_t)BK7258_FS_PROBE_LEN ||
      memcmp(buf, g_fs_probe, BK7258_FS_PROBE_LEN) != 0)
    {
      _err("bk7258: LittleFS probe persistence check failed (n=%d)\n",
           (int)n);
    }
}
#endif /* CONFIG_BK7258_FLASH_LITTLEFS */

/* TEMP cold-reset diagnostic: emit a short raw-UART tag so the last
 * completed startup checkpoint remains visible without /dev/console,
 * printf or syslog.  Delete after diagnosis.
 */

static void cold_ckpt(const char *tag)
{
  up_putc('\r');
  up_putc('\n');
  while (*tag)
    {
      up_putc(*tag++);
    }

  up_putc('\r');
  up_putc('\n');
}

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
  cold_ckpt("C0"); /* board_app_initialize entry */

#ifdef CONFIG_BK7258_WDT
  /* AP autostart may wait up to three seconds.  Stop the bootloader AON WDT
   * before entering that wait or it resets the entire SoC before AP failure
   * diagnostics can be consumed.
   */

  (void)bk7258_wdt_initialize();
#endif
  cold_ckpt("C1"); /* WDT init done */

#ifdef CONFIG_BK7258_AP_CONTROL
  int apret;

  apret = bk7258_ap_control_initialize();
  cold_ckpt("C2"); /* AP control init returned */
  if (apret < 0)
    {
      _err("bk7258: AP control init failed: %d\n", apret);
    }
#ifdef CONFIG_BK7258_AP_AUTOSTART
  else
    {
      cold_ckpt("C3"); /* AP autostart begins */
      apret = bk7258_ap_start(BK7258_AP_DEFAULT_TIMEOUT_MS);
      if (apret < 0)
        {
          _err("bk7258: AP autostart failed: %d\n", apret);
        }

      cold_ckpt("C4"); /* AP autostart returned */
    }
#endif
#endif

#ifdef CONFIG_BK7258_GPIO_LOWERHALF
  (void)bk7258_gpio_lowerhalf_initialize();
#endif
  cold_ckpt("C5"); /* GPIO lower-half done */

  /* Register the BK7258 DVFS /proc/dvfs entry *before* mounting procfs: the
   * fs_procfs NOTE requires the procfs entry table to be stable at mount
   * time (procfs_register reallocs the table; doing it after the mount would
   * race with concurrent procfs access).
   */

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_BK7258_DVFS_PROCFS)
  (void)bk7258_dvfs_procfs_register();
#endif

  /* Mount procfs at the NSH proc mountpoint so ps, ls /proc, and cat of
   * /proc entries work.  CONFIG_NSH_ARCHINIT activates this hook;
   * CONFIG_FS_PROCFS provides the filesystem.
   */

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_NSH_PROC_MOUNTPOINT)
  (void)mount(NULL, CONFIG_NSH_PROC_MOUNTPOINT, "procfs", 0, NULL);
#endif
  cold_ckpt("C6"); /* procfs registration/mount done */

#ifdef CONFIG_BK7258_FLASH_MTD
  /* Create the MTD instance for the 1 MiB data partition.  When LittleFS is
   * also enabled, register /dev/mtdblock0 + mount /data and run the probe
   * persistence check on the same instance.
   */

  FAR struct mtd_dev_s *mtd = bk7258_flash_mtd_initialize();
  if (mtd != NULL)
    {
#ifdef CONFIG_BK7258_FLASH_LITTLEFS
      bk7258_fs_probe(mtd);
#endif
    }
#endif
  cold_ckpt("C7"); /* flash MTD / LittleFS done */
  cold_ckpt("C8"); /* board_app_initialize return */

  return 0;
}
