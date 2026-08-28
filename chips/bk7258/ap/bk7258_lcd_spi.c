/****************************************************************************
 * chips/bk7258/ap/bk7258_lcd_spi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SPI display controller to NuttX framebuffer wrapper.
 *
 * Panel commands and physical pin assignments are intentionally outside
 * this file.  The selected board binds an SDK panel descriptor and the RESET
 * / DC control pins; this chip layer owns only the SPI-LCD controller,
 * framebuffer storage and the standard /dev/fb0 interface.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_LCD_SPI

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/video/fb.h>

#include <arch/chip/bk7258_lcd_spi.h>

#include <driver/lcd_spi.h>
#include <driver/lcd_types.h>

/* The RGB565 framebuffer is exposed to NuttX users directly.  The SDK SPI
 * driver sends the panel a byte stream whose format is fixed by the SDK
 * CONFIG_LCD_SPI_COLOR_DEPTH_BYTE setting; this wrapper only supports the
 * two-byte RGB565 contract that the existing display path already uses.
 */

#define BK7258_LCD_SPI_BYTES_PER_PIXEL 2u

struct bk7258_lcd_spi_priv_s
{
  struct fb_vtable_s vtable;
  mutex_t lock;
  const struct bk7258_lcd_spi_board_s *board;
  uint8_t *framebuf_alloc;
  uint8_t *framebuf;
  size_t framebuf_bytes;
  uint16_t power;
  bool inited;
};

static int bk7258_lcd_spi_getvideoinfo(FAR struct fb_vtable_s *vtable,
                                       FAR struct fb_videoinfo_s *vinfo);
static int bk7258_lcd_spi_getplaneinfo(FAR struct fb_vtable_s *vtable,
                                       int planeno,
                                       FAR struct fb_planeinfo_s *pinfo);
#ifdef CONFIG_FB_UPDATE
static int bk7258_lcd_spi_updatearea(FAR struct fb_vtable_s *vtable,
                                     FAR const struct fb_area_s *area);
#endif
static int bk7258_lcd_spi_getpower(FAR struct fb_vtable_s *vtable);
static int bk7258_lcd_spi_setpower(FAR struct fb_vtable_s *vtable,
                                   int power);
static int bk7258_lcd_spi_ioctl(FAR struct fb_vtable_s *vtable, int cmd,
                                unsigned long arg);

static struct bk7258_lcd_spi_priv_s g_bk7258_lcd_spi =
{
  .vtable =
  {
    .getvideoinfo = bk7258_lcd_spi_getvideoinfo,
    .getplaneinfo = bk7258_lcd_spi_getplaneinfo,
#ifdef CONFIG_FB_UPDATE
    .updatearea   = bk7258_lcd_spi_updatearea,
#endif
    .getpower     = bk7258_lcd_spi_getpower,
    .setpower     = bk7258_lcd_spi_setpower,
    .ioctl        = bk7258_lcd_spi_ioctl,
  },
  .lock           = NXMUTEX_INITIALIZER,
  .board          = NULL,
  .framebuf_alloc = NULL,
  .framebuf       = NULL,
  .framebuf_bytes = 0,
  .power          = 0,
  .inited         = false,
};

static int bk7258_lcd_spi_getvideoinfo(FAR struct fb_vtable_s *vtable,
                                       FAR struct fb_videoinfo_s *vinfo)
{
  FAR struct bk7258_lcd_spi_priv_s *priv =
    (FAR struct bk7258_lcd_spi_priv_s *)vtable;

  if (vinfo == NULL || priv->board == NULL)
    {
      return -EINVAL;
    }

  vinfo->fmt     = FB_FMT_RGB16_565;
  vinfo->xres    = priv->board->width;
  vinfo->yres    = priv->board->height;
  vinfo->nplanes = 1;
  return OK;
}

static int bk7258_lcd_spi_getplaneinfo(FAR struct fb_vtable_s *vtable,
                                       int planeno,
                                       FAR struct fb_planeinfo_s *pinfo)
{
  FAR struct bk7258_lcd_spi_priv_s *priv =
    (FAR struct bk7258_lcd_spi_priv_s *)vtable;

  if (planeno != 0 || pinfo == NULL || priv->framebuf == NULL)
    {
      return -EINVAL;
    }

  pinfo->fbmem  = priv->framebuf;
  pinfo->fblen  = priv->framebuf_bytes;
  pinfo->stride = priv->board->width * BK7258_LCD_SPI_BYTES_PER_PIXEL;
  pinfo->display = 0;
  pinfo->bpp    = 16;
  return OK;
}

