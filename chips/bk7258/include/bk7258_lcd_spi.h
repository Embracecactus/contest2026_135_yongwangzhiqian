/****************************************************************************
 * board/bk7258/chip/include/bk7258_lcd_spi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SPI LCD framebuffer controller and board/panel binding contract.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_SPI_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_SPI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* A board binds one SPI panel to the SDK panel descriptor and physical
 * control pins.  The SDK descriptor type is intentionally opaque here so
 * the chip public header stays free of immutable-bundle includes; the chip
 * lower half casts it back to the SDK lcd_device_t in its own translation
 * unit.
 */

struct bk7258_lcd_spi_board_s
{
  const char *name;
  uint8_t spi_id;       /* BK7258 SPI controller index: 0 or 1 */
  uint8_t reset_gpio;   /* Panel RESET pin */
  uint8_t dc_gpio;      /* Panel DC/RS pin */
  uint16_t width;       /* Panel visible width in pixels */
  uint16_t height;      /* Panel visible height in pixels */
  const void *sdk_device; /* const lcd_device_t * panel descriptor */

  /* Optional board hook that runs after the chip has verified the binding
   * but before the SDK SPI-LCD init sequence starts.
   */

  int (*control_pins_initialize)(const struct bk7258_lcd_spi_board_s *board);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_BK7258_LCD_SPI) && defined(CONFIG_BK7258_AP_CORE)

/* Implemented by the selected physical board. */

const struct bk7258_lcd_spi_board_s *bk7258_board_lcd_spi_config(void);

/* Register the selected SPI panel as the standard NuttX /dev/fb0 device. */

int bk7258_lcd_spi_initialize(void);

#endif /* CONFIG_BK7258_LCD_SPI && CONFIG_BK7258_AP_CORE */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_SPI_H */
