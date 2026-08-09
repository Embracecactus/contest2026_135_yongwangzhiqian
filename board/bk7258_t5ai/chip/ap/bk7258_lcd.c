/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/bk7258_lcd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI Board) direct NuttX framebuffer driver for the 3.5"
 * ILI9488 TFT (320x480 RGB565).
 *
 * The ILI9488 is an RGB parallel panel whose registers are initialized
 * over a software (bit-banged) SPI on the T5-AI Board:
 *   CLK=GPIO49, CSX=GPIO48, SDA=GPIO50, RST=GPIO53 (no DC line).
 * Pixel data flows over the RGB bus from a memory frame buffer that the
 * SDK RGB controller refreshes continuously.
 *
 * Software SPI is modelled on TuyaOpen's tdd_disp_sw_spi.c; the init
 * sequence comes from tdd_disp_rgb_ili9488.c.  The 34 bk_lcd_* symbols
 * live exclusively in the AP libdriver.a, so this driver is AP-only.
 *
 * The RGB controller scans a single PSRAM buffer continuously.  Expose that
 * buffer directly through fb_register_device(); the generic LCD framebuffer
 * adapter cannot be used because it allocates a second 300 KiB shadow buffer
 * from the much smaller AP SRAM heap.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_LCD

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/mutex.h>
#include <nuttx/video/fb.h>

#include <arch/chip/bk7258_lcd.h>
#include <arch/chip/bk7258_psram.h>

#include <driver/gpio.h>
#include <driver/lcd.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Frame buffer: 320x480 RGB565 (2 bytes/pixel). */

#define BK7258_LCD_FRAMEBUF_BYTES  (BK7258_LCD_WIDTH * \
                                    BK7258_LCD_HEIGHT * 2)

/* Software SPI bit timing helper: a short busy loop. */

#define BK7258_LCD_SPI_DELAY()     do { } while (0)

/* ILI9488 command codes (from ILI9488 datasheet, via TuyaOpen). */

#define ILI9488_NOP        0x00
#define ILI9488_SLPOUT     0x11
#define ILI9488_PWCTR1     0xC0
#define ILI9488_PWCTR2     0xC1
#define ILI9488_VMCTR1     0xC5
#define ILI9488_IFMODE     0xC6
#define ILI9488_FRMCTR1    0xC8
#define ILI9488_INVCTR     0x80
#define ILI9488_PRCTR      0xD0
#define ILI9488_DFUNCTR    0xB6
#define ILI9488_MADCTL     0x36
#define ILI9488_PIXFMT     0x3A
#define ILI9488_INVON      0x21
#define ILI9488_SETIMAGE   0x30
#define ILI9488_ACTRL3     0xE4
#define ILI9488_ACTRL4     0xE5
#define ILI9488_GMCTRP1    0xE0
#define ILI9488_GMCTRN1    0xE1
#define ILI9488_DISPON     0x29

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_lcd_priv_s
{
  struct fb_vtable_s vtable;         /* NuttX framebuffer interface */
  mutex_t lock;                      /* init and power serialization */
  uint16_t power;                    /* panel power level */
  uint8_t *framebuf_alloc;           /* PSRAM allocation before alignment */
  uint8_t *framebuf;                 /* RGB565 frame buffer */
  bool inited;                       /* LCD hardware initialized */
};

static int bk7258_lcd_getvideoinfo(FAR struct fb_vtable_s *vtable,
                                   FAR struct fb_videoinfo_s *vinfo);
static int bk7258_lcd_getplaneinfo(FAR struct fb_vtable_s *vtable,
                                   int planeno,
                                   FAR struct fb_planeinfo_s *pinfo);
#ifdef CONFIG_FB_UPDATE
static int bk7258_lcd_updatearea(FAR struct fb_vtable_s *vtable,
                                 FAR const struct fb_area_s *area);
#endif
static int bk7258_lcd_getpower(FAR struct fb_vtable_s *vtable);
static int bk7258_lcd_setpower(FAR struct fb_vtable_s *vtable, int power);
static int bk7258_lcd_ioctl(FAR struct fb_vtable_s *vtable, int cmd,
                            unsigned long arg);

