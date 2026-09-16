/****************************************************************************
 * app/bk7258/bk7258_display_service.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shaniu dual-eye runtime.  The service waits for the AIDK deferred SD NAND
 * and LCD registration, conditionally leases /dev/mmcsd0 against USB MSC,
 * mounts it only for bounded asset operations, and paints through the
 * standard framebuffer ABI.  Physical left/right orientation is
 * intentionally not guessed.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_DISPLAY_SERVICE

#include "bk7258_display_rpc.h"
#include "bk7258_display_service.h"
#include "bk7258_media_volume.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/mutex.h>
#include <nuttx/signal.h>
#include <nuttx/video/fb.h>


#define BKDISPLAY_BLOCKDEV       CONFIG_BK7258_DISPLAY_BLOCKDEV
#define BKDISPLAY_MOUNTROOT      "/mnt"
#define BKDISPLAY_MOUNTPOINT     "/mnt/sdnand"
#define BKDISPLAY_FB0            "/dev/fb0"
#define BKDISPLAY_FB1            "/dev/fb1"
#define BKDISPLAY_RETRY_US       500000u
#define BKDISPLAY_DEVICE_POLL_US 100000u
#define BKDISPLAY_MAPPING_FB0_RGB565 0x07ffu
#define BKDISPLAY_MAPPING_FB1_RGB565 0xf81fu

struct bkdisplay_service_s
{
  mutex_t lock;
  struct bkdisplay_service_status_s status;
  bool started;
  bool devices_ready;
  bool volume_leased;
  bool volume_mounted;
  uint8_t diagnostic_stage;
  int diagnostic_error;
};

enum bkdisplay_diagnostic_stage_e
{
  BKDISPLAY_DIAG_NONE = 0,
  BKDISPLAY_DIAG_ALLOCATE,
  BKDISPLAY_DIAG_VOLUME_OPEN,
  BKDISPLAY_DIAG_STORE_ENSURE,
  BKDISPLAY_DIAG_STORE_RESOLVE,
  BKDISPLAY_DIAG_PACK_OPEN,
  BKDISPLAY_DIAG_PACK_RENDER,
  BKDISPLAY_DIAG_VOLUME_CLOSE,
  BKDISPLAY_DIAG_FB0,
  BKDISPLAY_DIAG_FB1,
};

static bool bkdisplay_service_retryable(int ret);

static int bkdisplay_blockdev_acquire(void)
{
  return bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY);
}

static int bkdisplay_blockdev_release(void)
{
  return bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY);
}

static const char *bkdisplay_diagnostic_stage_name(uint8_t stage)
{
  switch (stage)
    {
      case BKDISPLAY_DIAG_ALLOCATE:
        return "allocate";
      case BKDISPLAY_DIAG_VOLUME_OPEN:
        return "volume-open";
      case BKDISPLAY_DIAG_STORE_ENSURE:
        return "store-ensure";
      case BKDISPLAY_DIAG_STORE_RESOLVE:
        return "store-resolve";
      case BKDISPLAY_DIAG_PACK_OPEN:
        return "pack-open";
      case BKDISPLAY_DIAG_PACK_RENDER:
        return "pack-render";
      case BKDISPLAY_DIAG_VOLUME_CLOSE:
        return "volume-close";
      case BKDISPLAY_DIAG_FB0:
        return "fb0";
      case BKDISPLAY_DIAG_FB1:
        return "fb1";
      default:
        return "none";
    }
}

static void bkdisplay_diagnostic_failure(struct bkdisplay_service_s *service,
                                         uint8_t stage, int ret)
{
  if (service->diagnostic_stage != stage || service->diagnostic_error != ret)
    {
      syslog(bkdisplay_service_retryable(ret) ? LOG_WARNING : LOG_ERR,
             "BKDISPLAY RENDER %s stage=%s ret=%d\n",
             bkdisplay_service_retryable(ret) ? "WAIT" : "FAIL",
             bkdisplay_diagnostic_stage_name(stage), ret);
      service->diagnostic_stage = stage;
      service->diagnostic_error = ret;
    }
}

static struct bkdisplay_service_s g_bkdisplay_service =
{
  .lock = NXMUTEX_INITIALIZER,
  .status =
  {
    .state = BKDISPLAY_SERVICE_STOPPED,
    .physical_mapping_verified = false,
  },
};

static int bkdisplay_service_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static bool bkdisplay_service_retryable(int ret)
{
  return ret == -ENOENT || ret == -EBUSY || ret == -EAGAIN ||
         ret == -ENODEV || ret == -ENXIO || ret == -ENOTDIR;
}

static bool bkdisplay_service_node(const char *path, bool block)
{
  struct stat statbuf;

  if (stat(path, &statbuf) < 0)
    {
      return false;
    }

  return block ? S_ISBLK(statbuf.st_mode) : S_ISCHR(statbuf.st_mode);
}

static int bkdisplay_volume_open(struct bkdisplay_service_s *service)
{
  int ret;

  if (service->volume_mounted)
    {
      return 0;
    }

  if (!service->volume_leased)
    {
      ret = bkdisplay_blockdev_acquire();
      if (ret < 0)
        {
          return ret;
        }

      service->volume_leased = true;
    }

  if ((mkdir(BKDISPLAY_MOUNTROOT, 0777) < 0 && errno != EEXIST) ||
      (mkdir(BKDISPLAY_MOUNTPOINT, 0777) < 0 && errno != EEXIST))
    {
      ret = bkdisplay_service_errno();
      goto release;
    }

  if (mount(BKDISPLAY_BLOCKDEV, BKDISPLAY_MOUNTPOINT, "vfat", 0, NULL) < 0)
    {
      ret = bkdisplay_service_errno();
      goto release;
    }

  service->volume_mounted = true;
  return 0;

release:
  if (bkdisplay_blockdev_release() == 0)
    {
      service->volume_leased = false;
    }

  return ret;
}

static int bkdisplay_volume_close(struct bkdisplay_service_s *service)
{
  int ret;

  if (service->volume_mounted)
    {
      if (umount(BKDISPLAY_MOUNTPOINT) < 0)
        {
          /* Keep the lease pinned: exporting a still-mounted FAT volume is
           * less safe than returning a recoverable service error.
           */

          return bkdisplay_service_errno();
        }

      service->volume_mounted = false;
    }

  if (!service->volume_leased)
    {
      return 0;
    }

  ret = bkdisplay_blockdev_release();
  if (ret == 0)
    {
      service->volume_leased = false;
    }

  return ret;
}

