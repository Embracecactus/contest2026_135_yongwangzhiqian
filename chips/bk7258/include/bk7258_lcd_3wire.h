/****************************************************************************
 * chips/bk7258/include/bk7258_lcd_3wire.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_3WIRE_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_3WIRE_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

struct bk7258_lcd_3wire_s
{
  uint8_t clock_gpio;
  uint8_t chip_select_gpio;
  uint8_t data_gpio;
  uint8_t reset_gpio;
};

#ifdef __cplusplus
extern "C"
{
#endif

int bk7258_lcd_3wire_initialize(void *arg);
int bk7258_lcd_3wire_reset(void *arg, bool high);
int bk7258_lcd_3wire_write(void *arg, bool data_phase, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_3WIRE_H */
