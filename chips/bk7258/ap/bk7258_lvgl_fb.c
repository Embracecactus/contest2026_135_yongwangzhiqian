/****************************************************************************
 * board/bk7258/chip/ap/bk7258_lvgl_fb.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 accelerated LVGL framebuffer display.  The AP port deliberately
 * keeps D-cache disabled and PSRAM non-cacheable, so LVGL's generic DIRECT
 * framebuffer mode makes every software blend operate on slow PSRAM.  This
 * adapter keeps the LVGL renderer in a bounded internal-SRAM line buffer and
 * transfers completed dirty rectangles with the official DMA2D engine.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/video/fb.h>

#include <lvgl/lvgl.h>

#include "../include/bk7258_dma2d.h"
#include "../include/bk7258_lvgl_fb.h"

#define BK7258_LVGL_FB_BYTES_PER_PIXEL 2u
#define BK7258_LVGL_FB_TIMEOUT_MS      100u

struct bk7258_lvgl_fb_s
{
  int fd;
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s page[2];
  FAR struct bk7258_dma2d_s *dma2d;
  FAR lv_indev_t *touch;
  lv_indev_read_cb_t touch_read;
  int32_t touch_x;
  int32_t touch_y;
  uint16_t logical_width;
  uint16_t logical_height;
  uint16_t viewport_x;
  uint16_t viewport_y;
  uint16_t viewport_width;
  uint16_t viewport_height;
  uint8_t front;
  bool active;
  volatile int last_error;
};

static struct bk7258_lvgl_fb_s g_bk7258_lvgl_fb =
{
  .fd = -1,
};

static uint16_t g_bk7258_lvgl_draw_buffer
  [CONFIG_BK7258_LVGL_FB_MAX_WIDTH *
   CONFIG_BK7258_LVGL_FB_DRAW_LINES]
  __attribute__((aligned(4)));

/* A dirty logical rectangle is nearest-neighbour scaled in SRAM, then copied
 * to PSRAM by DMA2D.  Processing at most DRAW_LINES output rows per batch
 * bounds memory even when LVGL emits a tall, narrow partial area. */

static uint16_t g_bk7258_lvgl_scale_buffer
  [CONFIG_BK7258_LVGL_FB_MAX_WIDTH *
   CONFIG_BK7258_LVGL_FB_DRAW_LINES]
  __attribute__((aligned(4)));

static uint16_t bk7258_lvgl_fb_ceil_ratio(uint32_t value, uint16_t dst,
                                          uint16_t src)
{
  return (uint16_t)((value * dst + src - 1u) / src);
}

static int bk7258_lvgl_fb_geometry(FAR struct bk7258_lvgl_fb_s *priv)
{
  uint16_t logical_width = CONFIG_BK7258_LVGL_FB_LOGICAL_WIDTH;
  uint16_t logical_height = CONFIG_BK7258_LVGL_FB_LOGICAL_HEIGHT;

  if (logical_width == 0)
    {
      logical_width = priv->vinfo.xres;
    }

  if (logical_height == 0)
    {
      logical_height = priv->vinfo.yres;
    }

  if (logical_width == 0 || logical_height == 0 ||
      logical_width > CONFIG_BK7258_LVGL_FB_MAX_WIDTH ||
      priv->vinfo.xres > CONFIG_BK7258_LVGL_FB_MAX_WIDTH)
    {
      return -EINVAL;
    }

  priv->logical_width = logical_width;
  priv->logical_height = logical_height;

  /* Preserve the application's aspect ratio and never upscale. */

  if (logical_width <= priv->vinfo.xres &&
      logical_height <= priv->vinfo.yres)
    {
      priv->viewport_width = logical_width;
      priv->viewport_height = logical_height;
    }
  else if ((uint32_t)priv->vinfo.xres * logical_height <=
           (uint32_t)priv->vinfo.yres * logical_width)
    {
      priv->viewport_width = priv->vinfo.xres;
      priv->viewport_height =
        (uint16_t)((uint32_t)logical_height * priv->vinfo.xres /
                   logical_width);
    }
  else
    {
      priv->viewport_height = priv->vinfo.yres;
      priv->viewport_width =
        (uint16_t)((uint32_t)logical_width * priv->vinfo.yres /
                   logical_height);
    }

  if (priv->viewport_width == 0 || priv->viewport_height == 0)
    {
      return -EINVAL;
    }

  priv->viewport_x = (priv->vinfo.xres - priv->viewport_width) / 2u;
  priv->viewport_y = (priv->vinfo.yres - priv->viewport_height) / 2u;
  return 0;
}

