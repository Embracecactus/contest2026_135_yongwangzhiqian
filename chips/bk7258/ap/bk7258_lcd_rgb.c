/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/ap/bk7258_lcd_rgb.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 RGB controller to NuttX framebuffer wrapper.
 *
 * Panel commands and physical pin assignments are intentionally outside this
 * file.  The selected board binds a panel, RGB timing and pin callbacks; this
 * chip layer owns only the controller, AP IRQ route, PSRAM scanout buffer and
 * standard /dev/fb0 interface.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_LCD

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/video/fb.h>

#include <arch/chip/bk7258_lcd.h>
#include <arch/chip/bk7258_psram.h>

#include <driver/lcd.h>

extern int32_t sys_drv_core_intr_group1_disable(uint32_t core_id,
                                                uint32_t param);
extern int32_t sys_drv_core_intr_group1_enable(uint32_t core_id,
                                               uint32_t param);

/* AP logical CPU0 is physical CPU1.  The v3.1.1.9 display service routes its
 * interrupt to physical CPU2 for the SDK application, so the wrapper moves
 * that route to the NuttX AP primary after lcd_driver_init().
 */

#define BK7258_LCD_AP_PRIMARY_CORE_ID     1u
#define BK7258_LCD_SDK_DEFAULT_CORE_ID    2u
#define BK7258_LCD_INTERRUPT_CTRL_BIT     (1u << 27)
#ifdef CONFIG_FB_SYNC
#  define BK7258_LCD_FRAME_COUNT          2u
#else
#  define BK7258_LCD_FRAME_COUNT          1u
#endif

/* The SDK validates the documented six-bit sync-width fields as though they
 * were only three bits wide.  Restore the board profile's values after its
 * controller setup without modifying the immutable SDK source.
 */

#define BK7258_LCD_RGB_SYNC_LOW_REG       0x48060034u
#define BK7258_LCD_DISPLAY_INT_REG         0x48060010u
#define BK7258_LCD_RGB_CONFIG_REG          0x48060014u
#define BK7258_LCD_DISPLAY_STATUS_REG      0x48060030u
#define BK7258_LCD_DISPLAY_BASE_REG        0x48060044u
#define BK7258_LCD_HSYNC_WIDTH_MASK       0x3fu
#define BK7258_LCD_VSYNC_WIDTH_MASK       (0x3fu << 8)

struct bk7258_lcd_priv_s
{
  struct fb_vtable_s vtable;
  mutex_t lock;
  const struct bk7258_lcd_board_s *board;
  uint8_t *framebuf_alloc;
  uint8_t *framebuf;
  size_t framebuf_bytes;
#ifdef CONFIG_FB_SYNC
  sem_t flip_sem;
#endif
  uint16_t power;
  bool inited;
};

static int bk7258_lcd_getvideoinfo(FAR struct fb_vtable_s *vtable,
                                   FAR struct fb_videoinfo_s *vinfo);
static int bk7258_lcd_getplaneinfo(FAR struct fb_vtable_s *vtable,
                                   int planeno,
                                   FAR struct fb_planeinfo_s *pinfo);
#ifdef CONFIG_FB_SYNC
static int bk7258_lcd_waitforvsync(FAR struct fb_vtable_s *vtable);
#endif
#ifdef CONFIG_FB_UPDATE
static int bk7258_lcd_updatearea(FAR struct fb_vtable_s *vtable,
                                 FAR const struct fb_area_s *area);
#endif
static int bk7258_lcd_getpower(FAR struct fb_vtable_s *vtable);
static int bk7258_lcd_setpower(FAR struct fb_vtable_s *vtable, int power);
static int bk7258_lcd_ioctl(FAR struct fb_vtable_s *vtable, int cmd,
                            unsigned long arg);

static struct bk7258_lcd_priv_s g_bk7258_lcd =
{
  .vtable =
  {
    .getvideoinfo = bk7258_lcd_getvideoinfo,
    .getplaneinfo = bk7258_lcd_getplaneinfo,
#ifdef CONFIG_FB_SYNC
    .waitforvsync = bk7258_lcd_waitforvsync,
#endif
#ifdef CONFIG_FB_UPDATE
    .updatearea   = bk7258_lcd_updatearea,
#endif
    .getpower     = bk7258_lcd_getpower,
    .setpower     = bk7258_lcd_setpower,
    .ioctl        = bk7258_lcd_ioctl,
  },
  .lock           = NXMUTEX_INITIALIZER,
  .board          = NULL,
  .framebuf_alloc = NULL,
  .framebuf       = NULL,
  .framebuf_bytes = 0,
#ifdef CONFIG_FB_SYNC
  .flip_sem       = SEM_INITIALIZER(0),
#endif
  .power          = 0,
  .inited         = false,
};

