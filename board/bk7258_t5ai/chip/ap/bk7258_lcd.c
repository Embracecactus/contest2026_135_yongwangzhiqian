/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/bk7258_lcd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI Board) LCD — NuttX lcd_dev_s lower-half for the 3.5"
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
 * Registration: the NuttX framebuffer layer calls board_graphics_setup()
 * (CONFIG_LCD_EXTERNINIT) to obtain the lcd_dev_s; this file exports it.
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

#include <nuttx/board.h>
#include <nuttx/arch.h>
#include <nuttx/lcd/lcd.h>
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
  struct lcd_dev_s dev;              /* NuttX LCD lower-half anchor */
  uint16_t power;                    /* panel power level */
  uint8_t *framebuf_alloc;           /* PSRAM allocation before alignment */
  uint8_t *framebuf;                 /* RGB565 frame buffer */
  bool inited;                       /* LCD hardware initialized */
};

static int bk7258_lcd_getvideoinfo(FAR struct lcd_dev_s *dev,
                                   FAR struct fb_videoinfo_s *vinfo);
static int bk7258_lcd_getplaneinfo(FAR struct lcd_dev_s *dev,
                                   unsigned int planeno,
                                   FAR struct lcd_planeinfo_s *pinfo);
static int bk7258_lcd_putrun(FAR struct lcd_dev_s *dev, fb_coord_t row,
                             fb_coord_t col, FAR const uint8_t *buffer,
                             size_t npixels);
static int bk7258_lcd_getpower(FAR struct lcd_dev_s *dev);
static int bk7258_lcd_setpower(FAR struct lcd_dev_s *dev, int power);
static int bk7258_lcd_getareaalign(FAR struct lcd_dev_s *dev,
                                   FAR struct lcddev_area_align_s *align);
static int bk7258_lcd_ioctl(FAR struct lcd_dev_s *dev, int cmd,
                            unsigned long arg);

