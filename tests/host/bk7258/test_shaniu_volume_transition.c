/* SPDX-License-Identifier: Apache-2.0 */
/* Deterministic overlap at the external USB lease boundary. The production
 * volume owner is linked unchanged; callbacks stand in for task interleaving.
 */
#include "bk7258_media_volume.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool acquiring;
static bool releasing;
static int leases;
static int acquire_error;
static int release_error;
static unsigned int release_calls;
static bool inject_acquire;
static bool inject_release;

int bk7258_usbmode_blockdev_acquire(void)
{
  acquiring = true;
  if (inject_acquire)
    {
      inject_acquire = false;
      assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) < 0);
      assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION) == -EBUSY);
    }
  acquiring = false;
  if (acquire_error) return acquire_error;
  leases++;
  return 0;
}

int bk7258_usbmode_blockdev_release(void)
{
  /* No release before acquisition completes; only one release per lease. */
  assert(!acquiring && !releasing);
  releasing = true;
  release_calls++;
  if (inject_release)
    {
      inject_release = false;
      assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) < 0);
      assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION) == -EBUSY);
      assert(bk7258_media_volume_release((enum bk7258_media_volume_owner_e)-1) < 0);
    }
  releasing = false;
  if (release_error) return release_error;
  assert(leases == 1);
  leases--;
  return 0;
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  inject_acquire = !strcmp(argv[1], "acquiring");
  inject_release = !strcmp(argv[1], "releasing");
  assert(inject_acquire || inject_release || !strcmp(argv[1], "retry"));
  if (!strcmp(argv[1], "retry"))
    {
      acquire_error = -EIO;
      assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == -EIO);
      acquire_error = 0;
    }
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == 0);
  assert(leases == 1);
  if (!strcmp(argv[1], "retry"))
    {
      release_error = -EIO;
      assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) == -EIO);
      assert(leases == 1);
      assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION) == -EBUSY);
      release_error = 0;
    }
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) == 0);
  assert(leases == 0);
  assert(release_calls == (!strcmp(argv[1], "retry") ? 2u : 1u));
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) == -EPERM);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION) == 0);
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_VISION) == 0);
  puts("CONTRACT_PASS");
  return 0;
}
