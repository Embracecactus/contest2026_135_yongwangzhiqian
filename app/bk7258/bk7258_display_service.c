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
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <time.h>
#ifdef CONFIG_BK7258_PROVISION_NATIVE
#include "qrcodegen.h"
#include <mbedtls/platform_util.h>
#endif

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
/* SN1 carries 107 alphanumeric characters. Version 4-M holds only 90;
 * 5-M fits the complete pin/secret and its 90-pixel quiet-zone square
 * remains inside the 160-pixel round panel at integer 2x scaling. */
#define BKDISPLAY_QR_VERSION 5

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
  uint16_t *frames[5];
  uint16_t *speaking_frame;
  bool speaking_painted;
  unsigned int animation_step;
  char claim_qr[108];
  unsigned int power_overlay;
  bool overlay_dirty;
};

static atomic_bool g_speaking;

void bk7258_display_speaking(bool active)
{
  atomic_store(&g_speaking, active);
}

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
         ret == -ENODEV || ret == -ENXIO || ret == -ENOTDIR ||
         ret == -EIO;
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
      /* A dirty, half-written or not-yet-ready FAT volume is a storage
       * condition, not a fatal service error: report the raw errno and let
       * the worker retry, so repairing or re-seating the card recovers
       * without a reboot.
       */

      int mount_error = bkdisplay_service_errno();
      syslog(LOG_WARNING, "BKDISPLAY VOLUME stage=mount ret=%d\n",
             mount_error);
      ret = -EAGAIN;
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

static void bkdisplay_cache_frames(struct bkdisplay_service_s *service,
                                   struct bkdisplay_pack_s *pack,
                                   uint16_t *base, const char *expression)
{
  static const char *const names[] =
    {NULL, "blink_half", "blink_closed", "look_left", "look_right"};
  unsigned int i;

  /* Only an explicit expression/pack switch reads from disk; the animation
   * holds no SD lease or file handle. At most 5 frames (256000 B) are cached,
   * and an old pack missing optional frames still displays statically.
   */

  for (i = 0; i < 5; i++)
    {
      free(service->frames[i]);
      service->frames[i] = NULL;
      if (i == 0)
        {
          service->frames[i] = base;
          continue;
        }

      if (i >= 3 && strcmp(expression, "neutral") != 0)
        {
          continue;
        }

      service->frames[i] = malloc(BKDISPLAY_CANVAS_PIXELS * sizeof(*base));
      if (service->frames[i] != NULL &&
          bkdisplay_pack_render(pack, names[i], BKDISPLAY_SIDE_UNMAPPED,
                                service->frames[i],
                                BKDISPLAY_CANVAS_PIXELS) < 0)
        {
          free(service->frames[i]);
          service->frames[i] = NULL;
        }
    }

  service->animation_step = 0;
  /* One optional 51,200-byte frame, bounded independently of pack size.
   * Prepare with the pack lease, never in the audio callback. */
  free(service->speaking_frame);
  service->speaking_frame = malloc(BKDISPLAY_CANVAS_PIXELS * sizeof(*base));
  if (service->speaking_frame &&
      bkdisplay_pack_render(pack, "speaking", BKDISPLAY_SIDE_UNMAPPED,
                            service->speaking_frame,
                            BKDISPLAY_CANVAS_PIXELS) < 0)
    {
      free(service->speaking_frame);
      service->speaking_frame = NULL;
    }
  service->speaking_painted = false;
  if (!service->speaking_frame)
    syslog(LOG_WARNING, "BKDISPLAY speaking frame unavailable\n");
}

static unsigned int bkdisplay_animate_locked(struct bkdisplay_service_s *service)
{
  static const uint8_t frames[] = {0, 1, 2, 1, 0, 3, 0, 4, 0};
  static const unsigned int delay_us[] =
    {2600000, 70000, 100000, 70000, 3500000, 650000, 2200000, 650000, 4400000};
  unsigned int step;
  uint16_t *pixels;
  int ret;

  if (strcmp(service->status.expression, "mapping-test") == 0 ||
      service->frames[0] == NULL || service->frames[1] == NULL ||
      service->frames[2] == NULL)
    {
      return BKDISPLAY_RETRY_US;
    }

  step = (service->animation_step + 1) % sizeof(frames);
  service->animation_step = step;
  pixels = service->frames[frames[step]];
  if (pixels == NULL)
    {
      pixels = service->frames[0];
    }

  ret = bkdisplay_framebuffer_write(BKDISPLAY_FB0, pixels);
  if (ret == 0)
    {
      ret = bkdisplay_framebuffer_write(BKDISPLAY_FB1, pixels);
    }

  if (ret < 0)
    {
      bkdisplay_status_error(service, ret);
      syslog(LOG_ERR, "BKDISPLAY ANIMATION FAIL ret=%d\n", ret);
    }
  else
    {
      /* expression keeps the user-selected logical expression and is not
       * overwritten by the transient blink frames.
       */

      service->status.render_sequence++;
    }

  return delay_us[step];
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

  if (!service->devices_ready) return -EAGAIN;
  if (service->claim_qr[0] || service->power_overlay) return -EBUSY;

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
      ret = bkdisplay_store_resolve_open(BKDISPLAY_MOUNTPOINT, &selection,
                                         &pack);
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
      if (ret == 0)
        {
          bkdisplay_cache_frames(service, pack, pixels, expression);
        }
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
      memcpy(service->status.source_sha256, selection.info.source_sha256,
             sizeof(service->status.source_sha256));
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
  if (pixels != service->frames[0])
    {
      free(pixels);
    }
  if (ret < 0)
    {
      bkdisplay_diagnostic_failure(service, stage, ret);
      bkdisplay_status_error(service, ret);
    }

  return ret;
}