static void bk7258_lcd_sw_spi_send_byte(uint8_t data);
static void bk7258_lcd_sw_spi_write_cmd(uint8_t cmd);
static void bk7258_lcd_sw_spi_write_data(uint8_t data);
static void bk7258_lcd_sw_spi_init_seq(const uint8_t *seq);
static int bk7258_lcd_ili9488_init(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct lcd_dev_s g_bk7258_lcd_dev =
{
  .getvideoinfo  = bk7258_lcd_getvideoinfo,
  .getplaneinfo  = bk7258_lcd_getplaneinfo,
  .getpower      = bk7258_lcd_getpower,
  .setpower      = bk7258_lcd_setpower,
  .getareaalign  = bk7258_lcd_getareaalign,
  .ioctl         = bk7258_lcd_ioctl,
};

/* One raster line working buffer for the NuttX planeinfo. */

static uint8_t g_bk7258_lcd_runbuf[BK7258_LCD_WIDTH * 2];

static struct bk7258_lcd_priv_s g_bk7258_lcd =
{
  .power       = 0,
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
 * Private Functions — NuttX lcd_dev_s
 ****************************************************************************/

static int bk7258_lcd_getvideoinfo(FAR struct lcd_dev_s *dev,
                                   FAR struct fb_videoinfo_s *vinfo)
{
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

static int bk7258_lcd_getplaneinfo(FAR struct lcd_dev_s *dev,
                                   unsigned int planeno,
                                   FAR struct lcd_planeinfo_s *pinfo)
{
  if (planeno == 0 && pinfo != NULL)
    {
      pinfo->putrun  = bk7258_lcd_putrun;
      pinfo->getrun  = NULL;   /* read-back not supported */
      pinfo->buffer  = g_bk7258_lcd_runbuf;
      pinfo->bpp     = BK7258_LCD_BPP;
      return OK;
    }

  return -EINVAL;
}

static int bk7258_lcd_putrun(FAR struct lcd_dev_s *dev, fb_coord_t row,
                             fb_coord_t col, FAR const uint8_t *buffer,
                             size_t npixels)
{
  FAR struct bk7258_lcd_priv_s *priv =
    (FAR struct bk7258_lcd_priv_s *)dev;
  size_t offset;

  if (row >= BK7258_LCD_HEIGHT || col >= BK7258_LCD_WIDTH)
    {
      return -EINVAL;
    }

  if (col + npixels > BK7258_LCD_WIDTH)
    {
      npixels = BK7258_LCD_WIDTH - col;
    }

  offset = (row * BK7258_LCD_WIDTH + col) * 2;
  memcpy(priv->framebuf + offset, buffer, npixels * 2);
  return OK;
}

static int bk7258_lcd_getpower(FAR struct lcd_dev_s *dev)
{
  FAR struct bk7258_lcd_priv_s *priv =
    (FAR struct bk7258_lcd_priv_s *)dev;

  return priv->power;
}

static int bk7258_lcd_setpower(FAR struct lcd_dev_s *dev, int power)
{
  FAR struct bk7258_lcd_priv_s *priv =
    (FAR struct bk7258_lcd_priv_s *)dev;

  if (power > 0)
    {
      (void)bk_lcd_rgb_display_en(true);
      priv->power = 1;
    }
  else
    {
      (void)bk_lcd_rgb_display_en(false);
      priv->power = 0;
    }

  return OK;
}

static int bk7258_lcd_getareaalign(FAR struct lcd_dev_s *dev,
                                   FAR struct lcddev_area_align_s *align)
{
  if (align == NULL)
    {
      return -EINVAL;
    }

  align->row_start_align = 1;
  align->height_align    = 1;
  align->col_start_align = 1;
  align->width_align     = 1;
  align->buf_align       = 16;
  return OK;
}

static int bk7258_lcd_ioctl(FAR struct lcd_dev_s *dev, int cmd,
                            unsigned long arg)
{
  (void)dev;
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

  if (priv->inited)
    {
      return OK;
    }

  if (!bk7258_psram_ready())
    {
      return -EAGAIN;
    }

  priv->framebuf_alloc = bk7258_psram_zalloc(BK7258_LCD_FRAMEBUF_BYTES + 15);
  if (priv->framebuf_alloc == NULL)
    {
      return -ENOMEM;
    }

  priv->framebuf = (FAR uint8_t *)
    (((uintptr_t)priv->framebuf_alloc + 15u) & ~(uintptr_t)15u);

  /* lcd_dev_s is an embedded first member: copy the pre-initialised
   * vtable so dev->getvideoinfo etc. are valid and the methods can cast
   * the received dev pointer back to priv.
   */

  priv->dev = g_bk7258_lcd_dev;

  /* Software SPI init sequence first (panel registers). */

  ret = bk7258_lcd_ili9488_init();
  if (ret != OK)
    {
      goto errout_with_framebuf;
    }

  /* RGB timing. */

  memset(&rgb, 0, sizeof(rgb));
  rgb.clk                 = LCD_30M;
  rgb.data_out_clk_edge   = POSEDGE_OUTPUT;
  rgb.hsync_pulse_width   = 20;
  rgb.vsync_pulse_width   = 4;
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
      ret = -EIO;
      goto errout_with_framebuf;
    }

  ret = bk_lcd_rgb_init(&lcd_dev);
  if (ret != BK_OK)
    {
      bk_lcd_driver_deinit();
      ret = -EIO;
      goto errout_with_framebuf;
    }

  /* Point the RGB controller at our frame buffer. */

  lcd_driver_set_display_base_addr((uint32_t)priv->framebuf);

  (void)bk_lcd_rgb_display_en(true);
  priv->power = 1;
  priv->inited = true;

  return OK;

errout_with_framebuf:
  bk7258_psram_free(priv->framebuf_alloc);
  priv->framebuf_alloc = NULL;
  priv->framebuf = NULL;
  return ret;
}

/* NuttX framebuffer layer entry (CONFIG_LCD_EXTERNINIT). */

FAR struct lcd_dev_s *board_graphics_setup(unsigned int devno)
{
  if (bk7258_lcd_initialize() != OK)
    {
      return NULL;
    }

  return (FAR struct lcd_dev_s *)&g_bk7258_lcd.dev;
}

#endif /* CONFIG_BK7258_LCD */
