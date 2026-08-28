/****************************************************************************
 * contest2026_135_yongwangzhiqian/boards/bk7258/t5_board/src/
 * bk7258_t5_board_lcd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5-Board V1.0.2 physical binding for its T35P128CQ-02 LCD sub-board.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_LCD

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_lcd.h>

#include <driver/gpio.h>

#include "ili9488_rgb.h"

/* The v3.1.1.9 AP archive exposes the GPIO device mapper but not its internal
 * header through the board wrapper bundle.
 */

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);
extern bk_err_t gpio_dev_map(gpio_id_t gpio_id, gpio_dev_t dev);

/* GPIO_CONFIG6 selects alternate functions for GPIO48..55.  Clear only the
 * four panel-control selectors; GPIO51/52/54/55 may belong to other devices.
 */

#define T5_BOARD_GPIO_CONFIG6_REG       0x440100d8u
#define T5_BOARD_LCD_CONTROL_MASK       ((0xfu << 0)  | (0xfu << 4) | \
                                         (0xfu << 8)  | (0xfu << 20))

static int t5_board_lcd_control_pins_initialize(
  const struct bk7258_lcd_board_s *board)
{
  const gpio_id_t pins[] =
  {
    (gpio_id_t)board->control.clock_gpio,
    (gpio_id_t)board->control.chip_select_gpio,
    (gpio_id_t)board->control.data_gpio,
    (gpio_id_t)board->control.reset_gpio,
    (gpio_id_t)BK7258_BOARD_LCD_BACKLIGHT_GPIO,
  };
  bk_err_t ret;
  unsigned int i;

  ret = bk_gpio_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  for (i = 0; i < sizeof(pins) / sizeof(pins[0]); i++)
    {
      ret = gpio_dev_unmap(pins[i]);
      if (ret != BK_OK)
        {
          syslog(LOG_ERR, "T5-Board LCD: GPIO%d unmap failed: %d\n",
                 pins[i], ret);
          return -EIO;
        }
    }

  *(volatile uint32_t *)T5_BOARD_GPIO_CONFIG6_REG &=
    ~T5_BOARD_LCD_CONTROL_MASK;

  for (i = 0; i < sizeof(pins) / sizeof(pins[0]); i++)
    {
      ret = bk_gpio_disable_input(pins[i]);
      if (ret != BK_OK)
        {
          return -EIO;
        }

      ret = bk_gpio_enable_output(pins[i]);
      if (ret != BK_OK)
        {
          return -EIO;
        }

      (void)bk_gpio_set_capacity(pins[i],
                                 i < 4 ? GPIO_DRIVER_CAPACITY_3 :
                                         GPIO_DRIVER_CAPACITY_0);
    }

  return OK;
}

static int t5_board_lcd_rgb_pins_initialize(
  const struct bk7258_lcd_board_s *board)
{
  static const struct
  {
    gpio_id_t pin;
    gpio_dev_t dev;
  } pins[] =
  {
    { GPIO_23, GPIO_DEV_LCD_R3 },
    { GPIO_22, GPIO_DEV_LCD_R4 },
    { GPIO_21, GPIO_DEV_LCD_R5 },
    { GPIO_20, GPIO_DEV_LCD_R6 },
    { GPIO_19, GPIO_DEV_LCD_R7 },
    { GPIO_42, GPIO_DEV_LCD_G2 },
    { GPIO_41, GPIO_DEV_LCD_G3 },
    { GPIO_40, GPIO_DEV_LCD_G4 },
    { GPIO_26, GPIO_DEV_LCD_G5 },
    { GPIO_25, GPIO_DEV_LCD_G6 },
    { GPIO_24, GPIO_DEV_LCD_G7 },
    { GPIO_47, GPIO_DEV_LCD_B3 },
    { GPIO_46, GPIO_DEV_LCD_B4 },
    { GPIO_45, GPIO_DEV_LCD_B5 },
    { GPIO_44, GPIO_DEV_LCD_B6 },
    { GPIO_43, GPIO_DEV_LCD_B7 },
    { GPIO_14, GPIO_DEV_LCD_CLK },
    { GPIO_16, GPIO_DEV_LCD_DE },
    { GPIO_17, GPIO_DEV_LCD_HSYNC },
    { GPIO_18, GPIO_DEV_LCD_VSYNC },
  };
  bk_err_t ret;
  unsigned int i;

  (void)board;

  for (i = 0; i < sizeof(pins) / sizeof(pins[0]); i++)
    {
      ret = gpio_dev_unmap(pins[i].pin);
      if (ret != BK_OK)
        {
          syslog(LOG_ERR, "T5-Board LCD: RGB GPIO%d unmap failed: %d\n",
                 pins[i].pin, ret);
          return -EIO;
        }

      ret = gpio_dev_map(pins[i].pin, pins[i].dev);
      if (ret != BK_OK)
        {
          syslog(LOG_ERR, "T5-Board LCD: RGB GPIO%d map failed: %d\n",
                 pins[i].pin, ret);
          return -EIO;
        }

      ret = bk_gpio_enable_output(pins[i].pin);
      if (ret != BK_OK)
        {
          syslog(LOG_ERR, "T5-Board LCD: RGB GPIO%d output failed: %d\n",
                 pins[i].pin, ret);
          return -EIO;
        }

      (void)bk_gpio_set_capacity(pins[i].pin, GPIO_DRIVER_CAPACITY_1);
    }

  return OK;
}

static int t5_board_lcd_set_backlight(
  const struct bk7258_lcd_board_s *board, bool on)
{
  bool level = on ? BK7258_BOARD_LCD_BACKLIGHT_ACTIVE_HIGH :
                    !BK7258_BOARD_LCD_BACKLIGHT_ACTIVE_HIGH;

  (void)board;
  return bk_gpio_set_output_value(
           (gpio_id_t)BK7258_BOARD_LCD_BACKLIGHT_GPIO, level) == BK_OK ?
         OK : -EIO;
}

static const struct bk7258_lcd_board_s g_t5_board_lcd =
{
  .name  = "T5-Board V1.0.2 T35P128CQ-02",
  .panel = &g_bk7258_ili9488_rgb_panel,
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

int bk7258_t5_board_lcd_initialize(void)
{
  return bk7258_lcd_initialize(&g_t5_board_lcd);
}

#endif /* CONFIG_BK7258_LCD */