static void *bk7258_lcd_framebuffer_alloc(size_t size)
{
#ifdef CONFIG_BK7258_LCD_FRAMEBUFFER_MEDIA
  void *memory;

  memory = bk7258_psram_media_malloc(BK7258_PSRAM_MEDIA_YUV, size);
  if (memory != NULL)
    {
      memset(memory, 0, size);
    }

  return memory;
#else
  return bk7258_psram_zalloc(size);
#endif
}

static void bk7258_lcd_framebuffer_free(void *memory)
{
#ifdef CONFIG_BK7258_LCD_FRAMEBUFFER_MEDIA
  bk7258_psram_media_free(memory);
#else
  bk7258_psram_free(memory);
#endif
}


#ifdef CONFIG_BK7258_LCD_VALIDATION_PATTERN
static volatile uint32_t g_bk7258_lcd_eof_count;
#endif

static void bk7258_lcd_eof_isr(void *arg)
{
  FAR struct bk7258_lcd_priv_s *priv = arg;
  union fb_paninfo_u info;
  bool flipped = false;

  if (priv != NULL &&
      fb_peek_paninfo(&priv->vtable, &info, FB_NO_OVERLAY) == OK)
    {
      uint32_t height = priv->board->panel->height;
      uint32_t yoffset = info.planeinfo.yoffset;

      if (info.planeinfo.xoffset == 0 &&
          (yoffset == 0 || yoffset == height))
        {
          uintptr_t address = (uintptr_t)priv->framebuf +
                              (size_t)yoffset *
                              priv->board->panel->width * 2u;

          __asm volatile ("dmb sy" ::: "memory");
          (void)lcd_driver_set_display_base_addr((uint32_t)address);
        }

      if (fb_remove_paninfo(&priv->vtable, FB_NO_OVERLAY) == OK)
        {
          flipped = true;
        }
    }

#ifdef CONFIG_FB_SYNC
  if (flipped)
    {
      (void)nxsem_post(&priv->flip_sem);
    }
#else
  (void)flipped;
#endif

  if (priv != NULL)
    {
      fb_notify_vsync(&priv->vtable);
    }

#ifdef CONFIG_BK7258_LCD_VALIDATION_PATTERN
  g_bk7258_lcd_eof_count++;
#endif
}

static int bk7258_lcd_route_irq_to_ap_primary(void)
{
  int32_t ret;

  /* disable() returns the complete pre-update enable register rather than an
   * errno-style status, while enable() has zero-on-success semantics.
   */

  (void)sys_drv_core_intr_group1_disable(BK7258_LCD_SDK_DEFAULT_CORE_ID,
                                         BK7258_LCD_INTERRUPT_CTRL_BIT);
  ret = sys_drv_core_intr_group1_enable(BK7258_LCD_AP_PRIMARY_CORE_ID,
                                        BK7258_LCD_INTERRUPT_CTRL_BIT);
  return ret == 0 ? OK : -EIO;
}

static void bk7258_lcd_unroute_ap_primary_irq(void)
{
  (void)sys_drv_core_intr_group1_disable(BK7258_LCD_AP_PRIMARY_CORE_ID,
                                         BK7258_LCD_INTERRUPT_CTRL_BIT);
}

static int bk7258_lcd_sdk_clock(uint8_t mhz, FAR lcd_clk_t *clock)
{
  if (clock == NULL)
    {
      return -EINVAL;
    }

  switch (mhz)
    {
      case 15:
        *clock = LCD_15M;
        return OK;
      case 30:
        *clock = LCD_30M;
        return OK;
      default:
        return -ENOTSUP;
    }
}

static void bk7258_lcd_restore_sync_widths(
  const struct bk7258_lcd_rgb_timing_s *timing)
{
  volatile uint32_t *reg =
    (volatile uint32_t *)BK7258_LCD_RGB_SYNC_LOW_REG;
  uint32_t value = *reg;

  value &= ~(BK7258_LCD_HSYNC_WIDTH_MASK |
             BK7258_LCD_VSYNC_WIDTH_MASK);
  value |= (uint32_t)timing->hsync_pulse_width |
           ((uint32_t)timing->vsync_pulse_width << 8);
  *reg = value;
  __asm volatile ("dmb sy" ::: "memory");
}

