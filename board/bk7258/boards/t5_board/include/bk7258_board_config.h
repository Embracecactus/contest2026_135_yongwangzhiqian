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
#define BK7258_BOARD_HAS_SPI_LCD_CONNECTOR       1
#define BK7258_BOARD_HAS_DVP_CONNECTOR           1
#define BK7258_BOARD_HAS_NATIVE_USB_HOST         1
#define BK7258_BOARD_HAS_GT1151_TOUCH            1

/* Two differential on-board analog microphones are fitted: MIC1 is routed
 * through MICP1/MICN1 and MIC2 through MICP2/MICN2.
 */

#define BK7258_BOARD_MIC_CHANNELS                 2
#define BK7258_BOARD_HAS_MIC1                     1
#define BK7258_BOARD_HAS_MIC2                     1

#define BK7258_BOARD_USER_LED_GPIO               1
#define BK7258_BOARD_USER_LED_ACTIVE_HIGH        1
#define BK7258_BOARD_USER_LED_CONSOLE_SHARED     1
#define BK7258_BOARD_USER_BUTTON_GPIO            12
#define BK7258_BOARD_USER_BUTTON_ACTIVE_LOW      1
#define BK7258_BOARD_SPEAKER_CONTROL_GPIO        28
#define BK7258_BOARD_SPEAKER_ACTIVE_HIGH          1
#define BK7258_BOARD_SPEAKER_ON_DELAY_MS         10u
#define BK7258_BOARD_SPEAKER_OFF_DELAY_MS        30u

/* T35P128CQ-02 RGB LCD sub-board wiring.  The serial control signals and
 * backlight enable are source-verified against T5-Board V1.0.2 and
 * LCD display sub-board schematics.
 */

#define BK7258_BOARD_LCD_SPI_CLK_GPIO             49
#define BK7258_BOARD_LCD_SPI_CS_GPIO              48
#define BK7258_BOARD_LCD_SPI_SDI_GPIO             50
#define BK7258_BOARD_LCD_RESET_GPIO               53
#define BK7258_BOARD_LCD_BACKLIGHT_GPIO           9
#define BK7258_BOARD_LCD_BACKLIGHT_PWM_CHAN        3
#define BK7258_BOARD_LCD_BACKLIGHT_PWM_FREQUENCY 1000u
#define BK7258_BOARD_LCD_BACKLIGHT_ACTIVE_HIGH    1

/* SPI LCD sub-board wiring supplied for the T5-Board expansion connector.
 * Its backlight is distinct from the RGB LCD sub-board above.  The official
 * BK7258 v3.1.1.9 GPIO map routes PWM channel 5 to GPIO25. */

#define BK7258_BOARD_SPI_LCD_BACKLIGHT_GPIO       25
#define BK7258_BOARD_SPI_LCD_BACKLIGHT_PWM_CHAN   5
#define BK7258_BOARD_SPI_LCD_BACKLIGHT_ACTIVE_HIGH 1

/* GT1151 capacitive touch controller on the LCD sub-board. */

#define BK7258_BOARD_TOUCH_I2C_BUS                0
#define BK7258_BOARD_TOUCH_I2C_ADDRESS            0x14
#define BK7258_BOARD_TOUCH_I2C_MAX_FREQUENCY     100000u
#define BK7258_BOARD_TOUCH_I2C_SCL_GPIO           13
#define BK7258_BOARD_TOUCH_I2C_SDA_GPIO           15
#define BK7258_BOARD_TOUCH_INTERRUPT_GPIO         55
#define BK7258_BOARD_TOUCH_RESET_GPIO             54

/* DVP camera connector P10, source-verified from T5-Board V1.0.2.  GPIO13
 * and GPIO15 are the camera sensor's control bus on this board; this wiring
 * deliberately differs from the SDK's generic GPIO0/GPIO1 example macro.
 * The RGB LCD uses many of the same pins, so board Kconfig prevents the two
 * routes from being initialized at the same time. */

#define BK7258_BOARD_DVP_I2C_BUS                  2
#define BK7258_BOARD_DVP_I2C_FREQUENCY           100000u
#define BK7258_BOARD_DVP_I2C_SCL_GPIO            13
#define BK7258_BOARD_DVP_I2C_SDA_GPIO            15
#define BK7258_BOARD_DVP_RESET_GPIO              51
#define BK7258_BOARD_DVP_PWDN_GPIO               0xff
#define BK7258_BOARD_DVP_MCLK_GPIO                27
#define BK7258_BOARD_DVP_PCLK_GPIO                29
#define BK7258_BOARD_DVP_HSYNC_GPIO               30
#define BK7258_BOARD_DVP_VSYNC_GPIO               31
#define BK7258_BOARD_DVP_D0_GPIO                  32
#define BK7258_BOARD_DVP_D1_GPIO                  33
#define BK7258_BOARD_DVP_D2_GPIO                  34
#define BK7258_BOARD_DVP_D3_GPIO                  35
#define BK7258_BOARD_DVP_D4_GPIO                  36
#define BK7258_BOARD_DVP_D5_GPIO                  37
#define BK7258_BOARD_DVP_D6_GPIO                  38
#define BK7258_BOARD_DVP_D7_GPIO                  39

/* TF-card wiring source-verified from T5-Board V1.0.2.  S1-1/S1-2 connect
 * the CH342F download UART to P10/P11 when ON, so four-bit TF operation
 * requires both switches OFF.  U3 is marked NC in the supplied schematic;
 * fitting that optional serial flash would electrically share CLK/CMD/D0/D1
 * and make the TF socket unavailable in both one- and four-bit modes.
 */

#define BK7258_BOARD_SDIO_U3_FLASH_FITTED         0

#define BK7258_BOARD_SDIO_D2_GPIO                10
#define BK7258_BOARD_SDIO_D3_GPIO                11
#define BK7258_BOARD_SDIO_CLK_GPIO               2
#define BK7258_BOARD_SDIO_CMD_GPIO               3
#define BK7258_BOARD_SDIO_D0_GPIO                4
#define BK7258_BOARD_SDIO_D1_GPIO                5

/* The schematic labels P6 as the socket card-detect contact, but real-board
 * sampling found the input high both with the card inserted and removed,
 * including after applying the SDK's input/pull-up configuration.  Keep the
 * schematic metadata below, but do not advertise it as a usable media-change
 * source.  The slot is therefore fixed media: insert the card before reset.
 */

#define BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE  0
#define BK7258_BOARD_SDIO_CARD_DETECT_GPIO       6
#define BK7258_BOARD_SDIO_CARD_DETECT_ACTIVE_LOW 1
#define BK7258_BOARD_SDIO_MEDIA_POLL_MS          100u

#endif /* __BOARD_BK7258_T5_BOARD_CONFIG_H */
