/****************************************************************************
 * boards/bk7258/common/src/bk7258_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Application-facing device and filesystem bringup for the Beken BK7258.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>

#ifdef CONFIG_BK7258_FLASH_MTD
#  include <nuttx/fs/fs.h>
#  include <nuttx/mtd/mtd.h>
#  include <arch/chip/bk7258_flash.h>
#  include <arch/chip/bk7258_flash_mtd.h>
#  include <arch/chip/bk7258_image_layout.h>
#endif

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_BK7258_DVFS_PROCFS)
#  include <arch/chip/bk7258_dvfs_procfs.h>
#endif

#include "bk7258_internal.h"

/****************************************************************************
 * Private Data and Functions
 ****************************************************************************/

#ifdef CONFIG_BK7258_STORAGE_ONCHIP_PERSISTENT
_Static_assert(BK7258_FLASH_ERASE_SIZE == BK7258_FLASH_SECTOR_SIZE,
               "partition and Flash erase sizes differ");

static const struct bk7258_flash_mtd_config_s g_bk7258_data_mtd_config =
{
  .base = BK7258_DATA_RAW_PHYSICAL_OFFSET,
  .size = BK7258_DATA_RAW_PHYSICAL_SIZE,
  .owner = BK7258_STORAGE_GUARD_DATA,
  .name = "bk7258-persistent-data"
};

/* Register /dev/mtdblock0 before rc.sysinit runs.  The script owns the
 * filesystem mount so the system-startup ordering remains visible and
 * configurable; this hook only makes the block device available to it.
 */

static void bk7258_fs_register(FAR struct mtd_dev_s *mtd)
{
  if (ftl_initialize(0, mtd) < 0)
    {
      _err("bk7258: failed to register LittleFS FTL block device\n");
    }
}
#endif /* CONFIG_BK7258_STORAGE_ONCHIP_PERSISTENT */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_bringup
 *
 * Description:
 *   Ensure mandatory platform initialization has completed, then register
 *   application-facing procfs and storage devices needed by rc.sysinit.
 ****************************************************************************/

int bk7258_bringup(void)
{
#ifndef CONFIG_BK7258_AP_CORE
  int ret;
#endif

#ifndef CONFIG_BK7258_AP_CORE
  ret = bk7258_cp_bringup_result();
  if (ret < 0)
    {
      return ret;
    }
#endif

  /* Register the BK7258 DVFS /proc/dvfs entry *before* mounting procfs: the
   * fs_procfs NOTE requires the procfs entry table to be stable at mount
   * time (procfs_register reallocs the table; doing it after the mount would
   * race with concurrent procfs access).
   */

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_BK7258_DVFS_PROCFS)
  (void)bk7258_dvfs_procfs_register();
#endif

#ifdef CONFIG_BK7258_FLASH_MTD
  /* Create the selected on-chip persistent MTD and expose its FTL block
   * device.  rc.sysinit mounts the selected filesystem and never formats it.
   */

  FAR struct mtd_dev_s *mtd =
    bk7258_flash_mtd_initialize(&g_bk7258_data_mtd_config);
  if (mtd != NULL)
    {
#ifdef CONFIG_BK7258_STORAGE_ONCHIP_PERSISTENT
      bk7258_fs_register(mtd);
#endif
    }
#endif

  return 0;
}