#ifdef CONFIG_BK7258_LCD_VALIDATION_PATTERN
static void bk7258_lcd_fill_validation_pattern(
  FAR struct bk7258_lcd_priv_s *priv)
{
  static const uint16_t colors[] =
  {
    0xf800,
    0x07e0,
    0x001f,
    0xffff,
  };
  FAR uint16_t *pixels = (FAR uint16_t *)priv->framebuf;
  unsigned int width = priv->board->panel->width;
  unsigned int height = priv->board->panel->height;
  unsigned int x;
  unsigned int y;

  for (y = 0; y < height; y++)
    {
      for (x = 0; x < width; x++)
        {
          pixels[y * width + x] = colors[(x * 4u) / width];
        }
    }

  __asm volatile ("dmb sy" ::: "memory");
}

static void bk7258_lcd_log_validation_state(
  FAR struct bk7258_lcd_priv_s *priv)
{
  FAR uint16_t *pixels = (FAR uint16_t *)priv->framebuf;
  unsigned int width = priv->board->panel->width;
  uint32_t ver_count0;
  uint32_t ver_count1;

  ver_count0 = bk_lcd_rgb_ver_cnt_get();
  up_mdelay(20);
  ver_count1 = bk_lcd_rgb_ver_cnt_get();

  syslog(LOG_INFO,
         "BK7258 LCD: validation scan=%lu->%lu eof=%lu "
         "int=%08lx cfg=%08lx status=%08lx base=%08lx "
         "pixels=%04x/%04x/%04x/%04x\n",
         (unsigned long)ver_count0, (unsigned long)ver_count1,
         (unsigned long)g_bk7258_lcd_eof_count,
         (unsigned long)*(volatile uint32_t *)BK7258_LCD_DISPLAY_INT_REG,
         (unsigned long)*(volatile uint32_t *)BK7258_LCD_RGB_CONFIG_REG,
         (unsigned long)*(volatile uint32_t *)BK7258_LCD_DISPLAY_STATUS_REG,
         (unsigned long)*(volatile uint32_t *)BK7258_LCD_DISPLAY_BASE_REG,
         pixels[0], pixels[width / 4u], pixels[width / 2u],
         pixels[(width * 3u) / 4u]);
}
#endif

static int bk7258_lcd_getvideoinfo(FAR struct fb_vtable_s *vtable,
                                   FAR struct fb_videoinfo_s *vinfo)
{
  FAR struct bk7258_lcd_priv_s *priv = &g_bk7258_lcd;

  (void)vtable;

  if (vinfo == NULL || priv->board == NULL)
    {
      return -EINVAL;
    }

  memset(vinfo, 0, sizeof(*vinfo));
  vinfo->fmt     = FB_FMT_RGB16_565;
  vinfo->xres    = priv->board->panel->width;
  vinfo->yres    = priv->board->panel->height;
  vinfo->nplanes = 1;
  return OK;
}

#ifdef CONFIG_FB_SYNC
static int bk7258_lcd_waitforvsync(FAR struct fb_vtable_s *vtable)
{
  FAR struct bk7258_lcd_priv_s *priv = &g_bk7258_lcd;

  (void)vtable;
  return nxsem_tickwait_uninterruptible(&priv->flip_sem,
                                        MSEC2TICK(100));
}
#endif

static int bk7258_lcd_getplaneinfo(FAR struct fb_vtable_s *vtable,
                                   int planeno,
                                   FAR struct fb_planeinfo_s *pinfo)
{
  FAR struct bk7258_lcd_priv_s *priv = &g_bk7258_lcd;
  size_t frame_bytes;
  uint8_t display;

  (void)vtable;

  if (planeno != 0 || pinfo == NULL || priv->board == NULL)
    {
      return -EINVAL;
    }

  /* The framebuffer upper half carries the requested scanout page in
   * pinfo->display while planeno remains the RGB color plane (zero).  Keep
   * that input before clearing the result.  LVGL's generic NuttX fbdev port
   * queries display 0 and display 1 separately when yres_virtual advertises
   * double buffering.
   */

  display = pinfo->display;
  if (display >= BK7258_LCD_FRAME_COUNT)
    {
      return -EINVAL;
    }

  frame_bytes = (size_t)priv->board->panel->width *
                priv->board->panel->height * 2u;
  memset(pinfo, 0, sizeof(*pinfo));
  pinfo->fbmem        = priv->framebuf + (size_t)display * frame_bytes;
  pinfo->fblen        = display == 0 ? priv->framebuf_bytes : frame_bytes;
  pinfo->stride       = priv->board->panel->width * 2u;
  pinfo->display      = display;
  pinfo->bpp          = 16;
  pinfo->xres_virtual = priv->board->panel->width;
  pinfo->yres_virtual = display == 0 ?
                        priv->board->panel->height *
                        BK7258_LCD_FRAME_COUNT :
                        priv->board->panel->height;
  return OK;
}

