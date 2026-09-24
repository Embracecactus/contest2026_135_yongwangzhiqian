/* SPDX-License-Identifier: Apache-2.0 */
/* Link both real owners. Only OS mount and USB hardware are replaced.
 * A mounted filesystem must never coexist with writable host export.
 */
#include "bk7258_media_volume.h"
#include <arch/chip/bk7258_usbmode.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>

static bool mounted;
static bool host_writable;
static int unmount_error;
static unsigned int exports;

int mkdir(const char *path, mode_t mode)
{ (void)path; (void)mode; return 0; }
int mount(const char *source, const char *target, const char *type,
          unsigned long flags, const void *data)
{
  (void)source; (void)target; (void)type; (void)flags; (void)data;
  assert(!host_writable);
  mounted = true;
  return 0;
}
int umount(const char *target)
{
  (void)target;
  if (unmount_error) { errno = unmount_error; return -1; }
  mounted = false;
  return 0;
}
int bk7258_usbcdc_initialize(void) { return 0; }
int bk7258_usbcdc_uninitialize(void) { return 0; }
int bk7258_usbmsc_initialize(const char *path)
{
  (void)path;
  assert(!mounted); /* Independent observer, not a private owner flag. */
  host_writable = true;
  exports++;
  return 0;
}
int bk7258_usbmsc_uninitialize(void) { host_writable = false; return 0; }
int nxsig_usleep(uint32_t us) { (void)us; return 0; }

int main(int argc, char **argv)
{
  assert(argc == 2);
  assert(bk7258_usbmode_initialize() == 0);
  assert(bk7258_media_volume_ota_prepare("/mnt/sdnand/test-package") == 0);
  assert(mounted && !host_writable);
  if (!strcmp(argv[1], "unmount-failure"))
    {
      unmount_error = EIO;
      assert(bk7258_media_volume_ota_release() == -EIO);
      assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == -EBUSY);
      assert(exports == 0 && mounted && !host_writable);
      unmount_error = 0;
    }
  else if (!strcmp(argv[1], "local-busy"))
    {
      assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == -EBUSY);
      assert(exports == 0 && mounted);
    }
  else if (!strcmp(argv[1], "wrong-owner"))
    {
      assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) == -EPERM);
      assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == -EBUSY);
      assert(exports == 0 && mounted);
    }
  else assert(!strcmp(argv[1], "handoff"));
  assert(bk7258_media_volume_ota_release() == 0);
  assert(!mounted);
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == 0);
  assert(host_writable && exports == 1);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == -EBUSY);
  assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == 0);
  assert(!host_writable);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == 0);
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) == 0);
  puts("CONTRACT_PASS");
  return 0;
}
