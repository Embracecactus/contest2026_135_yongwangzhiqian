/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_dual_lcd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK AI Toy dual GC9D01 board binding.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AIDK_DUAL_LCD

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/param.h>
#include <syslog.h>

#include <nuttx/lcd/gc9d01.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>
#include <nuttx/video/fb.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_lcd_spi.h>

#include <driver/gpio.h>

#define AIDK_LCD_POWER_SETTLE_US 10000u

struct aidk_lcd_panel_s
{
  struct gc9d01_lcd_s transport;
  struct bk7258_lcd_spi_config_s bus_config;
  FAR struct bk7258_lcd_spi_bus_s *bus;
  FAR struct lcd_dev_s *lcddev;
  uint8_t fbno;
};

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);
extern bk_err_t bk_pm_module_vote_ctrl_external_ldo(
  uint32_t module, gpio_id_t gpio_id, gpio_output_state_e value);

_Static_assert(BK7258_BOARD_LCD_PANEL_COUNT == 2,
               "AIDK LCD binding requires two panels");
_Static_assert(BK7258_BOARD_LCD1_SPI_ID == 1 &&
               BK7258_BOARD_LCD1_CLK_GPIO == 2 &&
               BK7258_BOARD_LCD1_CS_GPIO == 3 &&
               BK7258_BOARD_LCD1_DATA_GPIO == 4 &&
               BK7258_BOARD_LCD1_DC_GPIO == 5 &&
               BK7258_BOARD_LCD1_RESET_GPIO == 45,
               "AIDK LCD1 binding no longer matches the schematic");
_Static_assert(BK7258_BOARD_LCD2_SPI_ID == 0 &&
               BK7258_BOARD_LCD2_CLK_GPIO == 22 &&
               BK7258_BOARD_LCD2_CS_GPIO == 23 &&
               BK7258_BOARD_LCD2_DATA_GPIO == 24 &&
               BK7258_BOARD_LCD2_DC_GPIO == 7 &&
               BK7258_BOARD_LCD2_RESET_GPIO == 6,
               "AIDK LCD2 binding no longer matches the schematic");
_Static_assert(BK7258_BOARD_LCD_BACKLIGHT_PWM_GPIO == 25 &&
               BK7258_BOARD_LCD_BACKLIGHT_ACTIVE_HIGH == 1 &&
               BK7258_BOARD_LCD_LDO_GPIO == 52,
               "AIDK shared LCD power binding changed");

static mutex_t g_aidk_lcd_setup_lock = NXMUTEX_INITIALIZER;
static mutex_t g_aidk_lcd_backlight_lock = NXMUTEX_INITIALIZER;
static uint8_t g_aidk_lcd_backlight_users;
static bool g_aidk_lcd_power_voted;

static int aidk_lcd_reset(FAR struct gc9d01_lcd_s *lcd, bool asserted);
static int aidk_lcd_writecmd(FAR struct gc9d01_lcd_s *lcd, uint8_t cmd,
                             FAR const uint8_t *params, size_t nparams);
static int aidk_lcd_writegram(FAR struct gc9d01_lcd_s *lcd,
                              FAR const uint16_t *pixels, size_t width,
                              size_t height, size_t stride);
static int aidk_lcd_backlight(FAR struct gc9d01_lcd_s *lcd, int power);

