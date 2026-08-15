/****************************************************************************
 * board/bk7258/boards/aidk_ai_toy/include/bk7258_board_config.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-layer facts for the schematic-only AIDK AI Toy bring-up target.
 * Unknown BOM routes are intentionally represented as unavailable.
 ****************************************************************************/

#ifndef __BOARD_BK7258_AIDK_AI_TOY_CONFIG_H
#define __BOARD_BK7258_AIDK_AI_TOY_CONFIG_H

#define BK7258_BOARD_VARIANT_ID                 "aidk_ai_toy"
#define BK7258_BOARD_VARIANT_NAME               "AIDK AI Toy"
#define BK7258_BOARD_HARDWARE_VERSION           "schematic-only"
#define BK7258_BOARD_SCHEMATIC                   "AIDK_AI_Toy_schematic-owner-supplied"

/* UART0 is the sole documented console/download binding. */

#define BK7258_BOARD_CONSOLE_UART_ID             0
#define BK7258_BOARD_CONSOLE_BAUD                115200u
#define BK7258_BOARD_CONSOLE_DATA_BITS          8
#define BK7258_BOARD_CONSOLE_PARITY             0 /* none */
#define BK7258_BOARD_CONSOLE_STOP_BITS           1
#define BK7258_BOARD_CONSOLE_FLOW_CONTROL        0
#define BK7258_BOARD_CONSOLE_RTS_RESET           0
#define BK7258_BOARD_PORT_IDENTITY               "dynamic-usb-serial"

/* Debug/reset controls are deliberately not claimed by this binding. */

#define BK7258_BOARD_HAS_SWD                     0
#define BK7258_BOARD_BOOT_HOLD                   0
#define BK7258_BOARD_HAS_RTT                     0

/* No BOM-backed peripheral is enabled by the minimal bring-up product. */

#define BK7258_BOARD_HAS_USB_UART                1
#define BK7258_BOARD_HAS_AUDIO                   0
#define BK7258_BOARD_HAS_BATTERY                 0
#define BK7258_BOARD_HAS_TF_CARD                 0
#define BK7258_BOARD_HAS_SD_NAND                 0
#define BK7258_BOARD_HAS_RGB_LCD_CONNECTOR       0
#define BK7258_BOARD_HAS_SPI_LCD_CONNECTOR       0
#define BK7258_BOARD_HAS_DVP_CONNECTOR           0
#define BK7258_BOARD_HAS_CAMERA                  0
#define BK7258_BOARD_HAS_MFRC522                 0
#define BK7258_BOARD_HAS_SC7A20                  0
#define BK7258_BOARD_HAS_USB0                    0

#define BK7258_BOARD_MINIMAL_BRINGUP             1
#define BK7258_BOARD_HARDWARE_VERIFIED           0

/* Schematic conflict records; no route is enabled from these facts. */

#define BK7258_BOARD_CONFLICT_P20_P21_SC7A20_SWD 1
#define BK7258_BOARD_CONFLICT_P0_P1_MFRC522_CN1  1
#define BK7258_BOARD_CONFLICT_P8_P9_32K_KEY3_MOTOR 1
#define BK7258_BOARD_CONFLICT_USB0_UNKNOWN       1

#endif /* __BOARD_BK7258_AIDK_AI_TOY_CONFIG_H */
