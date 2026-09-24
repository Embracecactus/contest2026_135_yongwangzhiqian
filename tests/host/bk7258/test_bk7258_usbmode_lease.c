/****************************************************************************
 * tests/host/bk7258/test_bk7258_usbmode_lease.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/chip/bk7258_usbmode.h>

static unsigned int g_cdc_starts;
static unsigned int g_cdc_stops;
static unsigned int g_msc_starts;
static unsigned int g_msc_stops;
static int g_cdc_start_error;
static int g_cdc_stop_error;
static int g_msc_start_error;
static int g_msc_stop_error;

int bk7258_usbcdc_initialize(void)
{
  g_cdc_starts++;
  return g_cdc_start_error;
}

int bk7258_usbcdc_uninitialize(void)
{
  g_cdc_stops++;
  return g_cdc_stop_error;
}

int bk7258_usbmsc_initialize(const char *blockdev)
{
  assert(strcmp(blockdev, "/dev/mmcsd0") == 0);
  g_msc_starts++;
  return g_msc_start_error;
}

int bk7258_usbmsc_uninitialize(void)
{
  g_msc_stops++;
  return g_msc_stop_error;
}

int nxsig_usleep(uint32_t usec)
{
  assert(usec == 1000u);
  return 0;
}

static void test_queries_and_failed_handoffs(void)
{
  unsigned int starts = g_msc_starts;
  unsigned int stops = g_cdc_stops;

  /* Queries and selecting the current mode cannot re-enumerate USB. */

  for (unsigned int i = 0; i < 20; i++)
    {
      assert(bk7258_usbmode_get() == BK7258_USBMODE_CDC);
      assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == 0);
    }

  assert(g_msc_starts == starts && g_cdc_stops == stops);
  g_cdc_stop_error = -EBUSY;
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == -EBUSY);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_CDC);
  assert(g_msc_starts == starts);
  g_cdc_stop_error = 0;

  /* Failed startup restores the previous backend; failed rollback must
   * report NONE rather than claiming that CDC is running.
   */

  g_msc_start_error = -EIO;
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == -EIO);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_CDC);
  g_cdc_start_error = -ENODEV;
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == -EIO);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_NONE);
  g_cdc_start_error = 0;
  g_msc_start_error = 0;
  assert(bk7258_usbmode_initialize() == 0);
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == 0);

  g_msc_stop_error = -EBUSY;
  starts = g_cdc_starts;
  assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == -EBUSY);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_MSC);
  assert(g_cdc_starts == starts);
  assert(bk7258_usbmode_blockdev_acquire() == -EBUSY);
  g_msc_stop_error = 0;
  assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == 0);
  assert(bk7258_usbmode_blockdev_acquire() == 0);
  assert(bk7258_usbmode_blockdev_release() == 0);
}

int main(void)
{
  assert(bk7258_usbmode_get() == BK7258_USBMODE_NONE);
  assert(bk7258_usbmode_blockdev_acquire() == 0);
  assert(bk7258_usbmode_blockdev_release() == 0);
  assert(bk7258_usbmode_blockdev_release() == -EINVAL);

  assert(bk7258_usbmode_initialize() == 0);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_CDC);
  assert(g_cdc_starts == 1 && g_cdc_stops == 0);

  assert(bk7258_usbmode_blockdev_acquire() == 0);
  assert(bk7258_usbmode_blockdev_acquire() == 0);
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == -EBUSY);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_CDC);
  assert(g_cdc_stops == 0 && g_msc_starts == 0);
  assert(bk7258_usbmode_blockdev_release() == 0);
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == -EBUSY);
  assert(bk7258_usbmode_blockdev_release() == 0);

  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == 0);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_MSC);
  assert(g_cdc_stops == 1 && g_msc_starts == 1);
  assert(bk7258_usbmode_blockdev_acquire() == -EBUSY);

  assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == 0);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_CDC);
  assert(g_msc_stops == 1 && g_cdc_starts == 2);
  assert(bk7258_usbmode_blockdev_acquire() == 0);
  assert(bk7258_usbmode_blockdev_release() == 0);
  assert(bk7258_usbmode_set(BK7258_USBMODE_NONE) == -EINVAL);

  test_queries_and_failed_handoffs();
  printf("BK7258_USBMODE_LEASE_HOST_PASS\n");
  return 0;
}
