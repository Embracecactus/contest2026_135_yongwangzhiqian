/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/src/bk7258_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal board bringup for the Beken BK7258 (T5-AI) Stage N1 port.
 *
 * N1 never reaches nx_start() (the chip __start prints the banner and
 * halts), so no board-level initialisation ever runs at runtime.  We still
 * provide board_app_initialize() because NuttX's board library build
 * expects at least one board source file to exist, and the prototype in
 * include/nuttx/board.h must be satisfied for the link to succeed even if
 * the function is dead code.
 *
 * Stage N2 will replace this with a real bringup (console, MTD, etc.).
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <nuttx/board.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   Standard NuttX board Application-level initialization hook.  N1 has
 *   nothing to do; return OK so any future caller proceeds.
 *
 * Input Parameters:
 *   arg - Board-specific argument (unused).
 *
 * Returned Value:
 *   Zero (OK) on success.
 *
 ****************************************************************************/

int board_app_initialize(uintptr_t arg)
{
  return 0;
}
