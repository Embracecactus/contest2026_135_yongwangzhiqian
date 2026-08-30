/****************************************************************************
 * chips/bk7258/include/bk7258_lcd_spi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SPI-over-QSPI transport for NuttX LCD panel drivers.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_BK7258_LCD_SPI_H
#define __ARCH_ARM_SRC_BK7258_BK7258_LCD_SPI_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/compiler.h>

struct bk7258_lcd_spi_bus_s;

struct bk7258_lcd_spi_config_s
{
  FAR const char *name;
  uint8_t spi_id;       /* BK7258 QSPI controller index: 0 or 1 */
  uint8_t reset_gpio;   /* Active-low panel reset */
  uint8_t dc_gpio;      /* Panel data/command GPIO */
  uint16_t width;
  uint16_t height;
};

#ifdef __cplusplus
extern "C"
{
#endif

int bk7258_lcd_spi_bus_initialize(
  FAR const struct bk7258_lcd_spi_config_s *config,
  FAR struct bk7258_lcd_spi_bus_s **bus);
void bk7258_lcd_spi_bus_uninitialize(
  FAR struct bk7258_lcd_spi_bus_s *bus);
int bk7258_lcd_spi_reset(FAR struct bk7258_lcd_spi_bus_s *bus,
                         bool asserted);
int bk7258_lcd_spi_writecmd(FAR struct bk7258_lcd_spi_bus_s *bus,
                            uint8_t cmd, FAR const uint8_t *params,
                            size_t nparams);
int bk7258_lcd_spi_writegram(FAR struct bk7258_lcd_spi_bus_s *bus,
                             FAR const uint16_t *pixels, size_t width,
                             size_t height, size_t stride);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_BK7258_LCD_SPI_H */
