/****************************************************************************
 * board/bk7258/boards/t5ai_core/include/bk7258_board_config.h
 * bk7258_board_config.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Physical-board description for T5AI-Core V1.0.1.
 ****************************************************************************/

#ifndef __BOARD_BK7258_T5AI_CORE_CONFIG_H
#define __BOARD_BK7258_T5AI_CORE_CONFIG_H

#define BK7258_BOARD_VARIANT_ID                 "t5ai_core"
#define BK7258_BOARD_VARIANT_NAME               "T5AI-Core"
#define BK7258_BOARD_HARDWARE_VERSION           "V1.0.1"
#define BK7258_BOARD_SCHEMATIC                   \
  "T5AI-Core_V101-SCH-a69f7b5a91b4bf21a39bdb7c17812373.pdf"

#define BK7258_BOARD_HAS_USB_UART                1
#define BK7258_BOARD_HAS_AUDIO                   1
#define BK7258_BOARD_HAS_BATTERY                 1
#define BK7258_BOARD_HAS_TF_CARD                 0
#define BK7258_BOARD_HAS_RGB_LCD_CONNECTOR       0
#define BK7258_BOARD_HAS_DVP_CONNECTOR           0

#define BK7258_BOARD_USER_LED_GPIO               9
#define BK7258_BOARD_USER_LED_ACTIVE_HIGH        1
#define BK7258_BOARD_USER_BUTTON_GPIO            29
#define BK7258_BOARD_USER_BUTTON_ACTIVE_LOW      1
#define BK7258_BOARD_SPEAKER_CONTROL_GPIO        39
#define BK7258_BOARD_BATTERY_ADC_GPIO            28
#define BK7258_BOARD_CHARGE_DETECT_GPIO          38

#endif /* __BOARD_BK7258_T5AI_CORE_CONFIG_H */
