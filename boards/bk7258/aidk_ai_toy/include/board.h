/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * Beken BK7258 AIDK AI Toy board interface
 ****************************************************************************/

#ifndef __BOARDS_BK7258_AIDK_AI_TOY_INCLUDE_BOARD_H
#define __BOARDS_BK7258_AIDK_AI_TOY_INCLUDE_BOARD_H

#include <arch/board/bk7258_board_config.h>
#include <bk7258_board.h>

#ifdef CONFIG_BK7258_AIDK_BUTTONS
#  undef BOARD_NBUTTONS
#  define BOARD_NBUTTONS 3
#  define BUTTON_K1      (1u << 0) /* P13, volume down */
#  define BUTTON_K2      (1u << 1) /* P12, power */
#  define BUTTON_K3      (1u << 2) /* P8, volume up */
int bk7258_board_buttons_initialize(void);
#endif

#ifdef CONFIG_BK7258_AIDK_MOTOR
#  include <stdbool.h>
int bk7258_aidk_motor_initialize(void);
int bk7258_aidk_motor_capture_quiet(bool quiet);
#endif

#endif