#ifdef CONFIG_FB_UPDATE
static int bk7258_lcd_updatearea(FAR struct fb_vtable_s *vtable,
                                 FAR const struct fb_area_s *area)
{
  (void)vtable;
  (void)area;

  /* The controller continuously scans the PSRAM buffer.  This mapping is
   * cache-off today; retain the barrier for the standard FBIO_UPDATE hook.
   */

  __asm volatile ("dmb sy" ::: "memory");
  return OK;
}
#endif

static int bk7258_lcd_getpower(FAR struct fb_vtable_s *vtable)
{
  (void)vtable;
  return g_bk7258_lcd.power;
}

static int bk7258_lcd_setpower(FAR struct fb_vtable_s *vtable, int power)
{
  FAR struct bk7258_lcd_priv_s *priv = &g_bk7258_lcd;
  bk_err_t sdkret;
  int ret;

  (void)vtable;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->board == NULL || priv->board->set_backlight == NULL)
    {
      ret = -ENODEV;
      goto out;
    }

  sdkret = bk_lcd_rgb_display_en(power > 0);
  if (sdkret != BK_OK)
    {
      ret = -EIO;
      goto out;
    }

  ret = priv->board->set_backlight(priv->board, power > 0);
  if (ret < 0)
    {
      (void)bk_lcd_rgb_display_en(power <= 0);
      goto out;
    }

  priv->power = power > 0 ? 1 : 0;
  ret = OK;

out:
  nxmutex_unlock(&priv->lock);
  return ret;
}