static int bkdisplay_framebuffer_write(const char *path,
                                       const uint16_t *pixels)
{
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  struct fb_area_s area;
  uint8_t *target;
  unsigned int row;
  int fd;
  int ret = 0;

  fd = open(path, O_RDWR);
  if (fd < 0)
    {
      return bkdisplay_service_errno();
    }

  memset(&vinfo, 0, sizeof(vinfo));
  memset(&pinfo, 0, sizeof(pinfo));
  if (ioctl(fd, FBIOGET_VIDEOINFO,
            (unsigned long)((uintptr_t)&vinfo)) < 0 ||
      ioctl(fd, FBIOGET_PLANEINFO,
            (unsigned long)((uintptr_t)&pinfo)) < 0)
    {
      ret = bkdisplay_service_errno();
      goto out;
    }

  if (vinfo.fmt != FB_FMT_RGB16_565 ||
      vinfo.xres != BKDISPLAY_CANVAS_WIDTH ||
      vinfo.yres != BKDISPLAY_CANVAS_HEIGHT || pinfo.bpp != 16 ||
      pinfo.fbmem == NULL ||
      pinfo.stride < BKDISPLAY_CANVAS_WIDTH * sizeof(uint16_t) ||
      pinfo.fblen < (size_t)pinfo.stride * BKDISPLAY_CANVAS_HEIGHT)
    {
      ret = -EPROTO;
      goto out;
    }

  target = pinfo.fbmem;
  for (row = 0; row < BKDISPLAY_CANVAS_HEIGHT; row++)
    {
      memcpy(target + (size_t)row * pinfo.stride,
             pixels + (size_t)row * BKDISPLAY_CANVAS_WIDTH,
             BKDISPLAY_CANVAS_WIDTH * sizeof(uint16_t));
    }

  area.x = 0;
  area.y = 0;
  area.w = BKDISPLAY_CANVAS_WIDTH;
  area.h = BKDISPLAY_CANVAS_HEIGHT;
  if (ioctl(fd, FBIO_UPDATE,
            (unsigned long)((uintptr_t)&area)) < 0)
    {
      ret = bkdisplay_service_errno();
    }

out:
  close(fd);
  return ret;
}

