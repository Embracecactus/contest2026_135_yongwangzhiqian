/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/include/
 * bk7258_lcd.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI Board) LCD — direct NuttX framebuffer wrapper for the
 * 3.5" ILI9488 TFT (320x480 RGB565).
 *
 * The ILI9488 is connected as an RGB parallel display whose register
 * initialization is driven by a software (bit-banged) SPI on the T5-AI
 * Board (CLK=GPIO49, CSX=GPIO48, SDA=GPIO50, RST=GPIO53; no DC line).
 * Pixel data flows over the RGB parallel bus from a memory frame buffer
 * that the SDK RGB controller refreshes continuously.
 *
 * AP role: the 34 bk_lcd_* symbols live exclusively in the AP libdriver.a
 * (verified with `nm`; CP exports zero), so this driver is AP-only.
 *
 * Architecture:
 *   - Software SPI bit-bang (modelled on TuyaOpen's tdd_disp_sw_spi.c)
 *     sends the ILI9488 initialization sequence at bring-up.
 *   - bk_lcd_driver_init() + bk_lcd_rgb_init() configure the RGB timing.
 *   - A PSRAM frame buffer holds the 320x480 RGB565 image;
 *     lcd_driver_set_display_base_addr() points the RGB hardware at it.
 *   - The same buffer is exported as /dev/fb0, avoiding the generic LCD
 *     adapter's second full-size SRAM shadow buffer.
 *
 * ILI9488 init sequence is from TuyaOpen (tdd_disp_rgb_ili9488.c).
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Panel geometry. */

#define BK7258_LCD_WIDTH                320
#define BK7258_LCD_HEIGHT               480
#define BK7258_LCD_BPP                  16    /* RGB565 */

/* Software SPI control pins (T5-AI Board ILI9488 module). */

#define BK7258_LCD_SW_SPI_CLK_PIN       49
#define BK7258_LCD_SW_SPI_CSX_PIN       48
#define BK7258_LCD_SW_SPI_SDA_PIN       50
#define BK7258_LCD_SW_SPI_RST_PIN       53

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_LCD
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_lcd_initialize
 *
 * Description:
 *   Bring up the ILI9488 RGB LCD: software-SPI init sequence, RGB timing
 *   configuration, and direct /dev/fb0 registration.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int bk7258_lcd_initialize(void);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_LCD */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LCD_H */
