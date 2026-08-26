/****************************************************************************
 * boards/bk7258/common/src/bk7258_finalinit.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Post-rc.sysinit validation for the Beken BK7258 CP system services.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <syslog.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_ETC_ROMFS
#  define BK7258_SYSINIT_PATH \
     CONFIG_ETC_ROMFSMOUNTPT "/" CONFIG_NSH_SYSINITSCRIPT
#  define BK7258_RCS_PATH \
     CONFIG_ETC_ROMFSMOUNTPT "/" CONFIG_NSH_INITSCRIPT
#  ifdef CONFIG_ETC_CROMFS
#    define BK7258_ETC_FS_MAGIC CROMFS_MAGIC
#  else
#    define BK7258_ETC_FS_MAGIC ROMFS_MAGIC
#  endif
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if defined(CONFIG_ETC_ROMFS) || \
    (defined(CONFIG_FS_PROCFS) && defined(CONFIG_NSH_PROC_MOUNTPOINT)) || \
    defined(CONFIG_BK7258_STORAGE_ONCHIP_PERSISTENT)
static int bk7258_final_check_fs(FAR const char *name,
                                 FAR const char *path,
                                 uint32_t expected)
{
  struct statfs fsinfo;
  int errcode;

  if (statfs(path, &fsinfo) < 0)
    {
      errcode = errno;
      syslog(LOG_ERR, "BK7258 FINALINIT FAIL: statfs(%s) for %s: %d\n",
             path, name, errcode);
      return -errcode;
    }

  if (fsinfo.f_type != expected)
    {
      syslog(LOG_ERR,
             "BK7258 FINALINIT FAIL: %s at %s has type %08lx, "
             "expected %08lx\n",
             name, path, (unsigned long)fsinfo.f_type,
             (unsigned long)expected);
      return -ENODEV;
    }

  return 0;
}
#endif

#ifdef CONFIG_ETC_ROMFS
static int bk7258_final_check_path(FAR const char *path)
{
  struct stat fileinfo;
  int errcode;

  if (stat(path, &fileinfo) < 0)
    {
      errcode = errno;
      syslog(LOG_ERR, "BK7258 FINALINIT FAIL: stat(%s): %d\n",
             path, errcode);
      return -errcode;
    }

  return 0;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_app_finalinitialize
 *
 * Description:
 *   Validate the filesystem services established by rc.sysinit and report
 *   their status before NSH invokes rcS.
 ****************************************************************************/

int board_app_finalinitialize(uintptr_t arg)
{
#if defined(CONFIG_ETC_ROMFS) || \
    (defined(CONFIG_FS_PROCFS) && defined(CONFIG_NSH_PROC_MOUNTPOINT)) || \
    defined(CONFIG_BK7258_STORAGE_ONCHIP_PERSISTENT)
  int ret;
#endif

  (void)arg;

#ifdef CONFIG_ETC_ROMFS
  ret = bk7258_final_check_fs("etc ROMFS", CONFIG_ETC_ROMFSMOUNTPT,
                             BK7258_ETC_FS_MAGIC);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_final_check_path(BK7258_SYSINIT_PATH);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_final_check_path(BK7258_RCS_PATH);
  if (ret < 0)
    {
      return ret;
    }
#endif

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_NSH_PROC_MOUNTPOINT)
  ret = bk7258_final_check_fs("procfs", CONFIG_NSH_PROC_MOUNTPOINT,
                             PROCFS_MAGIC);
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_STORAGE_ONCHIP_PERSISTENT
  ret = bk7258_final_check_fs("persistent data", "/data",
                             LITTLEFS_SUPER_MAGIC);
  if (ret < 0)
    {
      return ret;
    }
#endif

  syslog(LOG_INFO, "BK7258 FINALINIT PASS\n");
  return 0;
}
