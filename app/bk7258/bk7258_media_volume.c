/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "bk7258_media_volume.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <syslog.h>
#ifdef CONFIG_BK7258_USBMODE
#  include <arch/chip/bk7258_usbmode.h>
#endif

static int g_volume_owner;

int bk7258_media_volume_acquire(enum bk7258_media_volume_owner_e owner)
{
  int expected = 0;
  if (owner != BK7258_MEDIA_VOLUME_DISPLAY &&
      owner != BK7258_MEDIA_VOLUME_VISION &&
      owner != BK7258_MEDIA_VOLUME_PREFERENCES &&
      owner != BK7258_MEDIA_VOLUME_OTA)
    {
      return -EINVAL;
    }
  if (!__atomic_compare_exchange_n(&g_volume_owner, &expected, owner, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }
#ifdef CONFIG_BK7258_USBMODE
  {
    int ret = bk7258_usbmode_blockdev_acquire();
    if (ret < 0)
      {
        __atomic_store_n(&g_volume_owner, 0, __ATOMIC_RELEASE);
        return ret;
      }
  }
#endif
  return 0;
}

int bk7258_media_volume_release(enum bk7258_media_volume_owner_e owner)
{
  if (owner == 0 ||
      __atomic_load_n(&g_volume_owner, __ATOMIC_ACQUIRE) != (int)owner)
    {
      return -EPERM;
    }
#ifdef CONFIG_BK7258_USBMODE
  {
    int ret = bk7258_usbmode_blockdev_release();
    if (ret < 0)
      {
        return ret;
      }
  }
#endif
  __atomic_store_n(&g_volume_owner, 0, __ATOMIC_RELEASE);
  return 0;
}

#if defined(CONFIG_BK7258_OTA_SOURCE_FILE) && defined(CONFIG_BK7258_USBMODE)
#define BK7258_MEDIA_VOLUME_MOUNTROOT "/mnt"
#define BK7258_MEDIA_VOLUME_MOUNTPOINT "/mnt/sdnand"
#define BK7258_MEDIA_VOLUME_OTA_PREFIX "/mnt/sdnand/"

static bool g_ota_mounted;
static bool g_ota_leased;

static int bk7258_media_volume_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static bool bk7258_media_volume_ota_root(const char *root)
{
  const char *component;

  if (root == NULL)
    {
      return false;
    }
  if (strncmp(root, BK7258_MEDIA_VOLUME_OTA_PREFIX,
              sizeof(BK7258_MEDIA_VOLUME_OTA_PREFIX) - 1u))
    {
      return false;
    }

  component = root + sizeof(BK7258_MEDIA_VOLUME_OTA_PREFIX) - 1u;
  while (*component != '\0')
    {
      const char *end = component;

      while (*end != '\0' && *end != '/')
        {
          end++;
        }

      if ((size_t)(end - component) == 2u && component[0] == '.' &&
          component[1] == '.')
        {
          return false;
        }

      component = *end == '\0' ? end : end + 1;
    }

  return true;
}

int bk7258_media_volume_ota_release(void)
{
  int ret;

  if (g_ota_mounted)
    {
      if (umount(BK7258_MEDIA_VOLUME_MOUNTPOINT) < 0)
        {
          ret = bk7258_media_volume_errno();
          syslog(LOG_WARNING, "BKOTA mount cleanup retained ret=%d\n", ret);
          return ret;
        }

      g_ota_mounted = false;
    }

  if (!g_ota_leased)
    {
      return 0;
    }

  ret = bk7258_media_volume_release(BK7258_MEDIA_VOLUME_OTA);
  if (ret == 0)
    {
      g_ota_leased = false;
    }
  else
    {
      syslog(LOG_WARNING, "BKOTA lease cleanup retained ret=%d\n", ret);
    }

  return ret;
}

int bk7258_media_volume_ota_prepare(const char *root)
{
  int ret;

  /* Retry retained cleanup before accepting another package path. */
  ret = bk7258_media_volume_ota_release();
  if (ret < 0)
    {
      return ret;
    }
  if (root == NULL)
    {
      return -EINVAL;
    }
  if (!bk7258_media_volume_ota_root(root))
    {
      return strncmp(root, BK7258_MEDIA_VOLUME_OTA_PREFIX,
                     sizeof(BK7258_MEDIA_VOLUME_OTA_PREFIX) - 1u) == 0 ?
             -EINVAL : 0;
    }

  ret = bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_OTA);
  if (ret < 0)
    {
      return ret;
    }

  g_ota_leased = true;
  if ((mkdir(BK7258_MEDIA_VOLUME_MOUNTROOT, 0777) < 0 && errno != EEXIST) ||
      (mkdir(BK7258_MEDIA_VOLUME_MOUNTPOINT, 0777) < 0 && errno != EEXIST))
    {
      ret = bk7258_media_volume_errno();
      goto fail;
    }
  if (mount(CONFIG_BK7258_USBMODE_BLOCKDEV, BK7258_MEDIA_VOLUME_MOUNTPOINT,
            "vfat", MS_RDONLY, NULL) < 0)
    {
      ret = bk7258_media_volume_errno();
      goto fail;
    }

  g_ota_mounted = true;
  return 0;

fail:
  (void)bk7258_media_volume_ota_release();
  return ret;
}
#endif
