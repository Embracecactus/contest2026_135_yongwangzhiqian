/****************************************************************************
 * board/bk7258/boards/aidk_ai_toy/src/bk7258_board_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal board hook: UART0 is owned by the shared chip console path and no
 * unverified external device is registered here.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <arch/board/board.h>

int bk7258_board_early_initialize(void)
{
  return OK;
}

int bk7258_board_devices_initialize(void)
{
  return OK;
}