static void bkdisplay_status_error(struct bkdisplay_service_s *service,
                                   int ret)
{
  service->status.last_error = ret;
  service->status.state = bkdisplay_service_retryable(ret) ?
                          BKDISPLAY_SERVICE_WAITING_ASSET :
                          BKDISPLAY_SERVICE_ERROR;
}

static int bkdisplay_render_locked(struct bkdisplay_service_s *service,
                                   const char *expression)
{
  struct bkdisplay_store_selection_s selection;
  struct bkdisplay_pack_s *pack = NULL;
  uint16_t *pixels = NULL;
  int close_ret;
  int ret;
  uint8_t stage = BKDISPLAY_DIAG_NONE;

  if (!service->devices_ready)
    {
      return -EAGAIN;
    }

  stage = BKDISPLAY_DIAG_ALLOCATE;
  pixels = malloc(BKDISPLAY_CANVAS_PIXELS * sizeof(*pixels));
  if (pixels == NULL)
    {
      return -ENOMEM;
    }

  stage = BKDISPLAY_DIAG_VOLUME_OPEN;
  ret = bkdisplay_volume_open(service);
  if (ret < 0)
    {
      goto out;
    }

  /* A freshly formatted SD NAND has no product directories yet.  Build the
   * store skeleton before resolving the optional active marker or fallback
   * pack.  Missing pack content remains a retryable WAITING_ASSET state.
   */

  stage = BKDISPLAY_DIAG_STORE_ENSURE;
  ret = bkdisplay_store_ensure(BKDISPLAY_MOUNTPOINT);
  if (ret == 0)
    {
      stage = BKDISPLAY_DIAG_STORE_RESOLVE;
      ret = bkdisplay_store_resolve(BKDISPLAY_MOUNTPOINT, &selection);
    }
  if (ret == 0)
    {
      stage = BKDISPLAY_DIAG_PACK_OPEN;
      ret = bkdisplay_pack_open(selection.path, &pack, NULL);
    }

  if (ret == 0)
    {
      /* Until a physical board calibration establishes which fitted panel is
       * left/right, require a shared expression and paint it identically to
       * both displays.  This avoids embedding a guessed orientation in the
       * product resource pack.
       */

      stage = BKDISPLAY_DIAG_PACK_RENDER;
      ret = bkdisplay_pack_render(pack, expression,
                                  BKDISPLAY_SIDE_UNMAPPED, pixels,
                                  BKDISPLAY_CANVAS_PIXELS);
    }

  bkdisplay_pack_close(pack);
  if (ret == 0)
    {
      stage = BKDISPLAY_DIAG_VOLUME_CLOSE;
    }

  close_ret = bkdisplay_volume_close(service);
  if (ret == 0 && close_ret < 0)
    {
      ret = close_ret;
    }

  if (ret == 0)
    {
      stage = BKDISPLAY_DIAG_FB0;
      ret = bkdisplay_framebuffer_write(BKDISPLAY_FB0, pixels);
    }

  if (ret == 0)
    {
      stage = BKDISPLAY_DIAG_FB1;
      ret = bkdisplay_framebuffer_write(BKDISPLAY_FB1, pixels);
    }

  if (ret == 0)
    {
      service->status.state = BKDISPLAY_SERVICE_READY;
      service->status.last_error = 0;
      service->status.screen_count = 2;
      service->status.render_sequence++;
      snprintf(service->status.expression,
               sizeof(service->status.expression), "%s", expression);
      snprintf(service->status.pack_id, sizeof(service->status.pack_id),
               "%s", selection.info.pack_id);
      service->status.pack_revision = selection.info.revision;
      service->diagnostic_stage = BKDISPLAY_DIAG_NONE;
      service->diagnostic_error = 0;
      syslog(LOG_INFO,
             "BKDISPLAY RENDER PASS expression=%s pack=%s revision=%lu "
             "screens=2 mapping=unverified fallback=%u sequence=%lu\n",
             expression, selection.info.pack_id,
             (unsigned long)selection.info.revision,
             selection.fallback ? 1u : 0u,
             (unsigned long)service->status.render_sequence);
    }

out:
  free(pixels);
  if (ret < 0)
    {
      bkdisplay_diagnostic_failure(service, stage, ret);
      bkdisplay_status_error(service, ret);
    }

  return ret;
}

