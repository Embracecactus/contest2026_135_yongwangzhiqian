/****************************************************************************
 * chips/bk7258/include/bk7258_lcd.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 RGB framebuffer controller and board/panel binding contract.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_lcd_pixel_format_e
{
  BK7258_LCD_PIXEL_FORMAT_RGB565 = 0,
};

enum bk7258_lcd_rgb_signal_e
{
  BK7258_LCD_RGB_R3 = 0,
  BK7258_LCD_RGB_R4,
  BK7258_LCD_RGB_R5,
  BK7258_LCD_RGB_R6,
  BK7258_LCD_RGB_R7,
  BK7258_LCD_RGB_G2,
  BK7258_LCD_RGB_G3,
  BK7258_LCD_RGB_G4,
  BK7258_LCD_RGB_G5,
  BK7258_LCD_RGB_G6,
  BK7258_LCD_RGB_G7,
  BK7258_LCD_RGB_B3,
  BK7258_LCD_RGB_B4,
  BK7258_LCD_RGB_B5,
  BK7258_LCD_RGB_B6,
  BK7258_LCD_RGB_B7,
  BK7258_LCD_RGB_CLK,
  BK7258_LCD_RGB_DE,
  BK7258_LCD_RGB_HSYNC,
  BK7258_LCD_RGB_VSYNC,
  BK7258_LCD_RGB_SIGNAL_COUNT,
};

struct bk7258_lcd_rgb_pin_s
{
  enum bk7258_lcd_rgb_signal_e signal;
  uint8_t pin;
};

struct bk7258_lcd_board_s;

/* A panel descriptor supplies scanout geometry.  Its register protocol is
 * initialized by the board through a transport-independent panel driver.
 */

struct bk7258_lcd_panel_s
{
  const char *name;
  uint16_t width;
  uint16_t height;
  enum bk7258_lcd_pixel_format_e format;
};

/* RGB timing belongs to the physical board/panel combination. */

struct bk7258_lcd_rgb_timing_s
{
  uint8_t pixel_clock_mhz;
  bool data_changes_on_rising_edge;
  uint16_t hsync_back_porch;
  uint16_t hsync_front_porch;
  uint16_t vsync_back_porch;
  uint16_t vsync_front_porch;
  uint8_t hsync_pulse_width;
  uint8_t vsync_pulse_width;
};

/* The panel-control bus is board wiring, even when its protocol is owned by
 * a reusable panel driver.
 */

struct bk7258_lcd_control_bus_s
{
  uint8_t clock_gpio;
  uint8_t chip_select_gpio;
  uint8_t data_gpio;
  uint8_t reset_gpio;
};

/* A board binds one panel to its physical pins and timing. */

struct bk7258_lcd_board_s
{
  const char *name;
  const struct bk7258_lcd_panel_s *panel;
  struct bk7258_lcd_rgb_timing_s timing;
  struct bk7258_lcd_control_bus_s control;

  int (*control_pins_initialize)(const struct bk7258_lcd_board_s *board);
  int (*rgb_pins_initialize)(const struct bk7258_lcd_board_s *board);
  int (*set_backlight)(const struct bk7258_lcd_board_s *board, bool on);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_BK7258_LCD_RGB) && defined(CONFIG_BK7258_AP_CORE)

/* Register the selected RGB panel as the standard NuttX /dev/fb0 device. */

int bk7258_lcd_initialize(const struct bk7258_lcd_board_s *board);

/* Bind one complete physical RGB bus.  The chip implementation translates
 * logical RGB signals to the private SDK device-selector namespace.
 */

int bk7258_lcd_rgb_configure_pins(
  const struct bk7258_lcd_rgb_pin_s *pins, size_t count);

#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_H */