static int bk7258_lvgl_fb_copy(FAR struct bk7258_lvgl_fb_s *priv,
                               FAR const void *src,
                               uint16_t src_width,
                               uint16_t src_height,
                               uint16_t dst_page,
                               uint16_t dst_x,
                               uint16_t dst_y,
                               uint16_t width,
                               uint16_t height)
{
  struct bk7258_dma2d_copy_s copy;

  memset(&copy, 0, sizeof(copy));
  copy.src = src;
  copy.dst = priv->page[dst_page].fbmem;
  copy.src_frame_width = src_width;
  copy.src_frame_height = src_height;
  copy.dst_frame_width = priv->vinfo.xres;
  copy.dst_frame_height = priv->vinfo.yres;
  copy.dst_x = dst_x;
  copy.dst_y = dst_y;
  copy.width = width;
  copy.height = height;
  copy.src_format = BK7258_DMA2D_RGB565;
  copy.dst_format = BK7258_DMA2D_RGB565;
  copy.src_swap = BK7258_DMA2D_SWAP_REGULAR;
  copy.dst_swap = BK7258_DMA2D_SWAP_REGULAR;
  copy.src_reverse = BK7258_DMA2D_REVERSE_NONE;
  copy.dst_reverse = BK7258_DMA2D_REVERSE_NONE;
  copy.timeout_ms = BK7258_LVGL_FB_TIMEOUT_MS;
  return bk7258_dma2d_copy(priv->dma2d, &copy);
}

static int bk7258_lvgl_fb_mirror(FAR struct bk7258_lvgl_fb_s *priv,
                                 uint8_t src_page, uint8_t dst_page)
{
  return bk7258_lvgl_fb_copy(priv, priv->page[src_page].fbmem,
                             priv->vinfo.xres, priv->vinfo.yres,
                             dst_page, 0, 0,
                             priv->vinfo.xres, priv->vinfo.yres);
}

static int bk7258_lvgl_fb_scale(FAR struct bk7258_lvgl_fb_s *priv,
                                FAR const lv_area_t *area,
                                FAR const uint16_t *pixels,
                                uint8_t dst_page)
{
  FAR uint16_t *stage = g_bk7258_lvgl_scale_buffer;
  uint16_t src_width = (uint16_t)lv_area_get_width(area);
  uint16_t dst_x1;
  uint16_t dst_x2;
  uint16_t dst_y1;
  uint16_t dst_y2;
  uint16_t dst_width;
  uint16_t dst_y;
  int ret = 0;

  dst_x1 = bk7258_lvgl_fb_ceil_ratio((uint32_t)area->x1,
                                     priv->viewport_width,
                                     priv->logical_width);
  dst_x2 = bk7258_lvgl_fb_ceil_ratio((uint32_t)area->x2 + 1u,
                                     priv->viewport_width,
                                     priv->logical_width) - 1u;
  dst_y1 = bk7258_lvgl_fb_ceil_ratio((uint32_t)area->y1,
                                     priv->viewport_height,
                                     priv->logical_height);
  dst_y2 = bk7258_lvgl_fb_ceil_ratio((uint32_t)area->y2 + 1u,
                                     priv->viewport_height,
                                     priv->logical_height) - 1u;

  if (dst_x1 > dst_x2 || dst_y1 > dst_y2)
    {
      return 0;
    }

  dst_width = dst_x2 - dst_x1 + 1u;
  for (dst_y = dst_y1; dst_y <= dst_y2; )
    {
      uint16_t batch_height = dst_y2 - dst_y + 1u;
      uint16_t row;

      if (batch_height > CONFIG_BK7258_LVGL_FB_DRAW_LINES)
        {
          batch_height = CONFIG_BK7258_LVGL_FB_DRAW_LINES;
        }

      for (row = 0; row < batch_height; row++)
        {
          uint16_t physical_y = dst_y + row;
          uint16_t logical_y =
            (uint16_t)((uint32_t)physical_y * priv->logical_height /
                       priv->viewport_height);
          FAR const uint16_t *src_row =
            pixels + (logical_y - area->y1) * src_width;
          FAR uint16_t *dst_row = stage + row * dst_width;
          uint16_t x;

          for (x = 0; x < dst_width; x++)
            {
              uint16_t physical_x = dst_x1 + x;
              uint16_t logical_x =
                (uint16_t)((uint32_t)physical_x * priv->logical_width /
                           priv->viewport_width);

              dst_row[x] = src_row[logical_x - area->x1];
            }
        }

      ret = bk7258_lvgl_fb_copy(priv, stage, dst_width, batch_height,
                                dst_page, priv->viewport_x + dst_x1,
                                priv->viewport_y + dst_y,
                                dst_width, batch_height);
      if (ret < 0)
        {
          break;
        }

      dst_y += batch_height;
    }

  return ret;
}