static int bk7258_lcd_ioctl(FAR struct fb_vtable_s *vtable, int cmd,
                            unsigned long arg)
{
  (void)vtable;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

int bk7258_lcd_initialize(
  FAR const struct bk7258_lcd_board_s *board)
{
  FAR struct bk7258_lcd_priv_s *priv = &g_bk7258_lcd;
  const struct bk7258_lcd_panel_s *panel;
  lcd_device_t lcd_dev;
  lcd_rgb_t rgb;
  lcd_clk_t clock;
  bk_err_t sdkret;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->inited)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  priv->board = board;
  if (priv->board == NULL || priv->board->panel == NULL ||
      priv->board->panel->initialize == NULL ||
      priv->board->rgb_pins_initialize == NULL ||
      priv->board->set_backlight == NULL)
    {
      ret = -ENODEV;
      goto errout;
    }

  panel = priv->board->panel;
  if (panel->format != BK7258_LCD_PIXEL_FORMAT_RGB565 ||
      panel->width == 0 || panel->height == 0)
    {
      ret = -ENOTSUP;
      goto errout;
    }

  ret = bk7258_lcd_sdk_clock(priv->board->timing.pixel_clock_mhz, &clock);
  if (ret < 0)
    {
      goto errout;
    }

  if (!bk7258_psram_ready())
    {
      syslog(LOG_ERR, "BK7258 LCD: PSRAM is not ready\n");
      ret = -EAGAIN;
      goto errout;
    }

  priv->framebuf_bytes = (size_t)panel->width * panel->height * 2u *
                         BK7258_LCD_FRAME_COUNT;
  priv->framebuf_alloc =
    bk7258_lcd_framebuffer_alloc(priv->framebuf_bytes + 15u);
  if (priv->framebuf_alloc == NULL)
    {
      syslog(LOG_ERR, "BK7258 LCD: framebuffer allocation failed\n");
      ret = -ENOMEM;
      goto errout;
    }

  priv->framebuf = (FAR uint8_t *)
    (((uintptr_t)priv->framebuf_alloc + 15u) & ~(uintptr_t)15u);

  ret = panel->initialize(priv->board);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 LCD: %s init failed: %d\n",
             panel->name, ret);
      goto errout_with_framebuffer;
    }

  ret = priv->board->rgb_pins_initialize(priv->board);
  if (ret < 0)
    {
      goto errout_with_framebuffer;
    }

  memset(&rgb, 0, sizeof(rgb));
  rgb.clk = clock;
  rgb.data_out_clk_edge =
    priv->board->timing.data_changes_on_rising_edge ?
    NEGEDGE_OUTPUT : POSEDGE_OUTPUT;
  rgb.hsync_back_porch = priv->board->timing.hsync_back_porch;
  rgb.hsync_front_porch = priv->board->timing.hsync_front_porch;
  rgb.vsync_back_porch = priv->board->timing.vsync_back_porch;
  rgb.vsync_front_porch = priv->board->timing.vsync_front_porch;
  rgb.hsync_pulse_width = priv->board->timing.hsync_pulse_width;
  rgb.vsync_pulse_width = priv->board->timing.vsync_pulse_width;

  memset(&lcd_dev, 0, sizeof(lcd_dev));
  lcd_dev.id      = LCD_DEVICE_UNKNOW;
  lcd_dev.name    = (char *)panel->name;
  lcd_dev.type    = LCD_TYPE_RGB565;
  lcd_dev.width   = panel->width;
  lcd_dev.height  = panel->height;
  lcd_dev.src_fmt = PIXEL_FMT_RGB565;
  lcd_dev.out_fmt = PIXEL_FMT_RGB565;
  lcd_dev.rgb     = &rgb;

  sdkret = lcd_driver_init(&lcd_dev);
  if (sdkret != BK_OK)
    {
      syslog(LOG_ERR, "BK7258 LCD: controller init failed: %d\n", sdkret);
      ret = -EIO;
      goto errout_with_framebuffer;
    }

  ret = bk7258_lcd_route_irq_to_ap_primary();
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 LCD: AP IRQ route failed\n");
      (void)lcd_driver_deinit();
      goto errout_with_framebuffer;
    }

  sdkret = bk_lcd_isr_register(RGB_OUTPUT_EOF, bk7258_lcd_eof_isr, priv);
  if (sdkret != BK_OK)
    {
      syslog(LOG_ERR, "BK7258 LCD: EOF callback failed: %d\n", sdkret);
      ret = -EIO;
      goto errout_with_controller;
    }

  bk7258_lcd_restore_sync_widths(&priv->board->timing);
  lcd_driver_ppi_set(panel->width, panel->height);

  sdkret = bk_lcd_set_yuv_mode(PIXEL_FMT_RGB565);
  if (sdkret != BK_OK)
    {
      syslog(LOG_ERR, "BK7258 LCD: RGB565 mode failed: %d\n", sdkret);
      ret = -EIO;
      goto errout_with_controller;
    }

  sdkret = lcd_driver_set_display_base_addr((uint32_t)priv->framebuf);
  if (sdkret != BK_OK)
    {
      syslog(LOG_ERR, "BK7258 LCD: scanout address failed: %d\n", sdkret);
      ret = -EIO;
      goto errout_with_controller;
    }

  sdkret = lcd_driver_display_enable();
  if (sdkret != BK_OK)
    {
      syslog(LOG_ERR, "BK7258 LCD: display enable failed: %d\n", sdkret);
      ret = -EIO;
      goto errout_with_controller;
    }

  ret = priv->board->set_backlight(priv->board, true);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 LCD: backlight enable failed: %d\n", ret);
      (void)bk_lcd_rgb_display_en(false);
      goto errout_with_controller;
    }

  priv->power = 1;
  ret = fb_register_device(0, 0, &priv->vtable);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 LCD: fb registration failed: %d\n", ret);
      (void)bk_lcd_rgb_display_en(false);
      (void)priv->board->set_backlight(priv->board, false);
      priv->power = 0;
      goto errout_with_controller;
    }

#ifdef CONFIG_BK7258_LCD_VALIDATION_PATTERN
  /* NuttX clears exported framebuffer memory while registering /dev/fb0. */

  bk7258_lcd_fill_validation_pattern(priv);
  bk7258_lcd_log_validation_state(priv);
#endif

  priv->inited = true;
  syslog(LOG_INFO,
         "BK7258 LCD: ready board=%s panel=%s %ux%u RGB565 clock=%uMHz "
         "fb=%p\n",
         priv->board->name, panel->name, panel->width, panel->height,
         priv->board->timing.pixel_clock_mhz, priv->framebuf);

  nxmutex_unlock(&priv->lock);
  return OK;

errout_with_controller:
  bk7258_lcd_unroute_ap_primary_irq();
  (void)lcd_driver_deinit();
errout_with_framebuffer:
  bk7258_lcd_framebuffer_free(priv->framebuf_alloc);
  priv->framebuf_alloc = NULL;
  priv->framebuf = NULL;
  priv->framebuf_bytes = 0;
errout:
  priv->board = NULL;
  nxmutex_unlock(&priv->lock);
  return ret;
}

#endif /* CONFIG_BK7258_LCD */