static int bkdisplay_wait_for_devices(void)
{
  unsigned int elapsed = 0;

  while (elapsed < CONFIG_BK7258_DISPLAY_DEVICE_TIMEOUT_MS)
    {
      if (bkdisplay_service_node(BKDISPLAY_BLOCKDEV, true) &&
          bkdisplay_service_node(BKDISPLAY_FB0, false) &&
          bkdisplay_service_node(BKDISPLAY_FB1, false))
        {
          return 0;
        }

      (void)nxsig_usleep(BKDISPLAY_DEVICE_POLL_US);
      elapsed += BKDISPLAY_DEVICE_POLL_US / 1000u;
    }

  return -ETIMEDOUT;
}

static int bkdisplay_worker(int argc, char *argv[])
{
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  int ret;

  (void)argc;
  (void)argv;

  ret = bkdisplay_wait_for_devices();
  if (ret < 0)
    {
      if (nxmutex_lock(&service->lock) >= 0)
        {
          bkdisplay_status_error(service, ret);
          service->started = false;
          nxmutex_unlock(&service->lock);
        }

      syslog(LOG_ERR, "BKDISPLAY START FAIL stage=device-wait ret=%d\n",
             ret);
      return ret;
    }

  for (;;)
    {
      ret = nxmutex_lock(&service->lock);
      if (ret < 0)
        {
          return ret;
        }

      /* A command may have rendered the first expression while this worker
       * was sleeping for a missing asset.  Preserve that newer operator
       * choice instead of overwriting it with the boot-time neutral frame.
       */

      if (service->status.state == BKDISPLAY_SERVICE_READY)
        {
          nxmutex_unlock(&service->lock);
          return 0;
        }

      service->devices_ready = true;
      service->status.state = BKDISPLAY_SERVICE_WAITING_ASSET;
      ret = bkdisplay_render_locked(service, "neutral");
      nxmutex_unlock(&service->lock);
      if (ret == 0)
        {
          syslog(LOG_INFO,
                 "BKDISPLAY SERVICE READY dev=/dev/fb0,/dev/fb1 "
                 "storage=" BKDISPLAY_BLOCKDEV " mount=short-lived\n");
          return 0;
        }

      if (!bkdisplay_service_retryable(ret))
        {
          if (nxmutex_lock(&service->lock) >= 0)
            {
              service->started = false;
              nxmutex_unlock(&service->lock);
            }

          syslog(LOG_ERR,
                 "BKDISPLAY START FAIL stage=neutral-render ret=%d\n",
                 ret);
          return ret;
        }

      (void)nxsig_usleep(BKDISPLAY_RETRY_US);
    }
}

int bk7258_display_service_prepare(void)
{
  return 0;
}

int bk7258_display_service_start(void)
{
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  pid_t pid;
  int ret;

  ret = nxmutex_lock(&service->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (service->started)
    {
      nxmutex_unlock(&service->lock);
      return 0;
    }

  service->status.state = BKDISPLAY_SERVICE_WAITING_DEVICES;
  service->status.last_error = 0;
  ret = bk7258_display_rpc_server_initialize();
  if (ret < 0)
    {
      bkdisplay_status_error(service, ret);
    }
  else
    {
      pid = task_create("bkdisplay-init",
                        CONFIG_BK7258_DISPLAY_SERVICE_PRIORITY,
                        CONFIG_BK7258_DISPLAY_SERVICE_STACKSIZE,
                        bkdisplay_worker, NULL);
      if (pid < 0)
        {
          ret = bkdisplay_service_errno();
          bkdisplay_status_error(service, ret);
        }
      else
        {
          service->started = true;
          ret = 0;
          syslog(LOG_INFO,
                 "BKDISPLAY SERVICE SCHEDULED storage=" BKDISPLAY_BLOCKDEV
                 " default=" BKDISPLAY_STORE_DEFAULT_PACK
                 " mapping=unverified\n");
        }
    }

  nxmutex_unlock(&service->lock);
  return ret;
}

int bk7258_display_set_expression(const char *expression)
{
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  int ret;

  if (expression == NULL || *expression == '\0')
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&service->lock);
  if (ret >= 0)
    {
      ret = bkdisplay_render_locked(service, expression);
      nxmutex_unlock(&service->lock);
    }

  return ret;
}

