/****************************************************************************
 * board/bk7258/chip/ap/bk7258_lcd_8080.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 8080-MCU LCD controller to NuttX framebuffer wrapper.
 *
 * The selected board binds an SDK panel descriptor; this chip layer owns
 * the RGB565 framebuffer, dirty-area tracking, the 8080 RAMWR command path
 * and /dev/fb0 registration.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_LCD_8080

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/video/fb.h>

#include <arch/chip/bk7258_lcd_8080.h>
#include <arch/chip/bk7258_sdk_abi.h>

#include <driver/lcd.h>
#include <driver/lcd_types.h>

#define BK7258_LCD_8080_BYTES_PER_PIXEL 2u
#define BK7258_LCD_8080_CMD_CASET       0x2au
#define BK7258_LCD_8080_CMD_RASET       0x2bu
#define BK7258_LCD_8080_CMD_RAMWR       0x2cu

struct bk7258_lcd_8080_priv_s
{
  struct fb_vtable_s vtable;
  mutex_t lock;
  const struct bk7258_lcd_8080_board_s *board;
  uint8_t *framebuf_alloc;
  uint8_t *framebuf;
  size_t framebuf_bytes;
  uint16_t power;
  bool inited;
};

static int bk7258_lcd_8080_getvideoinfo(FAR struct fb_vtable_s *vtable,
                                        FAR struct fb_videoinfo_s *vinfo);
static int bk7258_lcd_8080_getplaneinfo(FAR struct fb_vtable_s *vtable,
                                        int planeno,
                                        FAR struct fb_planeinfo_s *pinfo);
#ifdef CONFIG_FB_UPDATE
static int bk7258_lcd_8080_updatearea(FAR struct fb_vtable_s *vtable,
                                      FAR const struct fb_area_s *area);
#endif
static int bk7258_lcd_8080_getpower(FAR struct fb_vtable_s *vtable);
static int bk7258_lcd_8080_setpower(FAR struct fb_vtable_s *vtable,
                                    int power);
static int bk7258_lcd_8080_ioctl(FAR struct fb_vtable_s *vtable, int cmd,
                                 unsigned long arg);

static struct bk7258_lcd_8080_priv_s g_bk7258_lcd_8080 =
{
  .vtable =
  {
    .getvideoinfo = bk7258_lcd_8080_getvideoinfo,
    .getplaneinfo = bk7258_lcd_8080_getplaneinfo,
#ifdef CONFIG_FB_UPDATE
    .updatearea   = bk7258_lcd_8080_updatearea,
#endif
    .getpower     = bk7258_lcd_8080_getpower,
    .setpower     = bk7258_lcd_8080_setpower,
    .ioctl        = bk7258_lcd_8080_ioctl,
  },
  .lock           = NXMUTEX_INITIALIZER,
  .board          = NULL,
  .framebuf_alloc = NULL,
  .framebuf       = NULL,
  .framebuf_bytes = 0,
  .power          = 0,
  .inited         = false,
};

static int bk7258_lcd_8080_getvideoinfo(FAR struct fb_vtable_s *vtable,
                                        FAR struct fb_videoinfo_s *vinfo)
{
  FAR struct bk7258_lcd_8080_priv_s *priv =
    (FAR struct bk7258_lcd_8080_priv_s *)vtable;

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

static int bk7258_lcd_8080_getplaneinfo(FAR struct fb_vtable_s *vtable,
                                        int planeno,
                                        FAR struct fb_planeinfo_s *pinfo)
{
  FAR struct bk7258_lcd_8080_priv_s *priv =
    (FAR struct bk7258_lcd_8080_priv_s *)vtable;

  if (planeno != 0 || pinfo == NULL || priv->framebuf == NULL)
    {
      return -EINVAL;
    }

  pinfo->fbmem   = priv->framebuf;
  pinfo->fblen   = priv->framebuf_bytes;
  pinfo->stride  = priv->board->width * BK7258_LCD_8080_BYTES_PER_PIXEL;
  pinfo->display = 0;
  pinfo->bpp     = 16;
  return OK;
}

static void bk7258_lcd_8080_set_window(
  FAR const struct bk7258_lcd_8080_priv_s *priv,
  FAR const struct fb_area_s *area)
{
  uint32_t column[4];
  uint32_t row[4];

  column[0] = area->x;
  column[1] = area->x + area->w - 1;
  row[0]    = area->y;
  row[1]    = area->y + area->h - 1;

  (void)bk_lcd_8080_send_cmd(BK7258_LCD_8080_CMD_CASET, column, 2);
  (void)bk_lcd_8080_send_cmd(BK7258_LCD_8080_CMD_RASET, row, 2);
  (void)priv;
}

static void bk7258_lcd_8080_push_area(
  FAR const struct bk7258_lcd_8080_priv_s *priv,
  FAR const struct fb_area_s *area)
{
  FAR uint16_t *pixels;
  size_t offset;
  uint32_t count;

  offset = ((size_t)area->y * priv->board->width + area->x) *
           BK7258_LCD_8080_BYTES_PER_PIXEL;
  pixels = (FAR uint16_t *)(priv->framebuf + offset);
  count  = (uint32_t)area->w * (uint32_t)area->h;

  bk7258_lcd_8080_set_window(priv, area);
  lcd_hal_8080_data_send(BK7258_LCD_8080_CMD_RAMWR, pixels, count);
}

#ifdef CONFIG_FB_UPDATE
static int bk7258_lcd_8080_updatearea(FAR struct fb_vtable_s *vtable,
                                      FAR const struct fb_area_s *area)
{
  FAR struct bk7258_lcd_8080_priv_s *priv =
    (FAR struct bk7258_lcd_8080_priv_s *)vtable;
  int ret;

  if (area == NULL || priv->board == NULL || priv->framebuf == NULL)
    {
      return -EINVAL;
    }

  if (area->w == 0 || area->h == 0 ||
      area->x + area->w > priv->board->width ||
      area->y + area->h > priv->board->height)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  bk7258_lcd_8080_push_area(priv, area);
  nxmutex_unlock(&priv->lock);
  return OK;
}
#endif

static int bk7258_lcd_8080_getpower(FAR struct fb_vtable_s *vtable)
{
  FAR struct bk7258_lcd_8080_priv_s *priv =
    (FAR struct bk7258_lcd_8080_priv_s *)vtable;

  return priv->power ? 1 : 0;
}

static int bk7258_lcd_8080_setpower(FAR struct fb_vtable_s *vtable,
                                    int power)
{
  FAR struct bk7258_lcd_8080_priv_s *priv =
    (FAR struct bk7258_lcd_8080_priv_s *)vtable;
  struct fb_area_s area;
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
      area.x = 0;
      area.y = 0;
      area.w = priv->board->width;
      area.h = priv->board->height;
      bk7258_lcd_8080_push_area(priv, &area);
      priv->power = 1;
    }
  else if (power <= 0 && priv->power)
    {
      priv->power = 0;
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}

static int bk7258_lcd_8080_ioctl(FAR struct fb_vtable_s *vtable, int cmd,
                                 unsigned long arg)
{
  (void)vtable;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

int bk7258_lcd_8080_initialize(void)
{
  FAR struct bk7258_lcd_8080_priv_s *priv = &g_bk7258_lcd_8080;
  FAR const struct bk7258_lcd_8080_board_s *board;
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

  board = bk7258_board_lcd_8080_config();
  if (board == NULL || board->name == NULL || board->sdk_device == NULL ||
      board->width == 0 || board->height == 0)
    {
      nxmutex_unlock(&priv->lock);
      return -EINVAL;
    }

  device = (FAR const lcd_device_t *)board->sdk_device;
  framebuf_bytes = (size_t)board->width * board->height *
                   BK7258_LCD_8080_BYTES_PER_PIXEL;

  priv->framebuf_alloc = kmm_zalloc(framebuf_bytes + 15u);
  if (priv->framebuf_alloc == NULL)
    {
      syslog(LOG_ERR, "BK7258 LCD 8080: framebuffer allocation failed\n");
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

  ret = bk_lcd_8080_init(device);
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "BK7258 LCD 8080: controller init failed: %d\n", ret);
      ret = -EIO;
      goto errout_with_framebuffer;
    }

  (void)bk_lcd_pixel_config(board->width, board->height);

  ret = fb_register_device(0, 0, &priv->vtable);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 LCD 8080: fb_register failed: %d\n", ret);
      goto errout_with_framebuffer;
    }

  priv->inited = true;
  syslog(LOG_INFO,
         "BK7258 LCD 8080: ready board=%s panel=%ux%u RGB565 fb=%p\n",
         board->name, board->width, board->height, priv->framebuf);

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

#endif /* CONFIG_BK7258_LCD_8080 */
