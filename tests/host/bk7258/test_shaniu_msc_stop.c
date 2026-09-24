/* SPDX-License-Identifier: Apache-2.0 */
/* Actual backend stop, external USB driver and blockdriver boundaries only. */
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <syslog.h>
#define CONFIG_USBDEV_MSC_BLOCK_SIZE 512
#define MSC_OUT_EP 2
#define MSC_IN_EP 129
#define OK 0
typedef int irqstate_t;
struct inode;
struct geometry { bool geo_available; uint64_t geo_nsectors; unsigned int geo_sectorsize; };
struct block_operations { int (*geometry)(struct inode *, struct geometry *); void *read; void *write; };
struct inode { union { struct block_operations *i_bops; } u; };
static struct inode device;
static struct inode *g_bk7258_usbmsc_inode = &device;
static struct geometry g_bk7258_usbmsc_geometry;
static bool s_msc_storage_init = true;
static int thread_op;
static int g_bk7258_usbmsc_worker_lock, g_bk7258_usbmsc_lock;
static int driver_error, close_error, closes, deinitializes, disconnected;
static bool reference_released;
static int nxmutex_lock(int *lock) { assert(!*lock); *lock=1; return 0; }
static int nxmutex_unlock(int *lock) { assert(*lock); *lock=0; return 0; }
static irqstate_t enter_critical_section(void) { return 0; }
static void leave_critical_section(irqstate_t flags) { (void)flags; }
static void bk7258_usbmsc_soft_disconnect(void) { disconnected++; }
static int usbd_deinitialize(void)
{
  assert(g_bk7258_usbmsc_worker_lock && g_bk7258_usbmsc_lock);
  assert(!s_msc_storage_init);
  deinitializes++;
  return driver_error;
}
static void usbd_set_status(int status) { assert(status == 0); }
static int close_blockdriver(struct inode *inode)
{
  /* NuttX fs_closeblockdriver.c releases a valid inode even when the
   * underlying block close returns an error. Model that boundary explicitly.
   */
  assert(inode == &device && !reference_released);
  reference_released = true;
  closes++;
  return close_error;
}
static int start_error, opens;
struct usbd_interface { int dummy; };
static struct usbd_interface gs_intf0;
static char mass_ep_data[8], msc_storage_descriptor[8];
static int geometry(struct inode *inode, struct geometry *g)
{ assert(inode == &device); *g = (struct geometry){true, 1024, 512}; return 0; }
static struct block_operations ops = {geometry, &device, &device};
static int open_blockdriver(const char *path, int flags, struct inode **inode)
{ assert(path && flags == 0); opens++; reference_released = false; device.u.i_bops = &ops; *inode = &device; return 0; }
static void usbd_desc_register(char *desc) { (void)desc; }
static struct usbd_interface *usbd_msc_init_intf(struct usbd_interface *i, int a, int b)
{ (void)a; (void)b; return i; }
static void usbd_add_interface(struct usbd_interface *i) { assert(i == &gs_intf0); }
static int usbd_initialize(void) { return start_error; }
#include "bk7258_usbmsc_start.inc"
#include "bk7258_usbmsc_stop.inc"
int main(int argc, char **argv)
{
  assert(argc == 2);
  if (!strcmp(argv[1], "close-error"))
    {
      close_error = -EIO;
      assert(bk7258_usbmsc_uninitialize() == -EIO);
      assert(closes == 1 && reference_released);
      assert(!g_bk7258_usbmsc_worker_lock && !g_bk7258_usbmsc_lock);
      int calls = deinitializes;
      assert(bk7258_usbmsc_uninitialize() == -ENODEV);
      assert(closes == 1 && deinitializes == calls);
      close_error = 0;
      assert(bk7258_usbmsc_initialize("/dev/test") == 0 && opens == 1);
      assert(bk7258_usbmsc_uninitialize() == 0 && closes == 2);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "start-cleanup"))
    {
      s_msc_storage_init = false;
      g_bk7258_usbmsc_inode = NULL;
      start_error = -ENOMEM;
      driver_error = -EIO;
      assert(bk7258_usbmsc_initialize("/dev/test") < 0);
      assert(closes == 0);
      assert(bk7258_usbmsc_initialize("/dev/test") == -EBUSY && opens == 1);
      driver_error = 0;
      assert(bk7258_usbmsc_uninitialize() == 0 && closes == 1);
      puts("CONTRACT_PASS");
      return 0;
    }
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
