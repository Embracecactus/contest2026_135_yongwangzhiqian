/****************************************************************************
 * board/bk7258/boards/t5ai_core/src/bk7258_board_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5AI-Core-specific peripheral registration hooks.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <arch/board/board.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_board_early_initialize(void)
{
  return OK;
}

int bk7258_board_devices_initialize(void)
{
  return OK;
}