static void bk7258_lcd_sw_spi_send_byte(uint8_t data);
static void bk7258_lcd_sw_spi_write_cmd(uint8_t cmd);
static void bk7258_lcd_sw_spi_write_data(uint8_t data);
static void bk7258_lcd_sw_spi_init_seq(const uint8_t *seq);
static int bk7258_lcd_ili9488_init(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_lcd_priv_s g_bk7258_lcd =
{
  .vtable =
  {
    .getvideoinfo = bk7258_lcd_getvideoinfo,
    .getplaneinfo = bk7258_lcd_getplaneinfo,
#ifdef CONFIG_FB_UPDATE
    .updatearea   = bk7258_lcd_updatearea,
#endif
    .getpower     = bk7258_lcd_getpower,
    .setpower     = bk7258_lcd_setpower,
    .ioctl        = bk7258_lcd_ioctl,
  },
  .lock         = NXMUTEX_INITIALIZER,
  .power        = 0,
  .framebuf_alloc = NULL,
  .framebuf    = NULL,
  .inited      = false,
};

/* ILI9488 init sequence: [data_count][delay_ms][cmd][data...], 0 terminator.
 * From TuyaOpen tdd_disp_rgb_ili9488.c.
 */

static const uint8_t g_bk7258_lcd_ili9488_seq[] =
{
  3,  0,   ILI9488_PWCTR1,   0x0E, 0x0E,
  2,  0,   ILI9488_PWCTR2,   0x46,
  4,  0,   ILI9488_VMCTR1,   0x00, 0x2D, 0x80,
  2,  0,   ILI9488_IFMODE,   0x00,
  2,  0,   ILI9488_FRMCTR1,  0xA0,
  2,  0,   ILI9488_INVCTR,   0x02,
  5,  0,   ILI9488_PRCTR,    0x08, 0x0C, 0x50, 0x64,
  3,  0,   ILI9488_DFUNCTR,  0x32, 0x02,
  2,  0,   ILI9488_MADCTL,   0x48,
  2,  0,   ILI9488_PIXFMT,   0x70,
  2,  0,   ILI9488_INVON,    0x00,
  2,  0,   ILI9488_SETIMAGE, 0x01,
  5,  0,   ILI9488_ACTRL3,   0xA9, 0x51, 0x2C, 0x82,
  3,  0,   ILI9488_ACTRL4,   0x21, 0x05,
  16, 0,   ILI9488_GMCTRP1,  0x00, 0x0C, 0x10, 0x03, 0x0F, 0x05,
                              0x37, 0x66, 0x4D, 0x03, 0x0C, 0x0A,
                              0x2F, 0x35, 0x0F,
  16, 0,   ILI9488_GMCTRN1,  0x00, 0x0F, 0x16, 0x06, 0x13, 0x07,
                              0x3B, 0x35, 0x51, 0x07, 0x10, 0x0D,
                              0x36, 0x3B, 0x0F,
  1,  120, ILI9488_SLPOUT,
  1,  20,  ILI9488_DISPON,
  0,
};

/****************************************************************************
 * Private Functions — Software SPI
 ****************************************************************************/

static void bk7258_lcd_sw_spi_send_byte(uint8_t data)
{
  uint8_t n;

  for (n = 0; n < 8; n++)
    {
      if (data & 0x80)
        {
          (void)bk_gpio_set_output_value(
                  (gpio_id_t)BK7258_LCD_SW_SPI_SDA_PIN, true);
        }
      else
        {
          (void)bk_gpio_set_output_value(
                  (gpio_id_t)BK7258_LCD_SW_SPI_SDA_PIN, false);
        }

      data <<= 1;

      (void)bk_gpio_set_output_value(
              (gpio_id_t)BK7258_LCD_SW_SPI_CLK_PIN, false);
      (void)bk_gpio_set_output_value(
              (gpio_id_t)BK7258_LCD_SW_SPI_CLK_PIN, true);
    }
}

static void bk7258_lcd_sw_spi_write_cmd(uint8_t cmd)
{
  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_CSX_PIN, false);
  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_SDA_PIN, false);

  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_CLK_PIN, false);
  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_CLK_PIN, true);

  bk7258_lcd_sw_spi_send_byte(cmd);

  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_CSX_PIN, true);
}

static void bk7258_lcd_sw_spi_write_data(uint8_t data)
{
  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_CSX_PIN, false);
  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_SDA_PIN, true);

  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_CLK_PIN, false);
  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_CLK_PIN, true);

  bk7258_lcd_sw_spi_send_byte(data);

  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_CSX_PIN, true);
}

/* seq format: [data_count][delay_ms][cmd][data...], 0-terminated. */