static int bk7258_lvgl_fb_pan(FAR struct bk7258_lvgl_fb_s *priv,
                              uint8_t page)
{
  struct fb_planeinfo_s pinfo = priv->page[0];

  pinfo.yoffset = page == 0 ? 0 : priv->vinfo.yres;
  if (ioctl(priv->fd, FBIOPAN_DISPLAY,
            (unsigned long)((uintptr_t)&pinfo)) < 0)
    {
      return -errno;
    }

  if (ioctl(priv->fd, FBIO_WAITFORVSYNC, 0) < 0)
    {
      return -errno;
    }

  return 0;
}

static void bk7258_lvgl_fb_flush(FAR lv_display_t *display,
                                 FAR const lv_area_t *area,
                                 FAR uint8_t *pixels)
{
  FAR struct bk7258_lvgl_fb_s *priv =
    lv_display_get_driver_data(display);
  uint16_t width;
  uint16_t height;
  uint8_t back;
  int ret;

  if (priv == NULL || !priv->active || area == NULL || pixels == NULL ||
      area->x1 < 0 || area->y1 < 0 ||
      area->x2 >= priv->logical_width ||
      area->y2 >= priv->logical_height)
    {
      lv_display_flush_ready(display);
      return;
    }

  width = (uint16_t)lv_area_get_width(area);
  height = (uint16_t)lv_area_get_height(area);
  back = priv->front ^ 1u;

  if (priv->logical_width == priv->viewport_width &&
      priv->logical_height == priv->viewport_height)
    {
      ret = bk7258_lvgl_fb_copy(priv, pixels, width, height, back,
                                priv->viewport_x + area->x1,
                                priv->viewport_y + area->y1,
                                width, height);
    }
  else
    {
      ret = bk7258_lvgl_fb_scale(priv, area,
                                 (FAR const uint16_t *)pixels, back);
    }

  if (ret >= 0 && lv_display_flush_is_last(display))
    {
      ret = bk7258_lvgl_fb_pan(priv, back);
      if (ret >= 0)
        {
          priv->front = back;
          ret = bk7258_lvgl_fb_mirror(priv, priv->front,
                                      priv->front ^ 1u);
        }
    }

  if (ret < 0)
    {
      priv->last_error = ret;
    }

  /* A failed hardware operation must never strand LVGL's render pipeline.
   * DMA2D itself remains fail-closed after timeout/error, while the display
   * can still be deleted cleanly by the caller. */

  lv_display_flush_ready(display);
}

static void bk7258_lvgl_fb_release(FAR lv_event_t *event)
{
  FAR lv_display_t *display = lv_event_get_user_data(event);
  FAR struct bk7258_lvgl_fb_s *priv =
    lv_display_get_driver_data(display);

  if (priv == NULL)
    {
      return;
    }

  lv_display_set_driver_data(display, NULL);
  lv_display_set_flush_cb(display, NULL);
  priv->active = false;

  if (priv->touch != NULL && priv->touch_read != NULL)
    {
      lv_indev_set_read_cb(priv->touch, priv->touch_read);
      priv->touch = NULL;
      priv->touch_read = NULL;
    }

  if (priv->dma2d != NULL)
    {
      (void)bk7258_dma2d_uninitialize(priv->dma2d);
      priv->dma2d = NULL;
    }

  if (priv->fd >= 0)
    {
      close(priv->fd);
      priv->fd = -1;
    }
}