/* Product overlays are painted by this same service, never a second display
 * owner. They need neither SD NAND nor an installed eye pack. */
static int bkdisplay_builtin_locked(struct bkdisplay_service_s *service,
                                    bool fallback)
{
  uint16_t *pixels = calloc(BKDISPLAY_CANVAS_PIXELS, sizeof(*pixels));
  if (!pixels) return -ENOMEM;
  int ret = 0;
#ifdef CONFIG_BK7258_PROVISION_NATIVE
  if (service->claim_qr[0] && !service->power_overlay && !fallback)
    {
      uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(BKDISPLAY_QR_VERSION)];
      uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(BKDISPLAY_QR_VERSION)];
      if (!qrcodegen_encodeText(service->claim_qr, temp, qr,
              qrcodegen_Ecc_MEDIUM, BKDISPLAY_QR_VERSION, BKDISPLAY_QR_VERSION,
              qrcodegen_Mask_AUTO, false))
        ret = -E2BIG;
      else
        {
          int modules = qrcodegen_getSize(qr);
          int extent = (modules + 8) * 2;
          int origin = (BKDISPLAY_CANVAS_WIDTH - extent) / 2;
          for (int y = 0; y < extent; y++)
            for (int x = 0; x < extent; x++)
              {
                int mx = x / 2 - 4;
                int my = y / 2 - 4;
                bool dark = mx >= 0 && my >= 0 && mx < modules &&
                            my < modules && qrcodegen_getModule(qr, mx, my);
                pixels[(origin + y) * BKDISPLAY_CANVAS_WIDTH + origin + x] =
                  dark ? 0 : 0xffff;
              }
        }
      mbedtls_platform_zeroize(qr, sizeof(qr));
      mbedtls_platform_zeroize(temp, sizeof(temp));
    }
  else
#endif
    {
      /* Ring/stem = power intent; iris = built-in fallback eyes. */
      int cx = BKDISPLAY_CANVAS_WIDTH / 2;
      int cy = BKDISPLAY_CANVAS_HEIGHT / 2;
      for (int y = 0; y < BKDISPLAY_CANVAS_HEIGHT; y++)
        for (int x = 0; x < BKDISPLAY_CANVAS_WIDTH; x++)
          {
            int dx = x - cx, dy = y - cy;
            int rr = dx * dx + dy * dy;
            bool lit = service->power_overlay ?
                ((rr >= 28 * 28 && rr <= 34 * 34 && (dy > -23 || dx < -13 || dx > 13)) ||
                 (dx >= -3 && dx <= 3 && dy >= -39 && dy <= -7)) :
                (rr < 39 * 39 && rr > 17 * 17);
            if (lit) pixels[y * BKDISPLAY_CANVAS_WIDTH + x] =
                service->power_overlay == 2 ? 0xfd20 : 0x07ff;
          }
    }
  if (!ret) ret = bkdisplay_framebuffer_write(BKDISPLAY_FB0, pixels);
  if (!ret) ret = bkdisplay_framebuffer_write(BKDISPLAY_FB1, pixels);
  free(pixels);
  return ret;
}

int bk7258_display_onboarding(const char *qr)
{
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  if (qr && (strlen(qr) != 107 || strncmp(qr, "SN1:", 4))) return -EINVAL;
#ifndef CONFIG_BK7258_PROVISION_NATIVE
  if (qr) return -ENOTSUP;
#endif
  int ret = nxmutex_lock(&service->lock);
  if (ret) return ret;
  bool ready = bkdisplay_service_node(BKDISPLAY_FB0, false) &&
               bkdisplay_service_node(BKDISPLAY_FB1, false);
  if (qr && (!ready || service->power_overlay)) ret = -EAGAIN;
  else
    {
      memset(service->claim_qr, 0, sizeof(service->claim_qr));
      if (qr) memcpy(service->claim_qr, qr, 107);
      service->overlay_dirty = true;
      service->status.state = BKDISPLAY_SERVICE_WAITING_ASSET;
      if (ready)
        {
          ret = bkdisplay_builtin_locked(service, false);
          if (!ret) service->overlay_dirty = false;
        }
      if (ret && qr) memset(service->claim_qr, 0, sizeof(service->claim_qr));
    }
  nxmutex_unlock(&service->lock);
  return ret;
}

int bk7258_display_power(unsigned int phase)
{
  if (phase > 2) return -EINVAL;
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  int ret = nxmutex_lock(&service->lock);
  if (ret) return ret;
  if (service->power_overlay != phase)
    {
      service->power_overlay = phase;
      service->overlay_dirty = true;
      service->status.state = BKDISPLAY_SERVICE_WAITING_ASSET;
    }
  nxmutex_unlock(&service->lock);
  return 0;
}