static void bk7258_lcd_sw_spi_init_seq(const uint8_t *seq)
{
  while (*seq != 0)
    {
      uint8_t count = seq[0];
      uint8_t delay = seq[1];
      uint8_t cmd   = seq[2];
      uint8_t i;

      bk7258_lcd_sw_spi_write_cmd(cmd);
      for (i = 0; i + 1 < count; i++)
        {
          bk7258_lcd_sw_spi_write_data(seq[3 + i]);
        }

      if (delay > 0)
        {
          up_mdelay(delay);
        }

      seq += count + 2;
    }
}

/****************************************************************************
 * Name: bk7258_lcd_ili9488_init
 *
 * Reset the panel and run the software-SPI init sequence.
 ****************************************************************************/

static int bk7258_lcd_ili9488_init(void)
{
  gpio_config_t cfg;
  bk_err_t ret;

  /* Configure software SPI GPIOs as push-pull outputs. */

  memset(&cfg, 0, sizeof(cfg));
  cfg.io_mode   = GPIO_OUTPUT_ENABLE;
  cfg.pull_mode = GPIO_PULL_DISABLE;
  cfg.func_mode = GPIO_SECOND_FUNC_DISABLE;

  ret = bk_gpio_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  (void)bk_gpio_set_config((gpio_id_t)BK7258_LCD_SW_SPI_CLK_PIN, &cfg);
  (void)bk_gpio_set_config((gpio_id_t)BK7258_LCD_SW_SPI_CSX_PIN, &cfg);
  (void)bk_gpio_set_config((gpio_id_t)BK7258_LCD_SW_SPI_SDA_PIN, &cfg);
  (void)bk_gpio_set_config((gpio_id_t)BK7258_LCD_SW_SPI_RST_PIN, &cfg);

  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_CLK_PIN, true);
  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_CSX_PIN, true);
  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_SDA_PIN, false);
  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_RST_PIN, true);

  up_mdelay(100);

  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_RST_PIN, false);
  up_mdelay(100);
  (void)bk_gpio_set_output_value(
          (gpio_id_t)BK7258_LCD_SW_SPI_RST_PIN, true);
  up_mdelay(100);

  bk7258_lcd_sw_spi_init_seq(g_bk7258_lcd_ili9488_seq);

  return OK;
}

/****************************************************************************
 * Private Functions — NuttX framebuffer
 ****************************************************************************/

static int bk7258_lcd_getvideoinfo(FAR struct fb_vtable_s *vtable,
                                   FAR struct fb_videoinfo_s *vinfo)
{
  (void)vtable;

  if (vinfo == NULL)
    {
      return -EINVAL;
    }

  vinfo->fmt     = FB_FMT_RGB16_565;
  vinfo->xres    = BK7258_LCD_WIDTH;
  vinfo->yres    = BK7258_LCD_HEIGHT;
  vinfo->nplanes = 1;
  return OK;
}

static int bk7258_lcd_getplaneinfo(FAR struct fb_vtable_s *vtable,
                                   int planeno,
                                   FAR struct fb_planeinfo_s *pinfo)
{
  FAR struct bk7258_lcd_priv_s *priv = &g_bk7258_lcd;

  (void)vtable;

  if (planeno == 0 && pinfo != NULL)
    {
      memset(pinfo, 0, sizeof(*pinfo));
      pinfo->fbmem        = priv->framebuf;
      pinfo->fblen        = BK7258_LCD_FRAMEBUF_BYTES;
      pinfo->stride       = BK7258_LCD_WIDTH * 2;
      pinfo->display      = 0;
      pinfo->bpp          = BK7258_LCD_BPP;
      pinfo->xres_virtual = BK7258_LCD_WIDTH;
      pinfo->yres_virtual = BK7258_LCD_HEIGHT;
      return OK;
    }

  return -EINVAL;
}

#ifdef CONFIG_FB_UPDATE
static int bk7258_lcd_updatearea(FAR struct fb_vtable_s *vtable,
                                 FAR const struct fb_area_s *area)
{
  /* The RGB engine continuously scans the exported PSRAM buffer.  Cache is
   * currently disabled for this mapping, so no copy or cache maintenance is
   * needed.  Keep the hook for the standard FBIO_UPDATE contract.
   */

  (void)vtable;
  (void)area;
  __asm volatile ("dmb sy" ::: "memory");
  return OK;
}
#endif