FAR lv_display_t *bk7258_lvgl_fb_create(FAR const char *path)
{
  FAR struct bk7258_lvgl_fb_s *priv = &g_bk7258_lvgl_fb;
  FAR lv_display_t *display = NULL;
  int ret;

  if (path == NULL || priv->active)
    {
      return NULL;
    }

  memset(priv, 0, sizeof(*priv));
  priv->fd = -1;
  priv->fd = open(path, O_RDWR | O_CLOEXEC);
  if (priv->fd < 0)
    {
      goto errout;
    }

  if (ioctl(priv->fd, FBIOGET_VIDEOINFO,
            (unsigned long)((uintptr_t)&priv->vinfo)) < 0 ||
      priv->vinfo.fmt != FB_FMT_RGB16_565 ||
      priv->vinfo.xres > CONFIG_BK7258_LVGL_FB_MAX_WIDTH)
    {
      goto errout;
    }

  memset(&priv->page[0], 0, sizeof(priv->page[0]));
  if (ioctl(priv->fd, FBIOGET_PLANEINFO,
            (unsigned long)((uintptr_t)&priv->page[0])) < 0 ||
      priv->page[0].bpp != 16 ||
      priv->page[0].fbmem == NULL ||
      priv->page[0].yres_virtual != priv->vinfo.yres * 2u)
    {
      goto errout;
    }

  memset(&priv->page[1], 0, sizeof(priv->page[1]));
  priv->page[1].display = 1;
  if (ioctl(priv->fd, FBIOGET_PLANEINFO,
            (unsigned long)((uintptr_t)&priv->page[1])) < 0 ||
      priv->page[1].bpp != 16 || priv->page[1].fbmem == NULL)
    {
      goto errout;
    }

  ret = bk7258_lvgl_fb_geometry(priv);
  if (ret < 0)
    {
      goto errout;
    }

  ret = bk7258_dma2d_initialize(&priv->dma2d);
  if (ret < 0)
    {
      goto errout;
    }

  /* Keep letterbox pixels deterministic on both pages before the first
   * partial frame is presented. */

  memset(priv->page[0].fbmem, 0,
         (size_t)priv->vinfo.xres * priv->vinfo.yres *
         BK7258_LVGL_FB_BYTES_PER_PIXEL);
  memset(priv->page[1].fbmem, 0,
         (size_t)priv->vinfo.xres * priv->vinfo.yres *
         BK7258_LVGL_FB_BYTES_PER_PIXEL);

  display = lv_display_create(priv->logical_width, priv->logical_height);
  if (display == NULL)
    {
      goto errout;
    }

  priv->front = 0;
  priv->active = true;
  lv_display_set_driver_data(display, priv);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, g_bk7258_lvgl_draw_buffer, NULL,
                         sizeof(g_bk7258_lvgl_draw_buffer),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, bk7258_lvgl_fb_flush);
  lv_display_add_event_cb(display, bk7258_lvgl_fb_release,
                          LV_EVENT_DELETE, display);
  return display;

errout:
  if (display != NULL)
    {
      lv_display_delete(display);
    }
  else
    {
      if (priv->dma2d != NULL)
        {
          (void)bk7258_dma2d_uninitialize(priv->dma2d);
          priv->dma2d = NULL;
        }

      if (priv->fd >= 0)
        {
          close(priv->fd);
          priv->fd = -1;
        }
    }

  priv->active = false;
  return NULL;
}

static void bk7258_lvgl_fb_touch_read(FAR lv_indev_t *indev,
                                      FAR lv_indev_data_t *data)
{
  FAR struct bk7258_lvgl_fb_s *priv = &g_bk7258_lvgl_fb;
  int32_t x;
  int32_t y;

  if (priv->touch != indev || priv->touch_read == NULL)
    {
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }

  /* The NuttX driver leaves the coordinate unchanged when no new sample is
   * available.  Seed it with the last physical point so a logical point from
   * LVGL's previous read is never scaled a second time. */

  data->point.x = priv->touch_x;
  data->point.y = priv->touch_y;
  priv->touch_read(indev, data);

  if (data->state != LV_INDEV_STATE_PRESSED)
    {
      return;
    }

  priv->touch_x = data->point.x;
  priv->touch_y = data->point.y;
  x = data->point.x - priv->viewport_x;
  y = data->point.y - priv->viewport_y;

  if (x < 0 || y < 0 || x >= priv->viewport_width ||
      y >= priv->viewport_height)
    {
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }

  data->point.x = (int32_t)((uint32_t)x * priv->logical_width /
                            priv->viewport_width);
  data->point.y = (int32_t)((uint32_t)y * priv->logical_height /
                            priv->viewport_height);
}

int bk7258_lvgl_fb_bind_touch(FAR lv_display_t *display,
                              FAR lv_indev_t *indev)
{
  FAR struct bk7258_lvgl_fb_s *priv = &g_bk7258_lvgl_fb;
  lv_indev_read_cb_t read_cb;

  if (display == NULL || indev == NULL || !priv->active ||
      lv_display_get_driver_data(display) != priv || priv->touch != NULL ||
      lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER)
    {
      return -EINVAL;
    }

  read_cb = lv_indev_get_read_cb(indev);
  if (read_cb == NULL)
    {
      return -ENODEV;
    }

  priv->touch = indev;
  priv->touch_read = read_cb;
  priv->touch_x = 0;
  priv->touch_y = 0;
  lv_indev_set_display(indev, display);
  lv_indev_set_read_cb(indev, bk7258_lvgl_fb_touch_read);
  return 0;
}