static struct aidk_lcd_panel_s g_aidk_lcd_panels[2] =
{
  {
    .transport =
    {
      .reset = aidk_lcd_reset,
      .writecmd = aidk_lcd_writecmd,
      .writegram = aidk_lcd_writegram,
      .backlight = aidk_lcd_backlight,
    },
    .bus_config =
    {
      .name = "AIDK AI Toy LCD1 transport",
      .spi_id = BK7258_BOARD_LCD1_SPI_ID,
      .reset_gpio = BK7258_BOARD_LCD1_RESET_GPIO,
      .dc_gpio = BK7258_BOARD_LCD1_DC_GPIO,
      .width = BK7258_BOARD_LCD_WIDTH,
      .height = BK7258_BOARD_LCD_HEIGHT,
    },
    .bus = NULL,
    .lcddev = NULL,
    .fbno = BK7258_BOARD_LCD1_FBNO,
  },
  {
    .transport =
    {
      .reset = aidk_lcd_reset,
      .writecmd = aidk_lcd_writecmd,
      .writegram = aidk_lcd_writegram,
      .backlight = aidk_lcd_backlight,
    },
    .bus_config =
    {
      .name = "AIDK AI Toy LCD2 transport",
      .spi_id = BK7258_BOARD_LCD2_SPI_ID,
      .reset_gpio = BK7258_BOARD_LCD2_RESET_GPIO,
      .dc_gpio = BK7258_BOARD_LCD2_DC_GPIO,
      .width = BK7258_BOARD_LCD_WIDTH,
      .height = BK7258_BOARD_LCD_HEIGHT,
    },
    .bus = NULL,
    .lcddev = NULL,
    .fbno = BK7258_BOARD_LCD2_FBNO,
  }
};

static FAR struct aidk_lcd_panel_s *aidk_lcd_from_transport(
  FAR struct gc9d01_lcd_s *lcd)
{
  return (FAR struct aidk_lcd_panel_s *)lcd;
}

static int aidk_lcd_backlight_drive(bool on)
{
  gpio_id_t gpio = (gpio_id_t)BK7258_BOARD_LCD_BACKLIGHT_PWM_GPIO;
  bool level = on ? BK7258_BOARD_LCD_BACKLIGHT_ACTIVE_HIGH :
                    !BK7258_BOARD_LCD_BACKLIGHT_ACTIVE_HIGH;

  /* QSPI0 initialization temporarily maps P25 as its unused IO1 lane.  The
   * panels use single-line data on P4/P24, so always reclaim P25 before
   * driving the shared active-high backlight transistor.
   */

  if (gpio_dev_unmap(gpio) != BK_OK ||
      bk_gpio_disable_input(gpio) != BK_OK ||
      bk_gpio_enable_output(gpio) != BK_OK ||
      bk_gpio_set_capacity(gpio, GPIO_DRIVER_CAPACITY_0) != BK_OK ||
      bk_gpio_set_output_value(gpio, level) != BK_OK)
    {
      return -EIO;
    }

  return OK;
}

