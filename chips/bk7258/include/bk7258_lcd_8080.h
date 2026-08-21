/****************************************************************************
 * board/bk7258/chip/include/bk7258_lcd_8080.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 8080-MCU LCD framebuffer controller and board/panel binding.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_8080_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_8080_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bk7258_lcd_8080_board_s
{
  const char *name;
  uint16_t width;
  uint16_t height;
  const void *sdk_device;   /* const lcd_device_t * panel descriptor */

  int (*control_pins_initialize)(const struct bk7258_lcd_8080_board_s *board);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_BK7258_LCD_8080) && defined(CONFIG_BK7258_AP_CORE)

const struct bk7258_lcd_8080_board_s *bk7258_board_lcd_8080_config(void);
int bk7258_lcd_8080_initialize(void);

#endif /* CONFIG_BK7258_LCD_8080 && CONFIG_BK7258_AP_CORE */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_8080_H */
