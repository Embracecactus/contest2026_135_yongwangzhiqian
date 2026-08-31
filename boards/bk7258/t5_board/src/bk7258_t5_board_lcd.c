/****************************************************************************
 * boards/bk7258/t5_board/src/
 * bk7258_t5_board_lcd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5-Board V1.0.2 physical binding for its T35P128CQ-02 LCD sub-board.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_T5_BOARD_LCD

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_lcd.h>
#include <arch/chip/bk7258_lcd_3wire.h>
#include <arch/chip/bk7258_pinmux.h>

#include <nuttx/lcd/ili9488_rgb.h>

static int t5_board_lcd_control_pins_initialize(
  const struct bk7258_lcd_board_s *board)
{
  const uint8_t pins[] =
  {
    board->control.clock_gpio,
    board->control.chip_select_gpio,
    board->control.data_gpio,
    board->control.reset_gpio,
    BK7258_BOARD_LCD_BACKLIGHT_GPIO,
  };
  const bool initial_high[] =
  {
    true,
    true,
    false,
    false,
    BK7258_BOARD_LCD_BACKLIGHT_ACTIVE_HIGH == 0,
  };
  unsigned int i;
  int ret;

  for (i = 0; i < sizeof(pins) / sizeof(pins[0]); i++)
    {
      ret = bk7258_gpio_configure_output(
        pins[i], initial_high[i],
        i < 4 ? BK7258_GPIO_DRIVE_3 : BK7258_GPIO_DRIVE_0);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

static int t5_board_lcd_rgb_pins_initialize(
  const struct bk7258_lcd_board_s *board)
{
  static const struct bk7258_lcd_rgb_pin_s pins[] =
  {
    { BK7258_LCD_RGB_R3, 23 },
    { BK7258_LCD_RGB_R4, 22 },
    { BK7258_LCD_RGB_R5, 21 },
    { BK7258_LCD_RGB_R6, 20 },
    { BK7258_LCD_RGB_R7, 19 },
    { BK7258_LCD_RGB_G2, 42 },
    { BK7258_LCD_RGB_G3, 41 },
    { BK7258_LCD_RGB_G4, 40 },
    { BK7258_LCD_RGB_G5, 26 },
    { BK7258_LCD_RGB_G6, 25 },
    { BK7258_LCD_RGB_G7, 24 },
    { BK7258_LCD_RGB_B3, 47 },
    { BK7258_LCD_RGB_B4, 46 },
    { BK7258_LCD_RGB_B5, 45 },
    { BK7258_LCD_RGB_B6, 44 },
    { BK7258_LCD_RGB_B7, 43 },
    { BK7258_LCD_RGB_CLK, 14 },
    { BK7258_LCD_RGB_DE, 16 },
    { BK7258_LCD_RGB_HSYNC, 17 },
    { BK7258_LCD_RGB_VSYNC, 18 },
  };

  (void)board;
  return bk7258_lcd_rgb_configure_pins(
    pins, sizeof(pins) / sizeof(pins[0]));
}

static int t5_board_lcd_set_backlight(
  const struct bk7258_lcd_board_s *board, bool on)
{
  bool level = on ? BK7258_BOARD_LCD_BACKLIGHT_ACTIVE_HIGH :
                    !BK7258_BOARD_LCD_BACKLIGHT_ACTIVE_HIGH;

  (void)board;
  return bk7258_gpio_write(BK7258_BOARD_LCD_BACKLIGHT_GPIO, level);
}

static const struct bk7258_lcd_panel_s g_t5_board_ili9488_panel =
{
  .name   = "ili9488",
  .width  = 320,
  .height = 480,
  .format = BK7258_LCD_PIXEL_FORMAT_RGB565,
};

static const struct bk7258_lcd_board_s g_t5_board_lcd =
{
  .name  = "T5-Board V1.0.2 T35P128CQ-02",
  .panel = &g_t5_board_ili9488_panel,
  .timing =
  {
    .pixel_clock_mhz            = 15,
    .data_changes_on_rising_edge = true,
    .hsync_back_porch           = 80,
    .hsync_front_porch          = 80,
    .vsync_back_porch           = 8,
    .vsync_front_porch          = 8,
    .hsync_pulse_width          = 20,
    .vsync_pulse_width          = 4,
  },
  .control =
  {
    .clock_gpio      = BK7258_BOARD_LCD_SPI_CLK_GPIO,
    .chip_select_gpio = BK7258_BOARD_LCD_SPI_CS_GPIO,
    .data_gpio       = BK7258_BOARD_LCD_SPI_SDI_GPIO,
    .reset_gpio      = BK7258_BOARD_LCD_RESET_GPIO,
  },
  .control_pins_initialize = t5_board_lcd_control_pins_initialize,
  .rgb_pins_initialize     = t5_board_lcd_rgb_pins_initialize,
  .set_backlight           = t5_board_lcd_set_backlight,
};

static struct bk7258_lcd_3wire_s g_t5_board_lcd_3wire =
{
  .clock_gpio       = BK7258_BOARD_LCD_SPI_CLK_GPIO,
  .chip_select_gpio = BK7258_BOARD_LCD_SPI_CS_GPIO,
  .data_gpio        = BK7258_BOARD_LCD_SPI_SDI_GPIO,
  .reset_gpio       = BK7258_BOARD_LCD_RESET_GPIO,
};

static const struct ili9488_rgb_ops_s g_t5_board_ili9488_ops =
{
  .reset = bk7258_lcd_3wire_reset,
  .write = bk7258_lcd_3wire_write,
};

int bk7258_t5_board_lcd_initialize(void)
{
  int ret;

  ret = g_t5_board_lcd.control_pins_initialize(&g_t5_board_lcd);
  if (ret < 0)
    {
      return ret;
    }

  ret = g_t5_board_lcd.set_backlight(&g_t5_board_lcd, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_lcd_3wire_initialize(&g_t5_board_lcd_3wire);
  if (ret < 0)
    {
      return ret;
    }

  ret = ili9488_rgb_initialize(&g_t5_board_ili9488_ops,
                                &g_t5_board_lcd_3wire);
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_lcd_initialize(&g_t5_board_lcd);
}

#endif /* CONFIG_BK7258_T5_BOARD_LCD */
