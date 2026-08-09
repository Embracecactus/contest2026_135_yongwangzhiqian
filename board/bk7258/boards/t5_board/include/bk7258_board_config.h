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

#define BK7258_BOARD_USER_LED_GPIO               1
#define BK7258_BOARD_USER_LED_ACTIVE_HIGH        1
#define BK7258_BOARD_USER_BUTTON_GPIO            28
#define BK7258_BOARD_USER_BUTTON_ACTIVE_LOW      1
#define BK7258_BOARD_SPEAKER_CONTROL_GPIO        9

/* TF-card wiring source-verified from T5-Board V1.0.2. */

#define BK7258_BOARD_SDIO_D2_GPIO                10
#define BK7258_BOARD_SDIO_D3_GPIO                11
#define BK7258_BOARD_SDIO_CLK_GPIO               8
#define BK7258_BOARD_SDIO_CMD_GPIO               3
#define BK7258_BOARD_SDIO_D0_GPIO                4
#define BK7258_BOARD_SDIO_D1_GPIO                5
#define BK7258_BOARD_SDIO_CARD_DETECT_GPIO       6
#define BK7258_BOARD_SDIO_CARD_DETECT_ACTIVE_LOW 1

#endif /* __BOARD_BK7258_T5_BOARD_CONFIG_H */
