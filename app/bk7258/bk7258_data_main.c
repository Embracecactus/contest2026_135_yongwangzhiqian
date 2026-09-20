/****************************************************************************
 * app/bk7258/bk7258_data_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP operator command for the on-chip persistent data filesystem.
 *
 * It never formats automatically: only an explicit
 * `init --confirm erase-non-littlefs` request may format, and the pinned
 * LittleFS VFS formats only when the block device does not already hold a
 * valid LittleFS (`-o autoformat`).  A valid filesystem of any origin is
 * mounted as-is and never rewritten, and only the on-chip FTL block device
 * is opened: the soldered SD NAND and the calibration/MAC/immutable tail
 * are never touched.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_APP_DATA

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/statfs.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKDATA_DEVICE  "/dev/mtdblock0"
#define BKDATA_TARGET  "/data"
#define BKDATA_CONFIRM "erase-non-littlefs"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bkdata_status(void)
{
  struct statfs info;
  int errcode;

  if (statfs(BKDATA_TARGET, &info) < 0)
    {
      errcode = errno;
      printf("BKDATA STATUS data=absent ret=%d\n", -errcode);
      return -errcode;
    }

  printf("BKDATA STATUS data=mounted type=%08lx littlefs=%s\n",
         (unsigned long)info.f_type,
         info.f_type == LITTLEFS_SUPER_MAGIC ? "yes" : "no");
  return 0;
}

static int bkdata_init(const char *confirm)
{
  struct statfs info;
  int errcode;

  if (confirm == NULL || strcmp(confirm, BKDATA_CONFIRM) != 0)
    {
      fprintf(stderr, "BKDATA INIT FAIL ret=%d\n", -EINVAL);
      return -EINVAL;
    }

  /* An existing LittleFS is never rewritten, whoever created it.  The
   * product creates its own private directories after the mount.
   */

  if (statfs(BKDATA_TARGET, &info) == 0 &&
      info.f_type == LITTLEFS_SUPER_MAGIC)
    {
      printf("BKDATA INIT SKIP reason=already-littlefs type=%08lx\n",
             (unsigned long)info.f_type);
      return 0;
    }

  /* `autoformat` formats only content that fails to mount as LittleFS; an
   * unmounted but valid filesystem is mounted unchanged.
   */

  if (mount(BKDATA_DEVICE, BKDATA_TARGET, "littlefs", 0, "autoformat") < 0)
    {
      errcode = errno;
      fprintf(stderr, "BKDATA INIT FAIL ret=%d\n", -errcode);
      return -errcode;
    }

  if (statfs(BKDATA_TARGET, &info) < 0)
    {
      errcode = errno;
      fprintf(stderr, "BKDATA INIT FAIL ret=%d\n", -errcode);
      return -errcode;
    }

  if (info.f_type != LITTLEFS_SUPER_MAGIC)
    {
      fprintf(stderr, "BKDATA INIT FAIL ret=%d\n", -EIO);
      return -EIO;
    }

  printf("BKDATA INIT PASS target=%s type=%08lx\n", BKDATA_TARGET,
         (unsigned long)info.f_type);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      return bkdata_status() < 0 ? 1 : 0;
    }

  if (argc == 4 && strcmp(argv[1], "init") == 0 &&
      strcmp(argv[2], "--confirm") == 0)
    {
      return bkdata_init(argv[3]) < 0 ? 1 : 0;
    }

  fprintf(stderr,
          "usage: %s status\n"
          "       %s init --confirm %s\n"
          "status is read-only.  init formats only when %s does\n"
          "not already hold a valid LittleFS; it never runs\n"
          "automatically and never touches the SD NAND or the\n"
          "protected calibration tail.\n",
          argv[0], argv[0], BKDATA_CONFIRM, BKDATA_DEVICE);
  return 1;
}

#endif /* CONFIG_BK7258_APP_DATA */