static int aidk_lcd_backlight_restore(void)
{
  int ret;

  ret = nxmutex_lock(&g_aidk_lcd_backlight_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = aidk_lcd_backlight_drive(g_aidk_lcd_backlight_users != 0);
  nxmutex_unlock(&g_aidk_lcd_backlight_lock);
  return ret;
}

static int aidk_lcd_reset(FAR struct gc9d01_lcd_s *lcd, bool asserted)
{
  FAR struct aidk_lcd_panel_s *panel = aidk_lcd_from_transport(lcd);
  return bk7258_lcd_spi_reset(panel->bus, asserted);
}

static int aidk_lcd_writecmd(FAR struct gc9d01_lcd_s *lcd, uint8_t cmd,
                             FAR const uint8_t *params, size_t nparams)
{
  FAR struct aidk_lcd_panel_s *panel = aidk_lcd_from_transport(lcd);
  return bk7258_lcd_spi_writecmd(panel->bus, cmd, params, nparams);
}

static int aidk_lcd_writegram(FAR struct gc9d01_lcd_s *lcd,
                              FAR const uint16_t *pixels, size_t width,
                              size_t height, size_t stride)
{
  FAR struct aidk_lcd_panel_s *panel = aidk_lcd_from_transport(lcd);
  return bk7258_lcd_spi_writegram(panel->bus, pixels, width, height, stride);
}

static int aidk_lcd_backlight(FAR struct gc9d01_lcd_s *lcd, int power)
{
  FAR struct aidk_lcd_panel_s *panel = aidk_lcd_from_transport(lcd);
  uint8_t bit;
  uint8_t users;
  int ret;

  bit = (uint8_t)(1u << panel->fbno);
  ret = nxmutex_lock(&g_aidk_lcd_backlight_lock);
  if (ret < 0)
    {
      return ret;
    }

  users = power > 0 ? (uint8_t)(g_aidk_lcd_backlight_users | bit) :
                      (uint8_t)(g_aidk_lcd_backlight_users &
                                (uint8_t)~bit);
  ret = aidk_lcd_backlight_drive(users != 0);
  if (ret == OK)
    {
      g_aidk_lcd_backlight_users = users;
    }

  nxmutex_unlock(&g_aidk_lcd_backlight_lock);
  return ret;
}

FAR struct lcd_dev_s *board_graphics_setup(unsigned int devno)
{
  FAR struct aidk_lcd_panel_s *panel;
  int ret;

  if (devno >= nitems(g_aidk_lcd_panels))
    {
      return NULL;
    }

  panel = &g_aidk_lcd_panels[devno];
  ret = nxmutex_lock(&g_aidk_lcd_setup_lock);
  if (ret < 0)
    {
      return NULL;
    }

  if (panel->lcddev != NULL)
    {
      nxmutex_unlock(&g_aidk_lcd_setup_lock);
      return panel->lcddev;
    }

  ret = bk_gpio_driver_init();
  if (ret != BK_OK)
    {
      goto errout;
    }

  if (!g_aidk_lcd_power_voted)
    {
      ret = bk_pm_module_vote_ctrl_external_ldo(
              GPIO_CTRL_LDO_MODULE_LCD,
              (gpio_id_t)BK7258_BOARD_LCD_LDO_GPIO,
              GPIO_OUTPUT_STATE_HIGH);
      if (ret != BK_OK)
        {
          goto errout;
        }

      g_aidk_lcd_power_voted = true;
      (void)nxsig_usleep(AIDK_LCD_POWER_SETTLE_US);
    }

  /* Keep both backlights dark while either controller maps its pins and the
   * corresponding panel receives the initialization sequence.
   */

  ret = aidk_lcd_backlight_drive(false);
  if (ret < 0)
    {
      goto errout;
    }

  ret = bk7258_lcd_spi_bus_initialize(&panel->bus_config, &panel->bus);
  if (ret < 0)
    {
      goto errout_restore_backlight;
    }

  panel->lcddev = gc9d01_lcdinitialize(&panel->transport, panel->fbno);
  if (panel->lcddev == NULL)
    {
      bk7258_lcd_spi_bus_uninitialize(panel->bus);
      panel->bus = NULL;
      goto errout_restore_backlight;
    }

  syslog(LOG_INFO,
         "AIDK GC9D01 panel ready fb=%u spi=%u dc=P%u reset=P%u\n",
         panel->fbno, panel->bus_config.spi_id, panel->bus_config.dc_gpio,
         panel->bus_config.reset_gpio);
  nxmutex_unlock(&g_aidk_lcd_setup_lock);
  return panel->lcddev;

errout_restore_backlight:
  (void)aidk_lcd_backlight_restore();
errout:
  nxmutex_unlock(&g_aidk_lcd_setup_lock);
  return NULL;
}

int bk7258_aidk_dual_lcd_initialize(void)
{
  int ret;

  ret = fb_register(BK7258_BOARD_LCD1_FBNO, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = fb_register(BK7258_BOARD_LCD2_FBNO, 0);
  if (ret < 0)
    {
      (void)aidk_lcd_backlight_restore();
      return ret;
    }

  syslog(LOG_INFO,
         "AIDK LCD BOOT PASS panel=GC9D01 size=160x160 "
         "dev=/dev/fb0,/dev/fb1 backlight=P25 shared ldo=P52\n");
  return OK;
}

#endif /* CONFIG_BK7258_AIDK_DUAL_LCD */
