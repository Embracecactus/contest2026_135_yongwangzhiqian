/* SPDX-License-Identifier: Apache-2.0 */
#include <arch/chip/bk7258_usbmode.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
static int cdc_starts, stops, cleanup_error = -EIO;
static int resources;
int bk7258_usbcdc_initialize(void) { assert(!resources); cdc_starts++; return 0; }
int bk7258_usbcdc_uninitialize(void) { return 0; }
int bk7258_usbmsc_initialize(const char *path)
{ (void)path; assert(!resources); resources=1; return -ENOMEM; }
int bk7258_usbmsc_uninitialize(void)
{ stops++; if(cleanup_error) return cleanup_error; resources=0; return 0; }
int nxsig_usleep(uint32_t usec) { (void)usec; return 0; }
int main(void)
{
  assert(bk7258_usbmode_initialize() == 0);
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) < 0);
  assert(resources && cdc_starts == 1 && stops == 1);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_NONE);
  assert(bk7258_usbmode_blockdev_acquire() == -EBUSY);
  assert(bk7258_usbmode_initialize() == -EBUSY);
  assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == -EIO);
  assert(resources && cdc_starts == 1);
  cleanup_error = 0;
  assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == 0);
  assert(!resources && cdc_starts == 2);
  assert(bk7258_usbmode_blockdev_acquire() == 0);
  assert(bk7258_usbmode_blockdev_release() == 0);
  puts("CONTRACT_PASS");
  return 0;
}
