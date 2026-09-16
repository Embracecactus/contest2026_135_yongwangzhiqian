/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include "bk7258_preferences_storage.h"
#include "bk7258_media_volume.h"

#define PREFERENCES_MOUNT "/mnt/sdnand"

static bool g_preferences_mounted;
static bool g_preferences_leased;

static int preferences_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static int preferences_close(void)
{
  int ret;

  if (g_preferences_mounted)
    {
      if (umount(PREFERENCES_MOUNT) < 0)
        {
          return preferences_errno();
        }

      g_preferences_mounted = false;
    }

  if (g_preferences_leased)
    {
      ret = bk7258_media_volume_release(BK7258_MEDIA_VOLUME_PREFERENCES);
      if (ret < 0)
        {
          return ret;
        }

      g_preferences_leased = false;
    }

  return 0;
}

int bk7258_preferences_storage_end(int status)
{
  int ret = preferences_close();
  return status < 0 ? status : ret;
}

int bk7258_preferences_storage_begin(void)
{
  int ret;

  /* Retry retained cleanup before acquiring anything new. */

  ret = preferences_close();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_PREFERENCES);
  if (ret < 0)
    {
      return ret;
    }

  g_preferences_leased = true;
  if ((mkdir("/mnt", 0777) < 0 && errno != EEXIST) ||
      (mkdir(PREFERENCES_MOUNT, 0777) < 0 && errno != EEXIST))
    {
      return bk7258_preferences_storage_end(preferences_errno());
    }

  /* Never adopt or unmount an existing foreign mount. */

  if (mount(CONFIG_BK7258_PREFERENCES_BLOCKDEV, PREFERENCES_MOUNT,
            "vfat", 0, NULL) < 0)
    {
      return bk7258_preferences_storage_end(preferences_errno());
    }

  g_preferences_mounted = true;
  return 0;
}