static int bk7258_lcd_getpower(FAR struct fb_vtable_s *vtable)
{
  FAR struct bk7258_lcd_priv_s *priv = &g_bk7258_lcd;

  (void)vtable;

  return priv->power;
}

static int bk7258_lcd_setpower(FAR struct fb_vtable_s *vtable, int power)
{
  FAR struct bk7258_lcd_priv_s *priv = &g_bk7258_lcd;
  int ret;

  (void)vtable;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (power > 0)
    {
      ret = bk_lcd_rgb_display_en(true);
      if (ret == BK_OK)
        {
          priv->power = 1;
          ret = OK;
        }
      else
        {
          ret = -EIO;
        }
    }
  else
    {
      ret = bk_lcd_rgb_display_en(false);
      if (ret == BK_OK)
        {
          priv->power = 0;
          ret = OK;
        }
      else
        {
          ret = -EIO;
        }
    }

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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_lcd_initialize(void)
{
  FAR struct bk7258_lcd_priv_s *priv = &g_bk7258_lcd;
  lcd_device_t lcd_dev;
  lcd_rgb_t rgb;
  bk_err_t ret;
  int result;

  result = nxmutex_lock(&priv->lock);
  if (result < 0)
    {
      return result;
    }

  if (priv->inited)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  if (!bk7258_psram_ready())
    {
      nxmutex_unlock(&priv->lock);
      return -EAGAIN;
    }

  priv->framebuf_alloc = bk7258_psram_zalloc(BK7258_LCD_FRAMEBUF_BYTES + 15);
  if (priv->framebuf_alloc == NULL)
    {
      nxmutex_unlock(&priv->lock);
      return -ENOMEM;
    }

  priv->framebuf = (FAR uint8_t *)
    (((uintptr_t)priv->framebuf_alloc + 15u) & ~(uintptr_t)15u);

  /* Software SPI init sequence first (panel registers). */

  ret = bk7258_lcd_ili9488_init();
  if (ret != OK)
    {
      result = ret;
      goto errout_with_framebuf;
    }

  /* RGB timing. */

  memset(&rgb, 0, sizeof(rgb));
  rgb.clk                 = LCD_30M;
  rgb.data_out_clk_edge   = POSEDGE_OUTPUT;
  /* The BK7258 LCD controller exposes three-bit pulse-width fields.  The
   * v3.1.1.9 driver clamps any value above seven by programming 2/2, so use
   * those effective values explicitly instead of relying on its fallback.
   */

  rgb.hsync_pulse_width   = 2;
  rgb.vsync_pulse_width   = 2;
  rgb.hsync_back_porch    = 80;
  rgb.hsync_front_porch   = 80;
  rgb.vsync_back_porch    = 8;
  rgb.vsync_front_porch   = 8;

  memset(&lcd_dev, 0, sizeof(lcd_dev));
  lcd_dev.id     = LCD_DEVICE_UNKNOW;
  lcd_dev.name   = "ili9488";
  lcd_dev.type   = LCD_TYPE_RGB565;
  lcd_dev.width  = BK7258_LCD_WIDTH;
  lcd_dev.height = BK7258_LCD_HEIGHT;
  lcd_dev.rgb    = &rgb;

  ret = bk_lcd_driver_init(LCD_30M);
  if (ret != BK_OK)
    {
      result = -EIO;
      goto errout_with_framebuf;
    }

  ret = bk_lcd_rgb_init(&lcd_dev);
  if (ret != BK_OK)
    {
      (void)bk_lcd_rgb_deinit();
      bk_lcd_driver_deinit();
      result = -EIO;
      goto errout_with_framebuf;
    }

  /* Point the RGB controller at our frame buffer. */

  lcd_driver_set_display_base_addr((uint32_t)priv->framebuf);

  (void)bk_lcd_rgb_display_en(true);
  priv->power = 1;

  result = fb_register_device(0, 0, &priv->vtable);
  if (result < 0)
    {
      (void)bk_lcd_rgb_display_en(false);
      (void)bk_lcd_rgb_deinit();
      bk_lcd_driver_deinit();
      priv->power = 0;
      goto errout_with_framebuf;
    }

  priv->inited = true;

  nxmutex_unlock(&priv->lock);
  return OK;

errout_with_framebuf:
  bk7258_psram_free(priv->framebuf_alloc);
  priv->framebuf_alloc = NULL;
  priv->framebuf = NULL;
  nxmutex_unlock(&priv->lock);
  return result;
}

#endif /* CONFIG_BK7258_LCD */
