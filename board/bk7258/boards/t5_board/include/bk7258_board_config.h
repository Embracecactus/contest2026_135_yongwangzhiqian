/****************************************************************************
 * board/bk7258/boards/t5_board/include/bk7258_board_config.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Physical-board description for T5-Board V1.0.2.
 ****************************************************************************/

#ifndef __BOARD_BK7258_T5_BOARD_CONFIG_H
#define __BOARD_BK7258_T5_BOARD_CONFIG_H

#define BK7258_BOARD_VARIANT_ID                 "t5_board"
#define BK7258_BOARD_VARIANT_NAME               "T5-Board"
#define BK7258_BOARD_HARDWARE_VERSION           "V1.0.2"
#define BK7258_BOARD_SCHEMATIC                   "T5-Board_V102_SCH250617.pdf"

#define BK7258_BOARD_HAS_USB_UART                1
#define BK7258_BOARD_HAS_AUDIO                   1
#define BK7258_BOARD_HAS_BATTERY                 0
#define BK7258_BOARD_HAS_TF_CARD                 1
#define BK7258_BOARD_HAS_RGB_LCD_CONNECTOR       1
#define BK7258_BOARD_HAS_DVP_CONNECTOR           1
#define BK7258_BOARD_HAS_NATIVE_USB_HOST          1
#define BK7258_BOARD_HAS_GT1151_TOUCH             1

#define BK7258_BOARD_USER_LED_GPIO               1
#define BK7258_BOARD_USER_LED_ACTIVE_HIGH        1
#define BK7258_BOARD_USER_LED_CONSOLE_SHARED     1
#define BK7258_BOARD_USER_BUTTON_GPIO            12
#define BK7258_BOARD_USER_BUTTON_ACTIVE_LOW      1
#define BK7258_BOARD_SPEAKER_CONTROL_GPIO        28

/* T35P128CQ-02 RGB LCD sub-board wiring.  The serial control signals and
 * backlight enable are source-verified against T5-Board V1.0.2 and
 * LCD display sub-board schematics.
 */

#define BK7258_BOARD_LCD_SPI_CLK_GPIO             49
#define BK7258_BOARD_LCD_SPI_CS_GPIO              48
#define BK7258_BOARD_LCD_SPI_SDI_GPIO             31
#define BK7258_BOARD_LCD_RESET_GPIO               53
#define BK7258_BOARD_LCD_BACKLIGHT_GPIO           9
#define BK7258_BOARD_LCD_BACKLIGHT_ACTIVE_HIGH    1

/* GT1151 capacitive touch controller on the LCD sub-board. */

#define BK7258_BOARD_TOUCH_I2C_BUS                0
#define BK7258_BOARD_TOUCH_I2C_ADDRESS            0x14
#define BK7258_BOARD_TOUCH_I2C_SCL_GPIO           13
#define BK7258_BOARD_TOUCH_I2C_SDA_GPIO           15
#define BK7258_BOARD_TOUCH_INTERRUPT_GPIO         50
#define BK7258_BOARD_TOUCH_RESET_GPIO             54

/* TF-card wiring source-verified from T5-Board V1.0.2. */

#define BK7258_BOARD_SDIO_D2_GPIO                10
#define BK7258_BOARD_SDIO_D3_GPIO                11
#define BK7258_BOARD_SDIO_CLK_GPIO               2
#define BK7258_BOARD_SDIO_CMD_GPIO               3
#define BK7258_BOARD_SDIO_D0_GPIO                4
#define BK7258_BOARD_SDIO_D1_GPIO                5
#define BK7258_BOARD_SDIO_CARD_DETECT_GPIO       6
#define BK7258_BOARD_SDIO_CARD_DETECT_ACTIVE_LOW 0

#endif /* __BOARD_BK7258_T5_BOARD_CONFIG_H */
