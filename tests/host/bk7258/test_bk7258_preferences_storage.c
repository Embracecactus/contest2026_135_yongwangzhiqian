/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include "bk7258_preferences_storage.h"
#include "bk7258_media_volume.h"

static int lease, mounted, acquire_error, mount_error, mkdir_error;
static int unmount_error, release_error, unmount_calls, acquire_calls;
int bk7258_media_volume_acquire(enum bk7258_media_volume_owner_e owner)
{
  assert(owner == BK7258_MEDIA_VOLUME_PREFERENCES);
  acquire_calls++;
  if (acquire_error) return acquire_error;
  assert(!lease && !mounted);
  lease = 1;
  return 0;
}
int bk7258_media_volume_release(enum bk7258_media_volume_owner_e owner)
{
  assert(owner == BK7258_MEDIA_VOLUME_PREFERENCES && lease && !mounted);
  if (release_error) return release_error;
  lease = 0;
  return 0;
}
int mkdir(const char *path, mode_t mode)
{
  (void)mode;
  assert(lease && (!strcmp(path, "/mnt") || !strcmp(path, "/mnt/sdnand")));
  errno = mkdir_error ? mkdir_error : EEXIST;
  return -1;
}
int mount(const char *device, const char *path, const char *type,
          unsigned long flags, const void *data)
{
  assert(lease && !mounted && !strcmp(device, "/dev/mmcsd0"));
  assert(!strcmp(path, "/mnt/sdnand") && !strcmp(type, "fatfs"));
  assert(flags == 0 && data == NULL);
  if (mount_error) { errno = mount_error; return -1; }
  mounted = 1;
  return 0;
}
int umount(const char *path)
{
  assert(lease && mounted && !strcmp(path, "/mnt/sdnand"));
  unmount_calls++;
  if (unmount_error) { errno = unmount_error; return -1; }
  mounted = 0;
  return 0;
}
int main(void)
{
  acquire_error = -EBUSY;
  assert(bk7258_preferences_storage_begin() == -EBUSY);
  acquire_error = 0;
  mkdir_error = EACCES;
  assert(bk7258_preferences_storage_begin() == -EACCES);
  assert(!lease && !mounted);
  mkdir_error = 0;
  mount_error = EBUSY;
  assert(bk7258_preferences_storage_begin() == -EBUSY);
  assert(!lease && !mounted && !unmount_calls);
  mount_error = 0;
  assert(bk7258_preferences_storage_begin() == 0);
  assert(bk7258_preferences_storage_end(-ENOSPC) == -ENOSPC);
  assert(!lease && !mounted);
  assert(bk7258_preferences_storage_begin() == 0);
  unmount_error = EBUSY;
  assert(bk7258_preferences_storage_end(0) == -EBUSY);
  assert(lease && mounted);
  int previous = acquire_calls;
  assert(bk7258_preferences_storage_begin() == -EBUSY);
  assert(acquire_calls == previous);
  unmount_error = 0;
  assert(bk7258_preferences_storage_begin() == 0);
  release_error = -EIO;
  assert(bk7258_preferences_storage_end(0) == -EIO);
  assert(lease && !mounted);
  previous = acquire_calls;
  assert(bk7258_preferences_storage_begin() == -EIO);
  assert(acquire_calls == previous);
  release_error = 0;
  assert(bk7258_preferences_storage_begin() == 0);
  assert(bk7258_preferences_storage_end(0) == 0);
  assert(!lease && !mounted);
  puts("BK7258_PREFERENCES_STORAGE_HOST_PASS");
  return 0;
}
