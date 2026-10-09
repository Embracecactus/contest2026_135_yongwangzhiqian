/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Exercise the real preference, storage, media-volume and USB-mode owners.
 * Only the KVDB, mount and USB hardware edges are replaced.  A successful
 * writable MSC handoff must make the cached SD-backed playback volume stale.
 */

#include "bk7258_preferences.h"
#include <arch/chip/bk7258_usbmode.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unqlite.h>

static const char *g_volume = "60";
static bool g_mounted;
static bool g_host_writable;
static int g_cdc_start_error;
static int g_msc_start_error;
static int g_msc_stop_error;
static unsigned int g_volume_reads;
static unsigned int g_mounts;
static unsigned int g_unmounts;

int mkdir(const char *path, mode_t mode)
{
  (void)path;
  (void)mode;
  return 0;
}

int mount(const char *source, const char *target, const char *type,
          unsigned long flags, const void *data)
{
  assert(!strcmp(source, "/dev/mmcsd0"));
  assert(!strcmp(target, "/mnt/sdnand"));
  assert(!strcmp(type, "fatfs"));
  assert(flags == 0 && data == NULL);
  assert(!g_mounted && !g_host_writable);
  g_mounted = true;
  g_mounts++;
  return 0;
}

int umount(const char *target)
{
  assert(!strcmp(target, "/mnt/sdnand"));
  assert(g_mounted && !g_host_writable);
  g_mounted = false;
  g_unmounts++;
  return 0;
}

int bk7258_usbcdc_initialize(void)
{
  assert(!g_host_writable);
  return g_cdc_start_error;
}

int bk7258_usbcdc_uninitialize(void)
{
  assert(!g_host_writable);
  return 0;
}

int bk7258_usbmsc_initialize(const char *path)
{
  assert(!strcmp(path, "/dev/mmcsd0"));
  assert(!g_mounted && !g_host_writable);
  if (g_msc_start_error)
    {
      return g_msc_start_error;
    }
  g_host_writable = true;
  return 0;
}

int bk7258_usbmsc_uninitialize(void)
{
  /* USB mode also tears down a class whose initialize call failed.  The
   * hardware edge may therefore be only partially started here.
   */
  assert(!g_mounted);
  if (g_msc_stop_error)
    {
      return g_msc_stop_error;
    }
  g_host_writable = false;
  return 0;
}

int nxsig_usleep(uint32_t usec)
{
  assert(usec == 1000u);
  return 0;
}

int property_get_with_err(const char *key, char *value)
{
  assert(g_mounted && !g_host_writable);
  if (!strcmp(key, "persist.shaniu.volume"))
    {
      strcpy(value, g_volume);
      g_volume_reads++;
      return (int)strlen(value);
    }
  if (!strcmp(key, "persist.shaniu.persona"))
    {
      strcpy(value, "gentle");
      return 6;
    }
  return UNQLITE_NOTFOUND;
}

int property_set(const char *key, const char *value)
{
  (void)key;
  (void)value;
  return -ENOTSUP;
}

int property_delete(const char *key)
{
  (void)key;
  return -ENOTSUP;
}

int property_commit(void)
{
  return -ENOTSUP;
}

static unsigned int initial_read(void)
{
  unsigned int volume = 0;
  assert(bk7258_usbmode_initialize() == 0);
  assert(bk7258_preferences_playback_volume(&volume) == 0);
  assert(volume == 60u);
  assert(g_volume_reads == 1u);
  assert(g_mounts == 1u && g_unmounts == 1u);
  assert(!g_mounted && !g_host_writable);
  return volume;
}

static void host_write_volume(const char *value)
{
  assert(g_host_writable && !g_mounted);
  g_volume = value;
}

static void test_roundtrip(void)
{
  unsigned int volume = initial_read();
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == 0);
  host_write_volume("80");
  assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == 0);
  assert(bk7258_preferences_playback_volume(&volume) == 0);
  assert(volume == 80u);
  assert(g_volume_reads == 2u);
  assert(g_mounts == 2u && g_unmounts == 2u);
}

static void test_failed_handoff(void)
{
  unsigned int volume = initial_read();
  g_msc_start_error = -EIO;
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == -EIO);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_CDC);
  assert(!g_host_writable && !g_mounted);
  assert(bk7258_preferences_playback_volume(&volume) == 0);
  assert(volume == 60u && g_volume_reads == 1u);
  assert(g_mounts == 1u && g_unmounts == 1u);
}

static void test_failed_exit(void)
{
  unsigned int volume = initial_read();
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == 0);
  host_write_volume("80");
  g_msc_stop_error = -EBUSY;
  volume = 99u;
  assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == -EBUSY);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_MSC);
  /* The host still owns the volume.  The last confirmed playback value may
   * remain usable without mounting SD, but the host edit is not published yet.
   */
  assert(bk7258_preferences_playback_volume(&volume) == 0);
  assert(volume == 60u && g_volume_reads == 1u);
  g_msc_stop_error = 0;
  assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == 0);
  assert(bk7258_preferences_playback_volume(&volume) == 0);
  assert(volume == 80u && g_volume_reads == 2u);
}

static void test_failed_local_start(void)
{
  unsigned int volume = initial_read();
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == 0);
  host_write_volume("80");
  g_cdc_start_error = -EIO;
  assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == -EIO);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_MSC);
  assert(g_host_writable && !g_mounted);
  assert(bk7258_preferences_playback_volume(&volume) == 0);
  assert(volume == 60u && g_volume_reads == 1u);
  g_cdc_start_error = 0;
  assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == 0);
  assert(bk7258_preferences_playback_volume(&volume) == 0);
  assert(volume == 80u && g_volume_reads == 2u);
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  if (!strcmp(argv[1], "roundtrip"))
    {
      test_roundtrip();
    }
  else if (!strcmp(argv[1], "failed-handoff"))
    {
      test_failed_handoff();
    }
  else if (!strcmp(argv[1], "failed-exit"))
    {
      test_failed_exit();
    }
  else
    {
      assert(!strcmp(argv[1], "failed-local-start"));
      test_failed_local_start();
    }
  puts("CONTRACT_PASS");
  return 0;
}
