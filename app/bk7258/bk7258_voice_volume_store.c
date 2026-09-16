/****************************************************************************
 * app/bk7258/bk7258_voice_volume_store.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Durable device-local BKVoice volume.  AP reaches the CP-owned LittleFS
 * through RPMsgFS; the provisioning store supplies the common hash,
 * write/fsync/rename and revision-guard contract in an independent directory.
 ****************************************************************************/

#define _POSIX_C_SOURCE 200809L

#include "bk7258_voice_volume_store.h"
#include "bk7258_provision_store.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include <nuttx/mutex.h>

#define BKVOICE_VOLUME_RECORD_SIZE 8u

static mutex_t g_volume_store_lock = NXMUTEX_INITIALIZER;
static struct bkprov_store_s g_volume_store;
static char g_volume_root[160];
static uint64_t g_volume_revision;
static unsigned int g_volume_percent;
static bool g_volume_started;
static bool g_volume_loaded;
static bool g_volume_present;

static void bkvoice_volume_put64(uint8_t *output, uint64_t value)
{
  int index;

  for (index = 7; index >= 0; index--)
    {
      output[index] = (uint8_t)value;
      value >>= 8;
    }
}

static int bkvoice_volume_prepare_directory(const char *root)
{
  struct stat info;
  char parent[160];
  char *separator;
  size_t length;
  int ret;

  if (lstat(root, &info) == 0)
    {
      return S_ISDIR(info.st_mode) ?
             bkprov_store_check_filesystem(root) : -ENOTDIR;
    }

  if (errno != ENOENT)
    {
      return -errno;
    }

  length = strlen(root);
  if (length < 2 || length >= sizeof(parent))
    {
      return -EINVAL;
    }

  memcpy(parent, root, length + 1);
  separator = strrchr(parent, '/');
  if (separator == NULL || separator == parent)
    {
      return -EINVAL;
    }

  *separator = '\0';
  if (lstat(parent, &info) < 0)
    {
      return errno == ENOENT ? -EAGAIN : -errno;
    }

  if (!S_ISDIR(info.st_mode))
    {
      return -ENOTDIR;
    }

  ret = bkprov_store_check_filesystem(parent);
  if (ret < 0)
    {
      return ret;
    }

  if (mkdir(root, 0700) < 0 && errno != EEXIST)
    {
      return -errno;
    }

  if (lstat(root, &info) < 0)
    {
      return -errno;
    }

  if (!S_ISDIR(info.st_mode))
    {
      return -ENOTDIR;
    }

  return bkprov_store_check_filesystem(root);
}

static int bkvoice_volume_load_locked(void)
{
  uint8_t record[BKVOICE_VOLUME_RECORD_SIZE];
  size_t size;
  uint64_t revision;
  int ret;

  if (g_volume_loaded)
    {
      return 0;
    }

  ret = bkvoice_volume_prepare_directory(g_volume_root);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkprov_store_open(&g_volume_store, g_volume_root);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkprov_store_load(&g_volume_store, record, sizeof(record), &size,
                          &revision, NULL);
  if (ret == -ENOENT)
    {
      g_volume_revision = 0;
      g_volume_present = false;
      g_volume_loaded = true;
      return 0;
    }

  if (ret < 0)
    {
      return ret;
    }

  if (size != sizeof(record) || memcmp(record, "BVV1", 4) != 0 ||
      record[4] > 100u || record[5] != 0 || record[6] != 0 || record[7] != 0)
    {
      return -EBADMSG;
    }

  g_volume_revision = revision;
  g_volume_percent = record[4];
  g_volume_present = true;
  g_volume_loaded = true;
  return 0;
}

int bkvoice_volume_store_start(const char *root)
{
  size_t length;
  int ret;

  if (root == NULL)
    {
      return -EINVAL;
    }

  length = strlen(root);
  if (length < 2 || length >= sizeof(g_volume_root) || root[0] != '/' ||
      root[length - 1] == '/')
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_volume_store_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_volume_started)
    {
      ret = strcmp(g_volume_root, root) == 0 ? -EALREADY : -EBUSY;
    }
  else
    {
      memcpy(g_volume_root, root, length + 1);
      g_volume_started = true;
      ret = 0;
    }

  nxmutex_unlock(&g_volume_store_lock);
  return ret;
}

int bkvoice_volume_store_get(unsigned int *volume_percent)
{
  int ret;

  if (volume_percent == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_volume_store_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!g_volume_started)
    {
      ret = -ENODEV;
    }
  else
    {
      ret = bkvoice_volume_load_locked();
      if (ret == 0 && !g_volume_present)
        {
          ret = -ENOENT;
        }
      else if (ret == 0)
        {
          *volume_percent = g_volume_percent;
        }
    }

  nxmutex_unlock(&g_volume_store_lock);
  return ret;
}

int bkvoice_volume_store_set(unsigned int volume_percent)
{
  uint8_t record[BKVOICE_VOLUME_RECORD_SIZE] =
    {'B', 'V', 'V', '1', 0, 0, 0, 0};
  uint8_t transaction[16] = {'B', 'V', 'V', '1'};
  int ret;

  if (volume_percent > 100u)
    {
      return -ERANGE;
    }

  ret = nxmutex_lock(&g_volume_store_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!g_volume_started)
    {
      ret = -ENODEV;
      goto done;
    }

  ret = bkvoice_volume_load_locked();
  if (ret < 0)
    {
      goto done;
    }

  if (g_volume_revision == UINT64_MAX)
    {
      ret = -EOVERFLOW;
      goto done;
    }

  record[4] = (uint8_t)volume_percent;
  bkvoice_volume_put64(transaction + 4, g_volume_revision + 1u);
  transaction[12] = (uint8_t)volume_percent;
  ret = bkprov_store_commit(&g_volume_store, g_volume_revision, transaction,
                            record, sizeof(record));
  if (ret == 0)
    {
      g_volume_revision++;
      g_volume_percent = volume_percent;
      g_volume_present = true;
    }
  else if (ret == -EINPROGRESS)
    {
      memset(&g_volume_store, 0, sizeof(g_volume_store));
      g_volume_loaded = false;
      g_volume_present = false;
    }

done:
  nxmutex_unlock(&g_volume_store_lock);
  return ret;
}

int bkvoice_volume_store_reload(void)
{
  int ret = nxmutex_lock(&g_volume_store_lock);

  if (ret < 0)
    {
      return ret;
    }

  if (!g_volume_started)
    {
      ret = -ENODEV;
    }
  else
    {
      memset(&g_volume_store, 0, sizeof(g_volume_store));
      g_volume_revision = 0;
      g_volume_percent = 0;
      g_volume_loaded = false;
      g_volume_present = false;
      ret = 0;
    }

  nxmutex_unlock(&g_volume_store_lock);
  return ret;
}