int bk7258_display_replace_expression(const char *expected,
                                      const char *replacement)
{
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  int ret;

  if (expected == NULL || *expected == '\0' ||
      replacement == NULL || *replacement == '\0')
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&service->lock);
  if (ret >= 0)
    {
      ret = strcmp(service->status.expression, expected) == 0 ?
            bkdisplay_render_locked(service, replacement) : -EAGAIN;
      nxmutex_unlock(&service->lock);
    }

  return ret;
}

int bk7258_display_show_mapping_test(void)
{
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  uint16_t *pixels = NULL;
  size_t index;
  uint8_t stage = BKDISPLAY_DIAG_NONE;
  int ret;

  ret = nxmutex_lock(&service->lock);
  if (ret < 0)
    {
      return ret;
    }

  /* Calibration is meaningful only after the normal resource-backed frame
   * has reached both displays.  Keep this diagnostic independent of the
   * product eye pack so it can identify the framebuffer-to-physical-panel
   * mapping without embedding that board fact in an asset.
   */

  if (!service->devices_ready ||
      service->status.state != BKDISPLAY_SERVICE_READY)
    {
      ret = -EAGAIN;
      goto out;
    }

  stage = BKDISPLAY_DIAG_ALLOCATE;
  pixels = malloc(BKDISPLAY_CANVAS_PIXELS * sizeof(*pixels));
  if (pixels == NULL)
    {
      ret = -ENOMEM;
      goto failed;
    }

  for (index = 0; index < BKDISPLAY_CANVAS_PIXELS; index++)
    {
      pixels[index] = BKDISPLAY_MAPPING_FB0_RGB565;
    }

  stage = BKDISPLAY_DIAG_FB0;
  ret = bkdisplay_framebuffer_write(BKDISPLAY_FB0, pixels);
  if (ret < 0)
    {
      goto failed;
    }

  for (index = 0; index < BKDISPLAY_CANVAS_PIXELS; index++)
    {
      pixels[index] = BKDISPLAY_MAPPING_FB1_RGB565;
    }

  stage = BKDISPLAY_DIAG_FB1;
  ret = bkdisplay_framebuffer_write(BKDISPLAY_FB1, pixels);
  if (ret < 0)
    {
      goto failed;
    }

  service->status.last_error = 0;
  service->status.screen_count = 2;
  service->status.render_sequence++;
  snprintf(service->status.expression, sizeof(service->status.expression),
           "%s", "mapping-test");
  service->diagnostic_stage = BKDISPLAY_DIAG_NONE;
  service->diagnostic_error = 0;
  syslog(LOG_NOTICE,
         "BKDISPLAY CALIBRATE PASS fb0=cyan fb1=magenta "
         "mapping=unverified sequence=%lu\n",
         (unsigned long)service->status.render_sequence);
  ret = 0;
  goto out;

failed:
  bkdisplay_diagnostic_failure(service, stage, ret);
  bkdisplay_status_error(service, ret);

out:
  free(pixels);
  nxmutex_unlock(&service->lock);
  return ret;
}

static int bkdisplay_update_pack(const char *filename, bool install)
{
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  struct bkdisplay_store_selection_s selection;
  int close_ret;
  int ret;

  ret = nxmutex_lock(&service->lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_volume_open(service);
  if (ret == 0)
    {
      ret = install ?
        bkdisplay_store_install(BKDISPLAY_MOUNTPOINT, filename, &selection) :
        bkdisplay_store_activate(BKDISPLAY_MOUNTPOINT, filename, &selection);
      close_ret = bkdisplay_volume_close(service);
      if (ret == 0 && close_ret < 0)
        {
          ret = close_ret;
        }
    }

  if (ret == 0)
    {
      ret = bkdisplay_render_locked(service, "neutral");
    }

  if (ret < 0)
    {
      bkdisplay_status_error(service, ret);
    }

  nxmutex_unlock(&service->lock);
  return ret;
}

int bk7258_display_install(const char *filename)
{
  return filename == NULL ? -EINVAL :
         bkdisplay_update_pack(filename, true);
}

int bk7258_display_activate(const char *filename)
{
  return filename == NULL ? -EINVAL :
         bkdisplay_update_pack(filename, false);
}

int bk7258_display_get_status(struct bkdisplay_service_status_s *status)
{
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  int ret;

  if (status == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&service->lock);
  if (ret >= 0)
    {
      *status = service->status;
      nxmutex_unlock(&service->lock);
    }

  return ret;
}

#endif /* CONFIG_BK7258_DISPLAY_SERVICE */