static uint64_t bkdisplay_now_ms(void)
{
  struct timespec now;
  return clock_gettime(CLOCK_MONOTONIC, &now) == 0 ?
         (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000 : 0;
}

static int bkdisplay_worker(int argc, char *argv[])
{
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  uint64_t next = 0;
  (void)argc; (void)argv;
  for (;;)
    {
      int ret = nxmutex_lock(&service->lock);
      if (ret < 0) return ret;
      uint64_t now = bkdisplay_now_ms();
      service->devices_ready = bkdisplay_service_node(BKDISPLAY_FB0, false) &&
                               bkdisplay_service_node(BKDISPLAY_FB1, false);
      if (!service->devices_ready)
        service->status.state = BKDISPLAY_SERVICE_WAITING_DEVICES;
      else if (service->claim_qr[0] || service->power_overlay || service->overlay_dirty)
        {
          service->speaking_painted = false;
          if (service->overlay_dirty)
            {
              ret = bkdisplay_builtin_locked(service, false);
              if (!ret) service->overlay_dirty = false;
              else service->status.last_error = ret;
            }
          next = now;
        }
      else if (service->status.state == BKDISPLAY_SERVICE_READY &&
               service->speaking_frame &&
               (atomic_load(&g_speaking) || service->speaking_painted))
        {
          bool active = atomic_load(&g_speaking);
          if (active != service->speaking_painted)
            {
              uint16_t *frame = active ? service->speaking_frame :
                                        service->frames[0];
              ret = frame ? bkdisplay_framebuffer_write(BKDISPLAY_FB0, frame) :
                            -ENODATA;
              if (!ret) ret = bkdisplay_framebuffer_write(BKDISPLAY_FB1, frame);
              if (!ret)
                {
                  service->speaking_painted = active;
                  service->status.render_sequence++;
                  syslog(LOG_INFO, "BKDISPLAY voice speaking=%d rendered=1\n",
                         active);
                }
              else service->status.last_error = ret;
            }
          next = now + 100;
        }
      else if (now >= next)
        {
          if (service->status.state == BKDISPLAY_SERVICE_READY)
            next = now + bkdisplay_animate_locked(service) / 1000;
          else
            {
              service->status.state = BKDISPLAY_SERVICE_WAITING_ASSET;
              ret = bkdisplay_service_node(BKDISPLAY_BLOCKDEV, true) ?
                    bkdisplay_render_locked(service, "neutral") : -ENODEV;
              if (ret)
                {
                  service->status.last_error = ret;
                  /* A missing/corrupt asset is not a ready installed pack.
                   * Keep management/power displays alive with built-in eyes. */
                  (void)bkdisplay_builtin_locked(service, true);
                }
              next = now + (ret ? 2000 : 2600);
            }
        }
      nxmutex_unlock(&service->lock);
      (void)nxsig_usleep(BKDISPLAY_DEVICE_POLL_US);
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

  if (service->status.state != BKDISPLAY_SERVICE_READY)
    {
      service->status.state = BKDISPLAY_SERVICE_WAITING_DEVICES;
    }
  service->status.last_error = 0;
  ret = bk7258_display_rpc_server_initialize();
  if (ret < 0)
    {
      bkdisplay_status_error(service, ret);
    }
  else
    {
      pid = task_create("bkdisplay",
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

int bk7258_display_import(const void *data, size_t size)
{
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  int close_ret;
  int ret = nxmutex_lock(&service->lock);

  if (ret < 0)
    {
      return ret;
    }

  ret = service->devices_ready ? bkdisplay_volume_open(service) : -EAGAIN;
  if (ret == 0)
    {
      ret = bkdisplay_store_import(BKDISPLAY_MOUNTPOINT, data, size, NULL);
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

  nxmutex_unlock(&service->lock);
  /* After a start failure caused by a missing pack, a successful import
   * reuses the same display worker.
   */
  return ret == 0 ? bk7258_display_service_start() : ret;
}

int bk7258_display_reset_selection(void)
{
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  int ret = nxmutex_lock(&service->lock);
  int close_ret;
  if (ret < 0) return ret;
  ret = service->devices_ready ? bkdisplay_volume_open(service) : -EAGAIN;
  if (ret == 0)
    {
      ret = bkdisplay_store_reset_selection(BKDISPLAY_MOUNTPOINT);
      close_ret = bkdisplay_volume_close(service);
      if (ret == 0 && close_ret < 0) ret = close_ret;
    }
  /* 清理成功不依赖可选眼睛包存在；首启认领画面有独立内置路径。
   * 渲染错误继续报告，不能因缺少资源永远阻塞已撤销的事务。 */
  if (ret == 0)
    {
      int render_ret = bkdisplay_render_locked(service, "neutral");
      if (render_ret < 0) bkdisplay_status_error(service, render_ret);
    }
  if (ret < 0) bkdisplay_status_error(service, ret);
  nxmutex_unlock(&service->lock);
  return ret;
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
