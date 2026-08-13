/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/bk7258_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Application-facing device and filesystem bringup for the Beken BK7258.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <fcntl.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <debug.h>

#ifdef CONFIG_BK7258_FLASH_MTD
#  include <nuttx/fs/fs.h>
#  include <nuttx/mtd/mtd.h>
#  include "bk7258_flash_mtd.h"
#endif

#ifdef CONFIG_BK7258_FLASH_LITTLEFS
#  include <nuttx/fs/fs.h>
#endif

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_BK7258_DVFS_PROCFS)
#  include "bk7258_dvfs.h"
#endif

#include "bk7258_internal.h"

/****************************************************************************
 * Private Data and Functions
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

  /* Persistence verification: the file was created on a previous boot, so
   * a successful read-back of the expected marker proves writes survive
   * reset.  Stay silent when persistence is confirmed; log a runtime error
   * otherwise.
   */

  if (n != (ssize_t)BK7258_FS_PROBE_LEN ||
      memcmp(buf, g_fs_probe, BK7258_FS_PROBE_LEN) != 0)
    {
      _err("bk7258: LittleFS probe persistence check failed (n=%d)\n",
           (int)n);
    }
}
#endif /* CONFIG_BK7258_FLASH_LITTLEFS */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_bringup
 *
 * Description:
 *   Ensure mandatory platform initialization has completed, then register
 *   application-facing procfs, MTD and filesystem services.
 ****************************************************************************/

int bk7258_bringup(void)
{
  int ret;

  ret = bk7258_platform_initialize();
  if (ret < 0)
    {
      return ret;
    }

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
#ifdef CONFIG_MCUBOOT_BOOTLOADER
      /* Publish read-only, bounds-checked image-pair partitions only to the
       * NuttX MCUboot BL2 profile.
       */

      if (register_mtddriver(
            CONFIG_MCUBOOT_PRIMARY_SLOT_PATH,
            bk7258_mcuboot_mtd_get(BK7258_MCUBOOT_MTD_SLOT_PRIMARY),
            0600, NULL) < 0 ||
          register_mtddriver(
            CONFIG_MCUBOOT_SECONDARY_SLOT_PATH,
            bk7258_mcuboot_mtd_get(BK7258_MCUBOOT_MTD_SLOT_SECONDARY),
            0600, NULL) < 0)
        {
          _err("bk7258: MCUboot MTD node registration failed\n");
        }
#endif
    }
#endif

  return 0;
}