#ifdef CONFIG_FB_UPDATE
static int bk7258_lcd_spi_updatearea(FAR struct fb_vtable_s *vtable,
                                     FAR const struct fb_area_s *area)
{
  FAR struct bk7258_lcd_spi_priv_s *priv =
    (FAR struct bk7258_lcd_spi_priv_s *)vtable;
  lcd_display_area_t sdk_area;
  FAR uint8_t *data;
  uint16_t width;
  size_t offset;
  bk_err_t sdkret;
  int ret;

  if (area == NULL || priv->board == NULL || priv->framebuf == NULL)
    {
      return -EINVAL;
    }

  if (area->xmin > area->xmax || area->ymin > area->ymax ||
      area->xmax >= priv->board->width ||
      area->ymax >= priv->board->height)
    {
      return -EINVAL;
    }

  width = priv->board->width;
  sdk_area.x_start = area->xmin;
  sdk_area.y_start = area->ymin;
  sdk_area.x_end   = area->xmax;
  sdk_area.y_end   = area->ymax;

  offset = ((size_t)area->ymin * width + area->xmin) *
           BK7258_LCD_SPI_BYTES_PER_PIXEL;
  data = priv->framebuf + offset;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  sdkret = bk_lcd_spi_partial_display(priv->board->spi_id, &sdk_area, data);
  nxmutex_unlock(&priv->lock);

  return sdkret == BK_OK ? OK : -EIO;
}
#endif /* CONFIG_FB_UPDATE */

static int bk7258_lcd_spi_getpower(FAR struct fb_vtable_s *vtable)
{
  FAR struct bk7258_lcd_spi_priv_s *priv =
    (FAR struct bk7258_lcd_spi_priv_s *)vtable;

  return priv->power ? 1 : 0;
}

static int bk7258_lcd_spi_setpower(FAR struct fb_vtable_s *vtable,
                                   int power)
{
  FAR struct bk7258_lcd_spi_priv_s *priv =
    (FAR struct bk7258_lcd_spi_priv_s *)vtable;
  bk_err_t sdkret;
  int ret;

  if (priv->board == NULL || priv->framebuf == NULL)
    {
      return -EIO;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (power > 0 && !priv->power)
    {
      sdkret = bk_lcd_spi_frame_display(priv->board->spi_id,
                                        priv->framebuf,
                                        priv->framebuf_bytes);
      if (sdkret != BK_OK)
        {
          nxmutex_unlock(&priv->lock);
          return -EIO;
        }

      priv->power = 1;
    }
  else if (power <= 0 && priv->power)
    {
      priv->power = 0;
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}

static int bk7258_lcd_spi_ioctl(FAR struct fb_vtable_s *vtable, int cmd,
                                unsigned long arg)
{
  (void)vtable;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

int bk7258_lcd_spi_initialize(
  FAR const struct bk7258_lcd_spi_board_s *board)
{
  FAR struct bk7258_lcd_spi_priv_s *priv = &g_bk7258_lcd_spi;
  FAR const lcd_device_t *device;
  size_t framebuf_bytes;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->inited)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  if (board == NULL || board->name == NULL || board->sdk_device == NULL ||
      board->width == 0 || board->height == 0)
    {
      nxmutex_unlock(&priv->lock);
      return -EINVAL;
    }

  if (board->spi_id > 1)
    {
      syslog(LOG_ERR, "BK7258 LCD SPI: invalid controller %u\n",
             board->spi_id);
      nxmutex_unlock(&priv->lock);
      return -EINVAL;
    }

  device = (FAR const lcd_device_t *)board->sdk_device;
  framebuf_bytes = (size_t)board->width * board->height *
                   BK7258_LCD_SPI_BYTES_PER_PIXEL;

  priv->framebuf_alloc = kmm_zalloc(framebuf_bytes + 15u);
  if (priv->framebuf_alloc == NULL)
    {
      syslog(LOG_ERR, "BK7258 LCD SPI: framebuffer allocation failed\n");
      nxmutex_unlock(&priv->lock);
      return -ENOMEM;
    }

  priv->framebuf = (FAR uint8_t *)
    (((uintptr_t)priv->framebuf_alloc + 15u) & ~(uintptr_t)15u);
  priv->framebuf_bytes = framebuf_bytes;
  priv->board = board;

  if (board->control_pins_initialize != NULL)
    {
      ret = board->control_pins_initialize(board);
      if (ret < 0)
        {
          goto errout_with_framebuffer;
        }
    }

  bk_lcd_spi_init(board->spi_id, device, board->reset_gpio, board->dc_gpio);

  ret = fb_register_device(0, 0, &priv->vtable);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 LCD SPI: fb_register failed: %d\n", ret);
      goto errout_with_framebuffer;
    }

  priv->inited = true;
  syslog(LOG_INFO,
         "BK7258 LCD SPI: ready board=%s panel=%ux%u RGB565 spi=%u fb=%p\n",
         board->name, board->width, board->height, board->spi_id,
         priv->framebuf);

  nxmutex_unlock(&priv->lock);
  return OK;

errout_with_framebuffer:
  kmm_free(priv->framebuf_alloc);
  priv->framebuf_alloc = NULL;
  priv->framebuf = NULL;
  priv->framebuf_bytes = 0;
  priv->board = NULL;
  nxmutex_unlock(&priv->lock);
  return ret;
}

#endif /* CONFIG_BK7258_LCD_SPI */
