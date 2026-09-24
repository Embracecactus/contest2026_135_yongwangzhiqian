/* SPDX-License-Identifier: Apache-2.0 */
/* Actual backend stop, external USB driver and blockdriver boundaries only. */
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
typedef int irqstate_t;
struct inode { int tag; };
static struct inode device;
static struct inode *g_bk7258_usbmsc_inode = &device;
static struct { int sectors; } g_bk7258_usbmsc_geometry;
static bool s_msc_storage_init = true;
static int thread_op;
static int g_bk7258_usbmsc_worker_lock, g_bk7258_usbmsc_lock;
static int driver_error, closes, deinitializes, disconnected;
static int nxmutex_lock(int *lock) { assert(!*lock); *lock=1; return 0; }
static int nxmutex_unlock(int *lock) { assert(*lock); *lock=0; return 0; }
static irqstate_t enter_critical_section(void) { return 0; }
static void leave_critical_section(irqstate_t flags) { (void)flags; }
static void bk7258_usbmsc_soft_disconnect(void) { disconnected++; }
static int usbd_deinitialize(void)
{
  assert(g_bk7258_usbmsc_worker_lock && g_bk7258_usbmsc_lock);
  assert(!s_msc_storage_init && disconnected);
  deinitializes++;
  return driver_error;
}
static void usbd_set_status(int status) { assert(status == 0); }
static int close_blockdriver(struct inode *inode)
{ assert(inode == &device); closes++; return 0; }
#include "bk7258_usbmsc_stop.inc"
int main(int argc, char **argv)
{
  assert(argc == 2);
  if (!strcmp(argv[1], "retry"))
    {
      driver_error = -EIO;
      assert(bk7258_usbmsc_uninitialize() == -EIO);
      assert(closes == 0);
      assert(!g_bk7258_usbmsc_worker_lock && !g_bk7258_usbmsc_lock);
      driver_error = 0;
    }
  else assert(!strcmp(argv[1], "normal"));
  assert(bk7258_usbmsc_uninitialize() == 0);
  assert(closes == 1);
  int calls = deinitializes;
  assert(bk7258_usbmsc_uninitialize() == -ENODEV);
  assert(closes == 1 && deinitializes == calls);
  puts("CONTRACT_PASS");
  return 0;
}
