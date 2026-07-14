/****************************************************************************
 * Contest 2026 team 135 - RV1126B EVB board early initialization
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/board.h>

#include "rv1126b_evb.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: boardinitialize
 *
 * Description:
 *   Called from chip start code very early in the boot sequence.  This
 *   function performs minimal board-level hardware initialization (e.g.,
 *   early UART setup, GPIO for LEDs, etc.).  It is called before the OS
 *   is fully initialized, so only very basic hardware init should be done
 *   here.
 *
 ****************************************************************************/

void boardinitialize(void)
{
  /* Minimal hardware initialization for early boot.
   * UART console, clocks, and GPIO are typically initialized by the
   * chip-level startup code before this point.
   */
}

/****************************************************************************
 * Name: board_late_initialize
 *
 * Description:
 *   Called during OS initialization after the OS has been initialized but
 *   before the init daemon is started.  This is where drivers, filesystems,
 *   and other higher-level resources should be set up.
 *
 ****************************************************************************/

void board_late_initialize(void)
{
  rv1126b_bringup();
}
